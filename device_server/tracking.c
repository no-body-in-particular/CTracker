#include "tracking.h"
#include "logfiles.h"
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

/*
 * Adaptive tracking.
 *
 * Two things drive the location interval: how fast the device is moving, and the wearer's
 * heart rate. Either crossing its threshold marks the device active and shortens the
 * interval; once neither has been seen for ACTIVITY_DWELL it goes back to the idle one.
 *
 * Two things this has to get right, both learned the hard way:
 *
 * A device routinely holds more than one connection open at a time, and every connection
 * is its own struct. Keeping the rate limits in the connection meant each one ran its own
 * copy and they contradicted each other - one deciding the wearer was idle while another
 * still had them active, sending the watch an interval change every few seconds and
 * ignoring the cooldown entirely. The state that has to be shared therefore lives in a
 * per device file, next to the other per device state, so every connection reads and
 * writes the same values. It also survives a restart, so a daemon that comes back up does
 * not immediately reconfigure every device it sees.
 *
 * A resting heart rate sits close enough to the active threshold to cross it constantly,
 * so a single threshold flaps. Going active needs HEARTRATE_ACTIVE_BPM, but going idle
 * again needs it to fall below the lower HEARTRATE_CALM_BPM as well as the dwell expiring.
 *
 * Only protocols implementing an interval command take part. The rest leave
 * supports_interval false and are skipped, because COMMAND_FUNCTION forwards anything it
 * does not recognise to the device verbatim.
 */

typedef struct {
    time_t last_change;         //when an interval command was last sent, any connection
    unsigned int interval;      //what we last asked for
    time_t last_health_poll;    //when a health reading was last requested, any connection
    int active;                 //whether the wearer is currently considered active
    time_t last_activity;       //last movement or raised heart rate, any connection
    unsigned long owner;        //connection_id allowed to send to this device
    unsigned long polls_missed; //health polls sent with no reading back since (diagnostic only)
    unsigned long health_seen;  //this device has answered a health poll at least once
    time_t last_recovery;       //when the device was last restarted to recover health
    time_t last_health_reading; //when a reading was last received, drives the recovery timeout
    time_t last_interval_cfg;   //when the device was last told its own health reporting period
    time_t owner_seen;          //last time the owning connection was still here
    int worn;                   //1 on the body, 0 taken off, -1 never reported
} tracking_state;

static void read_state(connection * conn, tracking_state * st)
{
    memset(st, 0, sizeof(*st));
    st->worn = -1;

    if (strlen(conn->tracking_file) < 16) {
        return;
    }

    FILE * fp = fopen(conn->tracking_file, "r");

    if (fp <= 0) {
        return;
    }

    unsigned long a = 0, c = 0, e = 0;
    unsigned int b = 0;
    int d = 0;

    unsigned long f = 0, g = 0, h = 0, i = 0, j = 0, k = 0, l = 0;
    int m = -1;

    //still accepts a file written before the later fields existed - fscanf simply stops early
    //and they keep the zero memset put there. A missing last_health_reading reads as 0, which
    //the recovery check treats as "no reading time on record yet" and so never restarts on it
    //until the device sends a reading and stamps a real time.
    if (fscanf(fp, "%lu %u %lu %d %lu %lu %lu %lu %lu %lu %lu %lu %d", &a, &b, &c, &d, &e, &f, &g, &h, &i, &j, &k, &l, &m) >= 5) {
        st->last_change = (time_t)a;
        st->interval = b;
        st->last_health_poll = (time_t)c;
        st->active = d;
        st->last_activity = (time_t)e;
        st->owner = f;
        st->polls_missed = g;
        st->health_seen = h;
        st->last_recovery = (time_t)i;
        st->last_health_reading = (time_t)j;
        st->last_interval_cfg = (time_t)k;
        //a state file written before this field existed leaves it zero, which reads as an
        //owner last seen at the epoch - stale, so the first connection along takes over. That
        //is the right answer for an upgrade: nothing is holding the claim.
        st->owner_seen = (time_t)l;
        //-1 where the device has never said, which is not the same as "taken off"
        st->worn = m;
    }

    fclose(fp);
}

static void write_state(connection * conn, tracking_state * st)
{
    if (strlen(conn->tracking_file) < 16) {
        return;
    }

    FILE * fp = fopen(conn->tracking_file, "w");

    if (fp <= 0) {
        return;
    }

    fprintf(fp, "%lu %u %lu %d %lu %lu %lu %lu %lu %lu %lu %lu %d\n",
            (unsigned long)st->last_change, st->interval,
            (unsigned long)st->last_health_poll, st->active,
            (unsigned long)st->last_activity, st->owner,
            st->polls_missed, st->health_seen,
            (unsigned long)st->last_recovery,
            (unsigned long)st->last_health_reading,
            (unsigned long)st->last_interval_cfg,
            (unsigned long)st->owner_seen,
            st->worn);
    fclose(fp);
}

/*
 * The tracking file is a single struct rewritten whole, and a device routinely holds
 * several connections at once - each its own thread sharing this file. Without a lock,
 * two poll passes in the same second both read the same polls_missed and both write it
 * back plus one, so the counter climbs in bursts and a healthy device that is still
 * answering trips the health-recovery restart; equally, an activity or interval write
 * that read the count a moment earlier can clobber a reset note_health() just made.
 *
 * Every read-modify-write of the file therefore runs under an exclusive advisory lock,
 * held from the read through the write. The lock lives in a sibling .lock file so the
 * "w" truncation of the state file itself never races the lock, and the network send
 * that follows a write is done after unlocking, so no I/O happens under the lock.
 *
 * Read-only lookups (is_command_owner) are left unlocked: at worst they see the state
 * a moment stale, which the next pass corrects, and they never touch the counter.
 */
static int lock_state(connection * conn)
{
    if (strlen(conn->tracking_file) < 16) {
        return -1;                  //no per device file yet - nothing to serialise against
    }

    char lockpath[FILENAME_MAX];
    snprintf(lockpath, sizeof(lockpath), "%s.lock", conn->tracking_file);

    int fd = open(lockpath, O_CREAT | O_RDWR, 0660);

    if (fd < 0) {
        return -1;                  //fall back to lockless rather than drop the update
    }

    if (flock(fd, LOCK_EX) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void unlock_state(int fd)
{
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

/*
 * A device commonly leaves an older connection half open after reconnecting. Both are live
 * as far as the server is concerned, but only the newest one is actually being read by the
 * device - anything written to the older socket disappears. Sending from every connection
 * therefore meant commands were regularly delivered nowhere, and the ones that did arrive
 * were competing with each other.
 *
 * The newest connection to identify itself takes ownership, and only the owner sends.
 * Ownership is claimed rather than released, so there is nothing to clean up if a
 * connection dies: the next one to arrive simply takes over.
 */
void claim_command_ownership(connection * conn)
{
    int lock = lock_state(conn);
    tracking_state st;
    read_state(conn, &st);

    if (st.owner == conn->connection_id) {
        unlock_state(lock);
        return;
    }

    st.owner = conn->connection_id;
    st.owner_seen = time(0);
    write_state(conn, &st);
    unlock_state(lock);
    log_line(conn, "command ownership taken by connection %lu\n", conn->connection_id);
}

bool is_command_owner(connection * conn)
{
    tracking_state st;
    read_state(conn, &st);

    //nothing has claimed it yet - let this connection through rather than go silent
    if (st.owner == 0) {
        return true;
    }

    if (st.owner == conn->connection_id) {
        //keep the claim warm, so another connection can tell an owner that is still here
        //from one that has gone. Rate limited: this is called several times a second.
        if ((time(0) - st.owner_seen) > OWNER_REFRESH) {
            int lock = lock_state(conn);
            read_state(conn, &st);

            if (st.owner == conn->connection_id) {
                st.owner_seen = time(0);
                write_state(conn, &st);
            }

            unlock_state(lock);
        }

        return true;
    }

    /*
     * Somebody else holds the claim. If it has not been heard from in a while it is not
     * coming back - a connection that died without releasing anything, or one belonging to
     * the other server process - so take it over rather than leave the device with nothing
     * able to send to it. Before this, such a device stopped being polled and its queued
     * commands sat in the file indefinitely, with nothing in the log to say why.
     */
    if ((time(0) - st.owner_seen) > OWNER_TIMEOUT) {
        log_line(conn, "command owner %lu last seen %lu s ago, taking over\n",
                 st.owner, (unsigned long)(time(0) - st.owner_seen));
        claim_command_ownership(conn);
        return true;
    }

    return false;
}

void note_worn(connection * conn, bool worn)
{
    int lock = lock_state(conn);
    tracking_state st;
    read_state(conn, &st);
    st.worn = worn ? 1 : 0;

    //putting it back on is the moment readings can start again, so do not hold the previous
    //silence against it
    if (worn) {
        st.last_health_reading = time(0);
    }

    write_state(conn, &st);
    unlock_state(lock);
}

static void mark_activity(connection * conn)
{
    int lock = lock_state(conn);
    tracking_state st;
    read_state(conn, &st);
    st.last_activity = time(0);
    write_state(conn, &st);
    unlock_state(lock);
}

/*
 * Any health reading at all - pressure, pulse, temperature, oxygen - answers a poll. Recording
 * it here rather than in note_heartrate() means a device answering with only a temperature is
 * still plainly alive, and does not get restarted for failing to send a pulse.
 */
void note_health(connection * conn)
{
    int lock = lock_state(conn);
    tracking_state st;
    read_state(conn, &st);

    //stamp the reading time every time, since the recovery timeout is measured from it
    st.last_health_reading = time(0);
    st.polls_missed = 0;
    st.health_seen = 1;
    write_state(conn, &st);
    unlock_state(lock);
}

void note_heartrate(connection * conn, int bpm)
{
    if (bpm <= 0) {
        return;
    }

    conn->last_heartrate = bpm;
    conn->last_heartrate_time = time(0);

    if (bpm >= HEARTRATE_ACTIVE_BPM) {
        mark_activity(conn);
    }
}

void note_movement(connection * conn, double speed_kmh, bool speed_known)
{
    //a tower fix cannot measure speed and its position can be kilometres out, so a speed
    //derived from one says nothing about whether the wearer is moving
    if (!speed_known || speed_kmh < MOVING_SPEED_KMH) {
        return;
    }

    mark_activity(conn);
}

//What the device says it is actually on, which beats assuming our command arrived.
void note_device_interval(connection * conn, unsigned int seconds)
{
    if (seconds == 0) {
        return;
    }

    conn->current_interval = seconds;
}

void poll_health(connection * conn)
{
    if (!conn->supports_health_poll || conn->COMMAND_FUNCTION == 0) {
        return;
    }

    //A protocol declares itself the moment it recognises the traffic, which is before the
    //device has said who it is. Without an imei there is no per device file, so read_state
    //hands back zeroes, the rate limit below cannot be stored, and the poll fires on every
    //pass - as "IWBPXL,,080835#", addressed to nobody. Wait until the device has identified.
    if (conn->imei[0] == 0) {
        return;
    }

    if (!is_command_owner(conn)) {
        return;
    }

    int lock = lock_state(conn);
    tracking_state st;
    read_state(conn, &st);

    if ((time(0) - st.last_health_poll) < HEALTH_POLL_INTERVAL) {
        unlock_state(lock);
        return;
    }

    //A device that has been answering and then stops keeps its connection up and keeps
    //reporting positions, so nothing else notices. Restarting it is what brings the readings
    //back. Timed from the last reading actually received, not from a poll count: only for a
    //device that has answered before (last_health_reading > 0), and not again until the
    //cooldown has passed, so a watch that is simply not being worn is not restarted in a loop.
    /*
     * st.worn == 0 means the device told us it is off the body. A watch on a table is not
     * broken and restarting it achieves nothing except a black screen for whoever picks it
     * up next. Only devices that never report wear state (-1) or say they are being worn (1)
     * are candidates.
     */
    /*
     * A watch that is not moving is a watch nobody is wearing.
     *
     * st.worn is the device's own answer and this one has never given it: the
     * field reads -1, "never said", which the test below treats as a candidate
     * because -1 is not 0. That was the right call when the alternative was
     * never restarting a device that cannot report wear - and it means a watch
     * on a bedside table, which has no pulse to report because there is no
     * wrist, looks exactly like a watch whose sensor has wedged.
     *
     * It was restarting all night for it. Twenty seven restarts in a day, one
     * an hour whenever the readings stopped, on a device that was simply lying
     * still.
     *
     * Movement is the signal the device does give. last_activity is stamped by
     * steps, by speed over the ground and by a raised pulse, so a watch being
     * worn stamps it many times an hour and a watch on a table does not stamp
     * it at all. Requiring it to have moved inside the same window as the
     * reading timeout costs nothing where the sensor really has wedged - a
     * wearer moves - and stops the watch being rebooted for the crime of being
     * taken off.
     */
    bool moving_recently = (time(0) - st.last_activity) <= HEALTH_RECOVERY_TIMEOUT;

    if (HEALTH_RECOVERY_TIMEOUT > 0
            && st.worn != 0
            && moving_recently
            && st.last_health_reading > 0
            && (time(0) - st.last_health_reading) > HEALTH_RECOVERY_TIMEOUT
            && (time(0) - st.last_recovery) > HEALTH_RECOVERY_COOLDOWN) {
        long unsigned int gone = (long unsigned int)(time(0) - st.last_health_reading);
        st.polls_missed = 0;
        st.last_recovery = time(0);
        st.last_health_poll = time(0);
        //restart the clock so the reboot gets a full window to answer before we try again
        st.last_health_reading = time(0);
        write_state(conn, &st);
        unlock_state(lock);
        log_line(conn, "no health reading in %lu s - restarting the device\n", gone);
        /*
         * In the event log as well as the device log. This restarts a watch somebody is
         * wearing: it goes dark for several minutes, and the first anyone knew of it was the
         * black screen. Whatever else it is, it should not be a surprise.
         */
        char note[128];
        snprintf(note, sizeof(note),
                 "restarted the device - no health reading for %lu minutes", gone / 60);
        log_event(conn, note);
        conn->COMMAND_FUNCTION(conn, "RESTART#");
        return;
    }

    /*
     * Said once per poll while it applies, so a night with no readings and no
     * restarts is explainable from the log rather than looking like the
     * recovery silently failing.
     */
    if (HEALTH_RECOVERY_TIMEOUT > 0
            && st.last_health_reading > 0
            && (time(0) - st.last_health_reading) > HEALTH_RECOVERY_TIMEOUT
            && !moving_recently) {
        log_line(conn, "no health reading in %lu s, but it has not moved in %lu s"
                       " - not restarting a watch nobody is wearing\n",
                 (long unsigned int)(time(0) - st.last_health_reading),
                 (long unsigned int)(time(0) - st.last_activity));
    }

    /*
     * Where the device can keep its own schedule, tell it once and then leave it alone. It
     * pushes readings on that period without being asked, which is one command every few
     * hours in place of twenty an hour, and lets its radio stay down in between. Polling
     * remains for protocols that cannot be told.
     *
     * The period is re-sent occasionally rather than once ever, so a watch that was reset,
     * factory defaulted or simply did not take the command picks it up again on its own.
     */
    bool tell_interval = DEVICE_HEALTH_INTERVAL_MIN > 0 && conn->supports_health_interval
                         && (time(0) - st.last_interval_cfg) >= DEVICE_HEALTH_INTERVAL_REFRESH;

    if (tell_interval) {
        st.last_interval_cfg = time(0);
    }

    //claim the slot before sending, so a second connection reaching here at the same
    //moment sees the updated time and backs off instead of sending a duplicate. The lock
    //makes that read-and-claim atomic: without it two connections in the same second both
    //read the old time, both pass the interval check, and both increment polls_missed.
    st.last_health_poll = time(0);
    st.polls_missed++;
    write_state(conn, &st);
    unlock_state(lock);

    /*
     * Telling the device its own period is worth doing, but it is not a replacement for
     * asking. Handing the schedule over entirely lost most of the readings: over comparable
     * windows the watch pushed temperature 154 times and a heart rate three times, against 92
     * heart rates, 93 SPO2 and 90 blood pressures when the poll was doing the asking, and
     * sleep readings stopped altogether. What the device volunteers on its own schedule is
     * temperature; HEARTRATE# is what actually fetches the set. So configure the period, and
     * then poll anyway.
     */
    if (tell_interval) {
        char cmd[64] = {0};
        snprintf(cmd, sizeof(cmd), "HEALTHINT=%d,%d",
                 DEVICE_HEALTH_INTERVAL_MIN, DEVICE_HEALTH_INTERVAL_MIN);
        log_line(conn, "setting the device health period to %d minutes\n", DEVICE_HEALTH_INTERVAL_MIN);
        conn->COMMAND_FUNCTION(conn, cmd);
    }

    /*
     * Which trigger to use.
     *
     * HEARTRATE# is IWBPXL, which acknowledges and then measures - usually.
     * When it stops measuring it goes on acknowledging, so nothing looks
     * wrong from here: the watch answers every poll, holds its connection and
     * keeps reporting positions, and only the absence of readings says
     * anything. That state used to persist until the recovery timeout
     * rebooted the watch, half an hour later, on somebody's wrist.
     *
     * PULSE# is IWBP50, the other trigger the protocol defines. It answers
     * with the reading rather than with an acknowledgement, and against this
     * hardware it measured when XL would not. So after a couple of empty
     * polls, ask the other way before reaching for the restart.
     */
    const char * trigger = "HEARTRATE#";
    if (HEALTH_ESCALATE_AFTER > 0 && st.polls_missed > HEALTH_ESCALATE_AFTER) {
        trigger = "PULSE#";
        log_line(conn, "%lu polls with no reading - asking with PULSE# instead\n",
                 st.polls_missed);
    }
    conn->COMMAND_FUNCTION(conn, trigger);
}

void update_tracking_interval(connection * conn)
{
    if (!ADAPTIVE_INTERVAL_ENABLED) {
        return;
    }

    if (!conn->supports_interval || conn->COMMAND_FUNCTION == 0) {
        return;
    }

    if (!is_command_owner(conn)) {
        return;
    }

    int lock = lock_state(conn);
    tracking_state st;
    read_state(conn, &st);

    bool recent = st.last_activity > 0 && (time(0) - st.last_activity) < ACTIVITY_DWELL;
    bool calm = conn->last_heartrate == 0 || conn->last_heartrate < HEARTRATE_CALM_BPM;

    if (!st.active && recent) {
        st.active = 1;
    } else if (st.active && !recent && calm) {
        //both conditions: the dwell has expired and the heart rate has actually settled,
        //otherwise a rate hovering around the threshold flips this back and forth
        st.active = 0;
    }

    unsigned int target = st.active ? TRACK_INTERVAL_ACTIVE : TRACK_INTERVAL_IDLE;

    //until the device has told us what it is on, there is nothing to correct - asking
    //blind would mean a command on every single connect
    if (conn->current_interval == 0) {
        write_state(conn, &st);
        unlock_state(lock);
        return;
    }

    //the device is already where we want it. adopt that into our state rather than sending
    //a command telling it to do what it is already doing, which is what happened on every
    //fresh connection before the state file existed.
    if (conn->current_interval == target) {
        st.interval = target;
        write_state(conn, &st);
        unlock_state(lock);
        return;
    }

    //already asked for this. the device reports its interval only in its heartbeat, so
    //requiring confirmation before considering the job done meant reissuing the same
    //command every cooldown while waiting for the next one. only reissue when the device
    //actively disagrees with what we asked for.
    if (st.interval == target && conn->current_interval == target) {
        write_state(conn, &st);
        unlock_state(lock);
        return;
    }

    if (st.interval == target && (time(0) - st.last_change) < DEVICE_CONFIRM_GRACE) {
        write_state(conn, &st);
        unlock_state(lock);
        return;
    }

    if ((time(0) - st.last_change) < INTERVAL_CHANGE_COOLDOWN) {
        write_state(conn, &st);
        unlock_state(lock);
        return;
    }

    //claim the slot before sending, for the same reason as poll_health
    st.last_change = time(0);
    st.interval = target;
    write_state(conn, &st);
    unlock_state(lock);

    char command[64] = {0};
    snprintf(command, sizeof(command) - 1, "UPDATE=%u#", target);
    conn->COMMAND_FUNCTION(conn, command);

    log_line(conn, "tracking interval -> %u s (%s, hr %d, %lu s since activity)\n",
             target,
             st.active ? "active" : "idle",
             conn->last_heartrate,
             (unsigned long)(st.last_activity > 0 ? time(0) - st.last_activity : 0));
}

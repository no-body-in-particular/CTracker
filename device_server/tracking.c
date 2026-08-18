#include "tracking.h"
#include "logfiles.h"
#include <stdio.h>
#include <memory.h>

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
} tracking_state;

static void read_state(connection * conn, tracking_state * st)
{
    memset(st, 0, sizeof(*st));

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

    unsigned long f = 0;

    if (fscanf(fp, "%lu %u %lu %d %lu %lu", &a, &b, &c, &d, &e, &f) >= 5) {
        st->last_change = (time_t)a;
        st->interval = b;
        st->last_health_poll = (time_t)c;
        st->active = d;
        st->last_activity = (time_t)e;
        st->owner = f;
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

    fprintf(fp, "%lu %u %lu %d %lu %lu\n",
            (unsigned long)st->last_change, st->interval,
            (unsigned long)st->last_health_poll, st->active,
            (unsigned long)st->last_activity, st->owner);
    fclose(fp);
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
    tracking_state st;
    read_state(conn, &st);

    if (st.owner == conn->connection_id) {
        return;
    }

    st.owner = conn->connection_id;
    write_state(conn, &st);
    log_line(conn, "command ownership taken by connection %lu\n", conn->connection_id);
}

bool is_command_owner(connection * conn)
{
    tracking_state st;
    read_state(conn, &st);

    //nothing has claimed it yet - let this connection through rather than go silent
    return st.owner == 0 || st.owner == conn->connection_id;
}

static void mark_activity(connection * conn)
{
    tracking_state st;
    read_state(conn, &st);
    st.last_activity = time(0);
    write_state(conn, &st);
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

    if (!is_command_owner(conn)) {
        return;
    }

    tracking_state st;
    read_state(conn, &st);

    if ((time(0) - st.last_health_poll) < HEALTH_POLL_INTERVAL) {
        return;
    }

    //claim the slot before sending, so a second connection reaching here at the same
    //moment sees the updated time and backs off instead of sending a duplicate
    st.last_health_poll = time(0);
    write_state(conn, &st);

    conn->COMMAND_FUNCTION(conn, "HEARTRATE#");
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
        return;
    }

    //the device is already where we want it. adopt that into our state rather than sending
    //a command telling it to do what it is already doing, which is what happened on every
    //fresh connection before the state file existed.
    if (conn->current_interval == target) {
        st.interval = target;
        write_state(conn, &st);
        return;
    }

    //already asked for this. the device reports its interval only in its heartbeat, so
    //requiring confirmation before considering the job done meant reissuing the same
    //command every cooldown while waiting for the next one. only reissue when the device
    //actively disagrees with what we asked for.
    if (st.interval == target && conn->current_interval == target) {
        write_state(conn, &st);
        return;
    }

    if (st.interval == target && (time(0) - st.last_change) < DEVICE_CONFIRM_GRACE) {
        write_state(conn, &st);
        return;
    }

    if ((time(0) - st.last_change) < INTERVAL_CHANGE_COOLDOWN) {
        write_state(conn, &st);
        return;
    }

    //claim the slot before sending, for the same reason as poll_health
    st.last_change = time(0);
    st.interval = target;
    write_state(conn, &st);

    char command[64] = {0};
    snprintf(command, sizeof(command) - 1, "UPDATE=%u#", target);
    conn->COMMAND_FUNCTION(conn, command);

    log_line(conn, "tracking interval -> %u s (%s, hr %d, %lu s since activity)\n",
             target,
             st.active ? "active" : "idle",
             conn->last_heartrate,
             (unsigned long)(st.last_activity > 0 ? time(0) - st.last_activity : 0));
}

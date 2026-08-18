#include "tracking.h"
#include "logfiles.h"
#include <stdio.h>

/*
 * Adaptive tracking.
 *
 * Two things drive the location interval: how fast the device is moving, and the wearer's
 * heart rate. Either one crossing its threshold marks the device as active and shortens
 * the interval; once neither has been seen for ACTIVITY_DWELL it goes back to the idle
 * interval.
 *
 * The dwell is what keeps this from flapping. Speed between two fixes is a noisy measure -
 * a stop at a junction, a pause mid-run, or a fix that lands badly can all read as
 * stationary for one sample - and without a dwell the device would be sent a new interval
 * every few minutes. INTERVAL_CHANGE_COOLDOWN is a second guard on top of that.
 *
 * Only protocols that implement an interval command take part. The others leave
 * supports_interval false and are skipped entirely: COMMAND_FUNCTION falls through to
 * sending the raw string for anything it does not recognise, so a device that does not
 * speak UPDATE= would otherwise be sent the literal text.
 */

void note_heartrate(connection * conn, int bpm)
{
    if (bpm <= 0) {
        return;
    }

    conn->last_heartrate = bpm;
    conn->last_heartrate_time = time(0);

    if (bpm >= HEARTRATE_ACTIVE_BPM) {
        conn->last_activity = time(0);
    }
}

void note_movement(connection * conn, double speed_kmh, bool speed_known)
{
    //a tower fix cannot measure speed, and its position can be kilometres out, so a speed
    //derived from one says nothing about whether the wearer is moving
    if (!speed_known) {
        return;
    }

    if (speed_kmh >= MOVING_SPEED_KMH) {
        conn->last_activity = time(0);
    }
}

//What the device says it is actually using, which is better than assuming our command
//arrived. Thinkrace reports this in every heartbeat.
void note_device_interval(connection * conn, unsigned int seconds)
{
    if (seconds == 0) {
        return;
    }

    if (conn->current_interval != seconds) {
        log_line(conn, "device reports location interval: %u s\n", seconds);
    }

    conn->current_interval = seconds;
}

void poll_health(connection * conn)
{
    if (!conn->supports_health_poll || conn->COMMAND_FUNCTION == 0) {
        return;
    }

    if ((time(0) - conn->since_last_health_poll) < HEALTH_POLL_INTERVAL) {
        return;
    }

    conn->since_last_health_poll = time(0);
    conn->COMMAND_FUNCTION(conn, "HEARTRATE#");
}

void update_tracking_interval(connection * conn)
{
    if (!conn->supports_interval || conn->COMMAND_FUNCTION == 0) {
        return;
    }

    bool active = conn->last_activity > 0 && (time(0) - conn->last_activity) < ACTIVITY_DWELL;
    unsigned int target = active ? TRACK_INTERVAL_ACTIVE : TRACK_INTERVAL_IDLE;

    if (conn->current_interval == target) {
        return;
    }

    if ((time(0) - conn->last_interval_change) < INTERVAL_CHANGE_COOLDOWN) {
        return;
    }

    char command[64] = {0};
    snprintf(command, sizeof(command) - 1, "UPDATE=%u#", target);
    conn->COMMAND_FUNCTION(conn, command);
    conn->last_interval_change = time(0);

    //assume it took effect so we do not repeat the command every cooldown. where the device
    //reports its own setting, note_device_interval() overrides this with the truth and the
    //command is reissued if it did not actually land.
    conn->current_interval = target;

    log_line(conn, "tracking interval -> %u s (%s, hr %d, %lu s since activity)\n",
             target,
             active ? "active" : "idle",
             conn->last_heartrate,
             (unsigned long)(conn->last_activity > 0 ? time(0) - conn->last_activity : 0));
}


/* API keys live in secrets.h, which is gitignored - copy secrets.h.example to secrets.h
 * and fill in your own. Nothing in this file is a credential, so it can be committed. */
#include "secrets.h"

#define BUF_SIZE 4096 //send/recieve buffer sizes. minimum is 276. I like to keep it one cache page.
#define MAX_FENCE 4096 //max number of geofence entries

#define GRACE_TIME 100 //amount of milliseconds to wait in between one send and recieve cycle
#define LISTEN_ON "0.0.0.0" //IP to listen on, default 0.0.0.0 which is connections from any IP
#define LISTEN_PORT 9000 //port to listen on
//always add a trailing slash in path here
#define OUTDIR "/var/gps/" //directory that contains the output files, this includes process logging per device, plus the event log and the gps log


#define SERVER_TIME_OFFSET 60

/*databases for lbs location */

#define CELLDB_FILE "/var/gps/cell.db"

#define OPENCELLID_FILE "opencellid.csv"
#define MOZCELLID_FILE "mozilla.csv"

/*database for wifi location*/
//#define WIFIDB_FILE "/var/gps/wifi.db"
#define WIFIDB_FILE "/var/gps/wifi.db"

#define CACHE_SAVE_TIME 1200
#define CACHE_ENTRY_RETRY (60*60*128)

#define MAX_DATA_SIZE (1024*1024*30)
#define MAX_LOG_SIZE (1024*1024*4)

/* speeds are computed between consecutive fixes, which can come from GPS, WiFi or cell
 * towers with wildly different accuracy. a source change or a stale previous position
 * can imply a jump of hundreds of km, so anything above this is recorded as 0 rather
 * than charted as real movement. km/h. */
#define MAX_PLAUSIBLE_SPEED 700

/* Adaptive tracking.
 *
 * The location interval is shortened while the wearer is active - either their heart rate
 * is up or they are moving faster than a walk - and put back once they have been still for
 * a while. Only protocols that actually implement an interval command take part; the rest
 * are left alone rather than sent something they will not understand.
 *
 * MOVING_SPEED_KMH sits above brisk walking so that walking does not trigger it, while
 * running, cycling and driving all do. ACTIVITY_DWELL keeps the device in the short
 * interval for a while after the last sign of activity, so waiting at a junction or a
 * quiet minute mid-run does not flip it back and forth. */
/* Interval adaptation. Reconfiguring a watch repeatedly destabilised one: six interval
 * changes in thirty-five minutes left it acknowledging commands without measuring. The
 * causes are fixed - state shared across a device's connections rather than held per
 * connection, and hysteresis on the heart rate - so this is on again, but the cooldown is
 * deliberately long. It caps changes at twelve an hour even if every other guard fails,
 * which is the one thing that would have prevented the original damage. */
#define ADAPTIVE_INTERVAL_ENABLED 1

#define HEALTH_POLL_INTERVAL 180        //how often to ask the device for a reading, seconds

/*
 * Off. The idea was that a device which keeps its own health schedule is cheaper than a poll
 * every few minutes, and the watch does accept the command - it answers IWAP86. What it then
 * does with it is the problem: measured against the archive, heart rate, blood pressure and
 * SPO2 arrived every three minutes under the poll, tracking HEALTH_POLL_INTERVAL exactly, and
 * fell to roughly one set an hour once the watch was given the period. Temperature went the
 * other way and flooded. So whatever IWBP86's two parameters mean, "3" is not three minutes
 * for the readings that matter, and the poll is the thing that actually fetches a set.
 *
 * Left in place rather than deleted because the command itself is understood and confirmed
 * against hardware - see COMMANDS.md - and it may be right for a device that genuinely
 * volunteers a full set. It is not right for this one. Set above 0 to re-enable.
 */
#define DEVICE_HEALTH_INTERVAL_MIN 0
//how often the period is re-sent, so a watch that was reset or lost the setting picks it
//back up without waiting for anything to notice
#define DEVICE_HEALTH_INTERVAL_REFRESH (6 * 60 * 60)
//A watch that has been answering health polls and then stops has been seen to keep the
//connection up, keep reporting positions, and simply never answer again until it is restarted.
//Recovery is timed from the last reading actually received rather than counted in polls: a
//device that reconnects constantly fires its polls in clumps, so a count reached the limit
//in minutes and restarted a device that was still reporting, while a device whose polls stop
//firing (no longer the command owner, say) was never restarted however long its health was
//gone. Once no reading has arrived for this long the device is restarted. Only devices that
//have answered at least once are ever considered - a tracker with no sensor is not broken for
//failing to report a heart rate.
/*
 * How long a device may go without sending any health reading before it is restarted.
 *
 * This was 900 seconds, which against a three minute reporting period is five missed
 * reports - close enough to normal jitter that a watch taken off for a quarter of an hour,
 * or one that simply dropped a few uploads, was rebooted for it.
 *
 * It was then widened to 2700 on the reasoning that a device keeping its own schedule makes
 * a gap less informative - no poll went unanswered, the watch just did not speak. That
 * reasoning went away with the schedule: the poll is back, every poll draws a full set, and
 * an unanswered one means something again. Half an hour is ten reporting periods, which is
 * long enough not to fire on a watch that was taken off for a while, and the cooldown stays
 * well clear of it so a device that rebooting will not fix is not rebooted in a loop.
 */
/*
 * Off, because the restart does not do what it was believed to do.
 *
 * It restarted the watch six times in one night - 18:36, 19:32, 20:39, 21:43, 22:39, 23:42 -
 * each time reporting a gap of about half an hour. Thirty minutes to fire and sixty to cool
 * down is precisely an hourly reboot for as long as readings stay quiet.
 *
 * The case for keeping it had been that readings came back within seconds of a restart, so
 * the restart must have brought them back. Counting the gaps says otherwise. Over one day
 * this watch had eleven pauses in its health readings longer than fifteen minutes. Five of
 * them ended with no restart at all - 25, 56, 37, 17 and 20 minutes - and the longest of
 * those is longer than any gap that ever triggered one. The sensor stops and starts on its
 * own, and a restart landing during a pause that was going to end anyway looks exactly like
 * a restart that fixed something.
 *
 * Which also means the threshold was never separable from normal behaviour: thirty minutes
 * sits in the middle of the range this device pauses for routinely.
 *
 * During a pause the watch stays connected and acknowledges every IWBPXL with an IWAPXL - it
 * is the sensor that is quiet, not the radio - so there is nothing here to detect a fault
 * with either.
 *
 * The wear state cannot rescue this: the firmware never sends IWAPWR, so it stays unknown,
 * and a device that never reports it has to be treated as eligible or the check would
 * disable recovery for every device that does not.
 *
 * Turn it back on only with evidence that a restart ends a pause which would not otherwise
 * have ended - which means a threshold above the longest pause seen without one.
 */
/*
 * There is a second way to ask for a pulse - IWBP50, PULSE# - and it was tried here,
 * because IWBPXL acknowledges and then may or may not measure while BP50 answers with the
 * reading itself. Against this watch it does not help: of thirty nine escalations, two were
 * followed by a reading inside five minutes without a restart intervening, and that one came
 * straight after a restart. Only the reboot revives the sensor. The send path for PULSE#
 * stays - it is a valid command and command.php can still issue it - but the poll does not
 * reach for it.
 */


#define HEALTH_RECOVERY_TIMEOUT 0       //seconds without any reading before restarting; 0 disables
#define HEALTH_RECOVERY_COOLDOWN 3600   //seconds before a device may be restarted again
#define HEARTRATE_ACTIVE_BPM 90         //at or above this counts as active
#define HEARTRATE_CALM_BPM 80           //must fall below this before going idle again
#define MOVING_SPEED_KMH 8              //above a brisk walk: running, cycling, driving
#define TRACK_INTERVAL_ACTIVE 60        //location interval while active, seconds
#define TRACK_INTERVAL_IDLE 600         //location interval while still, seconds
#define ACTIVITY_DWELL 300              //stay active this long after the last activity seen
#define INTERVAL_CHANGE_COOLDOWN 300    //minimum gap between interval commands
//how long to let a device confirm a new interval, in its own heartbeat, before assuming
//the command was lost and reissuing. must exceed the idle reporting interval.
//A device that drops and re-opens its socket every few seconds is doing something normal
//for this hardware, and writing a "reconnected"/"disconnected" pair into the event log each
//time buried the events that mean something. On one device 203 of 262 events were nothing
//but connection churn, hiding six SOS presses among them. Only an absence longer than this
//is worth telling the user about.
#define EVENT_ABSENCE_MIN 300

#define DEVICE_CONFIRM_GRACE 900

/* Stats come from two clocks. Position derived values carry the position message's time,
 * while health values carry the measurement time inside the JK packet, which is typically
 * a handful of seconds earlier. Both are accurate, but a health batch processed after a
 * position lands slightly behind it and the file stops being sorted - which matters
 * because the front end reads through date_grep. A backward step within this tolerance is
 * nudged forward to keep the file ordered; anything larger is genuinely older data, such
 * as a device replaying what it buffered while offline, and is written as it stands. */
#define STAT_ORDER_TOLERANCE 30

//smallest number of access points worth sending to a wifi geolocation lookup
#define WIFI_LOOKUP_MIN 2
//most access points we will carry from one scan
#define WIFI_LOOKUP_MAX 16

/*
 * A device commonly leaves an older connection half open, and only one connection may send to
 * it - see is_command_owner(). The owner refreshes its claim every OWNER_REFRESH seconds, and
 * another connection may take the claim once the owner has been quiet for OWNER_TIMEOUT.
 */
#define OWNER_REFRESH 30                //seconds between refreshes of a live ownership claim
#define OWNER_TIMEOUT 150               //seconds before a silent owner's claim may be taken

/*
 * How long the same geofence alert is kept out of the *event log* for. It does not hold back
 * the alarm: a device outside a mandatory fence is warned on every position report, for as
 * long as it is outside, because that is what a mandatory fence is for.
 *
 * Only the log entry waits. That is where the noise was - 25957 pairs of identical
 * consecutive events ten seconds apart in the recorded history, one fence accounting for
 * 22143 of them - and a log nobody can read is a log that hides the next real alarm.
 */
#define FENCE_REPEAT_INTERVAL 600       //seconds before the same fence alert is logged again

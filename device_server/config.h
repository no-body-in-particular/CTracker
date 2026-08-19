
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
#define HEARTRATE_ACTIVE_BPM 90         //at or above this counts as active
#define HEARTRATE_CALM_BPM 80           //must fall below this before going idle again
#define MOVING_SPEED_KMH 8              //above a brisk walk: running, cycling, driving
#define TRACK_INTERVAL_ACTIVE 60        //location interval while active, seconds
#define TRACK_INTERVAL_IDLE 600         //location interval while still, seconds
#define ACTIVITY_DWELL 300              //stay active this long after the last activity seen
#define INTERVAL_CHANGE_COOLDOWN 300    //minimum gap between interval commands
//how long to let a device confirm a new interval, in its own heartbeat, before assuming
//the command was lost and reissuing. must exceed the idle reporting interval.
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

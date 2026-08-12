
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

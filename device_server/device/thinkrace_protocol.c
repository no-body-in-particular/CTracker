#include <time.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>

#include "../util.h"
#include "../config.h"
#include "../crc16.h"
#include "../string.h"
#include "../connection.h"
#include "../logfiles.h"
#include "../geofence.h"
#include "../commands.h"
#include "../wifi_lookup.h"
#include "../lbs_lookup.h"
#include "../multilaterate.h"
#include "thinkrace_protocol.h"
#include "../images.h"
#include <ctype.h>
#include "../tracking.h"

#define THINKRACE_TIMEOUT 1200

/*
 * The older commands here take the terminating '#' from whatever the caller typed, so
 * "UPDATE=600#" works and "UPDATE=600" silently goes out unterminated - the watch then waits
 * for an end of packet that never arrives and ignores the whole thing. Rather than repeat
 * that trap for every command added below, this appends the value with exactly one '#' on
 * the end however the caller wrote it.
 */
static void send_value_terminated(connection * conn, const char * value) {
    char buf[BUF_SIZE] = {0};
    size_t n = 0;

    while (value[n] && n + 2 < sizeof(buf)) {
        buf[n] = value[n];
        n++;
    }

    while (n > 0 && (buf[n - 1] == '#' || buf[n - 1] == '\r' || buf[n - 1] == '\n')) {
        n--;
    }

    buf[n++] = '#';
    buf[n] = 0;
    send_string(conn, buf);
}

bool thinkrace_send_command( void * c, const char * cmd) {
    connection * conn = (connection *)c;
    char buffer[4] = {0};
    size_t start = conn->send_count;

    if (strcmp(cmd, "HEARTRATE#") == 0) {
        send_string(conn, "IWBPXL,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strcmp(cmd, "SYNCTIME#") == 0) {
        char response[128] = {0};
        time_t now = time(NULL);
        struct tm * t = gmtime(&now); //gmtime for gmt
        struct tm lt = {0};
        localtime_r(&now, &lt);
        int tz = lt.tm_gmtoff / 3600 ;
        strftime(response, BUF_SIZE - 1, "IWBP00,%Y%m%d%H%M%S,", t);
        snprintf(buffer, sizeof(buffer), "%i#", tz);
        strcat(response, buffer);
        send_string(conn, response);

    } else if (strlen(cmd) > 4 && memcmp(cmd, "SMS=", 4) == 0) {
        /*
         * BPSM tunnels one of the watch's SMS commands over the data connection, so the
         * whole "#...#" vocabulary is reachable without sending an actual text message.
         *
         * From the firmware (protocol_beehome.handleBPSM): the packet is split on ',' and
         * must be exactly four fields, the last of which is the command. That command then
         * has '@' rewritten to '#' and '-' rewritten to ',' before being handed to the SMS
         * handler - the substitution exists because '#' terminates a packet and ',' separates
         * its fields, so neither can appear raw. The same rewriting is done here, so a caller
         * writes the command as it would be texted and this puts it on the wire safely.
         *
         *   SMS=#status#          -> @status@
         *   SMS=#USB#=adb         -> @USB@=adb        (adb over USB; there is no network adb)
         *   SMS=#listen#12345678  -> @listen@12345678 (the watch dials that number)
         *   SMS=#capture#         -> @capture@        (take a picture)
         *
         * Untested against hardware at the time of writing: it is built from the firmware's
         * own parser rather than from a document, and a command the watch does not recognise
         * is simply ignored by it.
         */
        char escaped[BUF_SIZE] = {0};
        size_t w = 0;

        for (const char * r = cmd + 4; *r && w + 1 < sizeof(escaped); r++) {
            escaped[w++] = (*r == '#') ? '@' : (*r == ',') ? '-' : *r;
        }

        send_string(conn, "IWBPSM,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_string(conn, escaped);
        send_string(conn, "#");

    /*
     * The commands below are not in the protocol document. Their shapes are taken from the
     * watch's own handlers in protocol_beehome, where each one checks the field count before
     * doing anything - so the count is the specification. Three fields means imei and serial,
     * four adds one value, five adds two.
     *
     *   handleBPOX  4 fields   blood oxygen reading
     *   handleBPTE  4 fields   temperature reading
     *   handleBPTF  3 fields   clock format, logged there as "is24Hour ==> "
     *   handleBPPH  4 fields   call switch, writes persist.sys.phone.enable
     *   handleBPMC  4 fields   motion detection, "BPMC index ==> value==> "
     *   handleBP86  5 fields   heart rate and blood pressure periods, in minutes
     *
     * Untested against hardware; a command the watch does not accept it simply ignores.
     */
    /*
     * The three separate measurement triggers the IW protocol defines - BPXL for a pulse,
     * BPXY for a blood pressure, BPXZ for a blood oxygen. Only BPXL was implemented, on the
     * strength of this firmware answering an XL with all three readings. It does not always:
     * the watch acknowledges IWAPXL and measures nothing, and there was then no other way to
     * ask. Each command is IWBPxx,IMEI,serial# and is answered with a bare IWAPxx.
     */
    /*
     * The rest of the command set the IW protocol defines. Each is IWBPxx,IMEI,serial then
     * whatever the command takes, and each is answered with the matching IWAPxx.
     */
    } else if (strcmp(cmd, "FIND#") == 0) {
        //the watch rings so it can be found
        send_string(conn, "IWBP88,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strcmp(cmd, "PULSE#") == 0) {
        //BP50, the other heart rate trigger. Worth having beside BPXL: the watch has been
        //seen acknowledging an XL and measuring nothing at all.
        send_string(conn, "IWBP50,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strlen(cmd) > 5 && memcmp(cmd, "CALL=", 5) == 0) {
        send_string(conn, "IWBP32,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 5);

    } else if (strlen(cmd) > 4 && memcmp(cmd, "SOS=", 4) == 0) {
        //three numbers, comma separated. An empty one keeps its field: "SOS=111,,333"
        send_string(conn, "IWBP12,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 4);

    } else if (strlen(cmd) > 5 && memcmp(cmd, "LANG=", 5) == 0) {
        //"<language>,<time zone>" - 1 is English, 0 Chinese
        send_string(conn, "IWBP20,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 5);

    } else if (strlen(cmd) > 11 && memcmp(cmd, "DELCONTACT=", 11) == 0) {
        send_string(conn, "IWBP52,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 11);

    } else if (strlen(cmd) > 8 && memcmp(cmd, "CONTACT=", 8) == 0) {
        /*
         * "<name>,<number>". The name goes up as hex, the same way MSG= sends its text - the
         * sample in the protocol document is D3590D54, which is not a phone book entry
         * anybody typed but two unicode characters written out.
         */
        const char * value = cmd + 8;
        const char * comma = strchr(value, ',');

        if (comma == 0) {
            log_line(conn, "CONTACT= wants a name and a number\n");

        } else {
            send_string(conn, "IWBP51,");
            send_string(conn, conn->imei);
            send_string(conn, ",080835,");

            for (const char * p = value; p < comma; p++) {
                snprintf(buffer, sizeof(buffer), "%02X", (unsigned char) * p);
                send_string(conn, buffer);
            }

            send_string(conn, ",");
            send_value_terminated(conn, comma + 1);
        }

    } else if (strlen(cmd) > 10 && memcmp(cmd, "WHITELIST=", 10) == 0) {
        send_string(conn, "IWBP84,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 10);

    } else if (strlen(cmd) > 7 && memcmp(cmd, "SERVER=", 7) == 0) {
        /*
         * "<domain flag>,<host>,<port>". Points the watch at a different server, so a typo
         * loses the device until somebody texts it back - though the same is already reachable
         * through the SMS tunnel as #ip#=, so this adds a shorter spelling rather than a new
         * way to go wrong.
         */
        send_string(conn, "IWBP19,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 7);

    } else if (strcmp(cmd, "BLOODPRESSURE#") == 0) {
        send_string(conn, "IWBPXY,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strcmp(cmd, "OXYGEN#") == 0) {
        send_string(conn, "IWBPXZ,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strcmp(cmd, "SPO2#") == 0) {
        send_string(conn, "IWBPOX,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,1#");

    } else if (strcmp(cmd, "TEMP#") == 0) {
        send_string(conn, "IWBPTE,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,1#");

    } else if (strlen(cmd) > 6 && memcmp(cmd, "HOURS=", 6) == 0) {
        //the watch wants a flag, not the number of hours: 1 means the 24 hour clock
        send_string(conn, "IWBPTF,");
        send_string(conn, conn->imei);
        send_string(conn, ",");
        //IW protocol V2.10: 1 is the 24 hour system and 2 is the 12 hour system. This sent
        //0 for 12 hour, which is not a value the command defines.
        send_string(conn, (strstr(cmd + 6, "24") != 0) ? "1#" : "2#");

    } else if (strlen(cmd) > 6 && memcmp(cmd, "PHONE=", 6) == 0) {
        send_string(conn, "IWBPPH,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 6);

    } else if (strlen(cmd) > 7 && memcmp(cmd, "MOTION=", 7) == 0) {
        send_string(conn, "IWBPMC,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 7);

    } else if (strlen(cmd) > 10 && memcmp(cmd, "HEALTHINT=", 10) == 0) {
        /*
         * IWBP86,IMEI,serial,<switch>,<value>#. The switch opens (1) or closes (0) health
         * monitoring and the value is the detection interval in minutes - it is not, as this
         * previously assumed, a heart rate period and a blood pressure period. Sending a 3
         * into the switch field is undefined, and the readings that matter stopped when it
         * was. Callers pass "<switch>,<minutes>".
         */
        send_string(conn, "IWBP86,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_value_terminated(conn, cmd + 10);

    } else if (strcmp(cmd, "RECORD#") == 0) {
        /*
         * Remote monitor: record from the microphone and upload it. There is no BP command
         * for this in the IW protocol - the trigger lives in the watch's SMS handler, where
         * "#monitor#" runs StartMonitor, which is what drives AudioService.startRecord() and
         * then "Monitor Record to send". So it goes through the BPSM tunnel, with '#' written
         * as '@' because '#' would end the packet.
         *
         * The recording comes back up the same voice-packet path a picture uses, and is told
         * apart from one by its leading bytes rather than by its id.
         */
        send_string(conn, "IWBPSM,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,@monitor@#");

    } else if (strcmp(cmd, "PHOTO#") == 0) {
        //BP46 with content "1" means take one now; the terminal answers AP46 and then
        //uploads the picture over AP42. The manual also documents a BP40 shortcut
        //(">*photo@1*<" in hex) which the PT880 accepts, but BP46 is the dedicated command
        //and reports its own success flag, so prefer it.
        send_string(conn, "IWBP46,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,1#");

    } else  if (strcmp(cmd, "SHUTDOWN#") == 0) {
        send_string(conn, "IWBP31,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strcmp(cmd, "RESTART#") == 0) {
        send_string(conn, "IWBP18,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strcmp(cmd, "LOCATE#") == 0) {
        send_string(conn, "IWBP16,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strcmp(cmd, "FACTORYALL#") == 0) {
        send_string(conn, "IWBP17,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835#");

    } else if (strlen(cmd) > 7 && memcmp(cmd, "UPDATE=", 7) == 0) {
        send_string(conn, "IWBP15,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_string(conn, cmd + 7);

    } else if (strlen(cmd) > 5 && memcmp(cmd, "MODE=", 5) == 0) {
        send_string(conn, "IWBP33,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");
        send_string(conn, cmd + 5);

    /*
     * IWBP34,IMEI,serial,<working mode>,<location interval seconds>,<GPS switch>#
     *
     * This was reached through a command called TIMES=, documented on both sides as "working
     * hours" and sent as IWBP34,imei,serial,1,0000@2359# - a time range in the field that
     * takes an interval in seconds, and no GPS switch at all. The IW protocol has no working
     * hours command; BP34 never set them. Since the third field decides whether the GPS
     * module stays on, leaving it off meant every login sent the watch a command whose last
     * parameter was missing.
     *
     * The value is passed through as given so any mode the firmware knows can be reached, but
     * a GPS switch is appended when the caller leaves it out, because defaulting that to
     * absent is what the old command effectively did.
     */
    } else if (strlen(cmd) > 8 && memcmp(cmd, "LOCMODE=", 8) == 0) {
        const char * value = cmd + 8;
        //count the commas: mode,interval is two fields, mode,interval,gps is three
        size_t fields = 1;

        for (const char * p = value; *p && *p != '#' && *p != '\n'; p++) {
            if (*p == ',') {
                fields++;
            }
        }

        send_string(conn, "IWBP34,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");

        if (fields >= 3) {
            send_value_terminated(conn, value);

        } else {
            //no GPS switch given - keep the module on rather than send the field empty
            char padded[64] = {0};
            size_t w = 0;

            for (const char * p = value; *p && *p != '#' && *p != '\n' && w + 3 < sizeof(padded); p++) {
                padded[w++] = *p;
            }

            padded[w++] = ',';
            padded[w++] = '1';
            padded[w] = 0;
            send_value_terminated(conn, padded);
        }

    } else if (strlen(cmd) > 6 && memcmp(cmd, "MSG=", 4) == 0) {
        send_string(conn, "IWBP40,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");

        for (char * p = cmd + 4; *p != 0 && *p != '\n'; p++) {
            //without the cast a byte above 0x7f becomes a negative int and "%02X" prints
            //eight digits - nine bytes with the terminator, into a buffer of four
            snprintf(buffer, sizeof(buffer), "%02X", (unsigned char)*p);
            send_string(conn, buffer);
        }

        send_string(conn, "#");

    } else {
        send_string(conn, cmd);
    }

    log_line(conn, "sent command: ");

    for (int i = start; i < conn->send_count; i++) {
        logprintf(conn, "%c", ((unsigned char *)conn->send_buffer)[i]);
    }

    logprintf(conn, "\n");
    return true;
}



void thinkrace_process_position(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    int year;
    int month;
    int day;
    char valid;
    float lat;
    float latdeg;
    char is_north;
    float lng;
    float lngdeg;
    char is_east;
    float speed;
    int hour;
    int minute;
    int second;
    float heading;
    int signal_strength;
    int num_sats;
    int battery_level;
    int working_mode;
    int fortification_state;
    //program data
    bool valid_position = false;
    size_t position_type = 0;
    unsigned char * wifi_split[16];
    //this used to be left uninitialised while db_entry.result was read further down, and
    //network_buffer entries whose mac failed to parse kept whatever was on the stack
    wifi_db_entry db_entry = {0};
    unsigned char * current_network[3];

    if (parse_count < 5) {
        log_line(conn, "   invalid location package.\n");
        return;
    }

    if (strlen(data_buffers[0]) < 59) {
        log_line(conn, "Invalid location package recieved.");
        return;
    }

    sscanf(data_buffers[0], "%2d%2d%2d%1c%2f%7f%1c%3f%7f%1c%5f%2d%2d%2d%6f%3d%3d%3d0%2d%2d", &year, &month, &day, &valid, &lat, &latdeg, &is_north, &lng, &lngdeg, &is_east, &speed, &hour, &minute, &second, &heading, &signal_strength, &num_sats, &battery_level, &working_mode, &fortification_state);
    lat += latdeg / 60;
    lng += lngdeg / 60;
    lat = is_north == 'S' ? -lat : lat;
    lng = is_east == 'W' ? -lng : lng;

    if (lat != 0 || lng != 0) {
        valid_position = true;
        position_type = 0;
    }

    if ( parse_count > 5) {
        db_entry.network_count = split_to('&', data_buffers[5], strlen(data_buffers[5]) + 1, wifi_split, 16);

        //Only networks that actually parse are kept, and they are packed down to the front
        //of the buffer. Previously every '&' separated field counted towards network_count
        //even when its mac could not be read, so a single malformed entry handed the
        //lookup a garbage address, and a scan of three fields where two were malformed
        //looked like three usable networks. Anything unusable is now dropped from the
        //count rather than passed on.
        size_t scanned = db_entry.network_count;
        size_t usable = 0;

        for (size_t i = 0; i < scanned && usable < WIFI_LOOKUP_MAX; i++) {
            size_t split_count = split_to('|', wifi_split[i], strlen(wifi_split[i]) + 1, current_network, 3);

            if (split_count != 3) {
                continue;
            }

            unsigned int values[6];

            if (sscanf( current_network[1], "%x:%x:%x:%x:%x:%x",
                        &values[0], &values[1], &values[2],
                        &values[3], &values[4], &values[5] ) != 6) {
                continue;
            }

            unsigned char mac[6];
            bool all_zero = true;
            bool all_ones = true;

            for (int b = 0; b < 6; b++) {
                if (values[b] > 0xff) {
                    all_zero = all_ones = false;
                    break;
                }

                mac[b] = (unsigned char)values[b];

                if (mac[b] != 0x00) {
                    all_zero = false;
                }

                if (mac[b] != 0xff) {
                    all_ones = false;
                }
            }

            //00:00:00:00:00:00 and ff:ff:ff:ff:ff:ff are never a real access point, and a
            //byte outside 0..ff means the field was not a mac at all
            if (all_zero || all_ones) {
                continue;
            }

            bool duplicate = false;

            for (size_t j = 0; j < usable; j++) {
                if (memcmp(db_entry.network_buffer[j].mac_addr, mac, 6) == 0) {
                    duplicate = true;
                    break;
                }
            }

            //the same access point listed twice adds no information but does skew a
            //multilateration towards it
            if (duplicate) {
                continue;
            }

            memcpy(db_entry.network_buffer[usable].mac_addr, mac, 6);
            usable++;
        }

        db_entry.network_count = usable;

        if (usable != scanned) {
            log_line(conn, "   wifi scan: %zu of %zu networks usable\n", usable, scanned);
        }

        //two access points are enough for a lookup to be worth attempting - the old
        //threshold of three threw away scans that would have resolved, and left the device
        //on a cell tower fix (or on no fix at all) instead
        if ( db_entry.network_count >= WIFI_LOOKUP_MIN) {
            if (!valid_position) {
                db_entry.result =  wifi_lookup(db_entry.network_buffer,  db_entry.network_count);

                if (db_entry.result.valid) {
                    valid_position = true;
                    position_type = 2;
                    lat = db_entry.result.lat;
                    lng = db_entry.result.lng;
                    num_sats =  db_entry.network_count;
                }

            } else {
                db_entry.result.lat = lat;
                db_entry.result.lng = lng;
                db_entry.result.last_tried = time(0);
                db_entry.result.valid = true;
                db_entry.result.radius = 10;
                db_entry.result = wifi_to_cache(db_entry);
            }
        }
    }

    if (!valid_position) {
        //when falling back to LBS try to force GPS geolocation
        if ((time(0) - conn->since_last_locate ) > 60) {
            conn->since_last_locate = time(0);
            thinkrace_send_command(conn, "LOCATE#");
        }

        cell_tower tower;
        tower.mcc = parse_int(data_buffers[1], strlen(data_buffers[1]));
        tower.mnc = parse_int(data_buffers[2], strlen(data_buffers[2]));
        tower.lac = parse_int(data_buffers[3], strlen(data_buffers[3]));
        tower.cell_id = parse_int(data_buffers[4], strlen(data_buffers[4]));
        tower.location = lbs_lookup(&tower, conn->current_lat, conn->current_lon);

        if (tower.location.valid) {
            lat = tower.location.lat;
            lng = tower.location.lng;
            speed = 0;
            position_type = 1;
            valid_position = true;
        }
    }

    time_t dt = date_to_time(year, month, day, hour, minute, second);

    if (valid_position) {
        //if we're fairly certain about our location do trigger fences.
        //move_to() adopts this fix's time itself, and it needs the *previous* fix's time
        //still in place to work out how long the device took to get here. Advancing the
        //clock before calling it left that gap at zero, and compute_speed() then fell back
        //to assuming the reporting interval - so a minute of running was divided by ten
        //minutes and every speed came out a tenth of its true value.
        move_to(conn, dt, position_type, lat, lng);
        write_stat(conn, "battery_level", battery_level);
        write_sat_count(conn, position_type, num_sats);
        write_stat(conn, "signal", signal_strength);
        conn->timeout_time = time(0) + THINKRACE_TIMEOUT;

    } else if (dt > conn->device_time) {
        //A position that could not be resolved still tells us what time the device thinks
        //it is. Only move_to() sets conn->device_time, so once the fixes went void and both
        //the wifi and tower lookups failed the clock stopped at the last resolved fix, and
        //every later log line, event and stat was written at that one instant. Forwards
        //only, so a stale fix replayed by a second connection cannot drag it back.
        conn->device_time = dt;
    }
}


void thinkrace_process_event(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 6) {
        log_line(conn, "   invalid heartbeat package.\n");
        return;
    }

    char * name = "unknown";

    switch (parse_int(data_buffers[5], 2)) {
        case 1:
            name = "SOS";
            break;

        case 2:
            name = "Low battery";
            break;

        case 3:
            name = "Pull out alarm";
            break;

        case 4:
            name = "Not wearing device";
            break;

        case 5:
            name = "Tamper alarm";
            break;

        case 6:
            name = "Speeding";
            break;

        case 7:
            name = "Abnormal heart rate";
            break;

        case 9:
            name = "Device has moved";
            break;

        case 10:
            name = "High Systolic blood pressure";
            break;

        case 11:
            name = "Low Systolic blood pressure";
            break;

        case 12:
            name = "High Diastolic blood pressure";
            break;

        case 13:
            name = "Low Diastolic blood pressure";
            break;

        case 14:
            name = "Sedentary reminder";
            break;

        case 15:
            name = "Exit GPS blind zone";
            break;

        case 16:
            name = "Device opened";
            break;

        case 20:
            name = "Geofence exit";
            break;

        case 21:
            name = "Geofence enter";
            break;

        case 22:
            name = "Message read";
            break;

        default:
            name = "unknown";
            break;
    }

    log_event(conn,  name);
}


void thinkrace_process_heartbeat(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 3) {
        log_line(conn, "   invalid heartbeat package.\n");
        return;
    }

    log_line(conn, "number of steps: %s\n", data_buffers[2]);

    //the step field is a running total for the current day that the watch resets at
    //midnight, so it climbs into five digits. every stat shares one y axis on the
    //chart, where the largest other series is systolic pressure at around 120, so the
    //raw count would flatten everything else into a line along the bottom. recording
    //it in thousands keeps it on the same scale as the rest.
    int steps = parse_int(data_buffers[2], strlen(data_buffers[2]));

    //a zero is the watch padding out a report it has no step data for - the real
    //counter only reaches zero at midnight, and it never drops during the day. writing
    //those would saw the line down to the floor between every genuine reading.
    if (steps > 0) {
        //AP03 carries no timestamp of its own, so write_stat() filed this under whatever
        //the last position message left in conn->device_time - up to a whole reporting
        //interval in the past. That backdated the step counts and pushed them behind rows
        //already written, which the front end reads through date_grep and date_grep needs
        //in order. The heartbeat is sent in real time, so stamp it with now, never earlier
        //than the clock we already have.
        time_t when = time(0) > conn->device_time ? time(0) : conn->device_time;
        write_stat_at(conn, "steps_k", steps / 1000.0f, when);
    }

    //the heartbeat carries the interval the device is actually on, which beats assuming
    //our last command landed
    if (parse_count > 5) {
        note_device_interval(conn, parse_int(data_buffers[5], strlen(data_buffers[5])));
    }

    conn->timeout_time = time(0) + THINKRACE_TIMEOUT;
}

void thinkrace_process_heartrate(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 4) {
        log_line(conn, "   invalid heartrate package.\n");
        return;
    }

    write_stat(conn, "heartrate", parse_float(data_buffers[1]));
    write_stat(conn, "systole", parse_float(data_buffers[2]));
    write_stat(conn,  "diastole", parse_float(data_buffers[3]));

    //a dedicated pulse/pressure upload answers the health poll just as a JK packet does
    note_health(conn);
}


void thinkrace_process_temperature(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 3) {
        log_line(conn, "   invalid temperature package.\n");
        return;
    }

    write_stat(conn,  "temperature", parse_float(data_buffers[1]));

    note_health(conn);
}




void thinkrace_process_saturation(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 2) {
        log_line(conn, "   invalid SPO2 package.\n");
        return;
    }

    write_stat(conn,  "SPO2", parse_float(data_buffers[1]));

    note_health(conn);
}

void thinkrace_process_stat(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 4) {
        log_line(conn, "   invalid temperature package.\n");
        return;
    }

    //two characters, not one. the vendor only ever sends 1 to 4 so a single
    //digit was enough, but the launcher's computed sleep metrics run past 9 and
    //a one character read turned type 10 into type 1 - a WASO figure recorded
    //as a blood pressure. widening is safe for the existing types: memcpy takes
    //min(count, strlen), so a one character field still reads as itself.
    int type = parse_int(data_buffers[2], 2);
    int systole = 0;
    int diastole = 0;

    //these readings carry their own measurement time, which is the right stamp for them
    //but runs on a different clock to the position messages - often minutes ahead. it
    //used to be left in conn->device_time afterwards, so it leaked into whatever was
    //written next and the following position message then dragged the clock back,
    //which is what put the reversed timestamps in the stats file. borrow it, then put
    //the position clock back.
    time_t position_clock = conn->device_time;
    conn->device_time = parse_date(data_buffers[1]);

    switch (type) {
        case 1:
            sscanf(data_buffers[3], "%d|%d", &diastole, &systole);
            write_stat(conn,  "diastole", diastole);
            write_stat(conn,  "systole", systole);
            break;

        case 2: {
            int bpm = parse_int(data_buffers[3], 3);
            write_stat(conn,  "heartrate", bpm);
            //feeds the activity test in update_tracking_interval()
            note_heartrate(conn, bpm);
            break;
        }

        case 3:
            write_stat(conn,  "temperature", parse_float(data_buffers[3]));
            break;

        case 4:
            write_stat(conn,  "SPO2", parse_int(data_buffers[3], 3));
            break;

        //5 to 7 are the nightly sleep summary, which the vendor firmware
        //measures into its own SLEEP_STATUS table and then never uploads - the
        //live protocol has no sleep opcode at all. The launcher app reads that
        //table and sends it here on the same JK frame, one reading per type,
        //stamped 08:00 UTC on the night it belongs to.
        //
        //These are minutes as the firmware counts them, which is not what the
        //column names promise: DEEP_SLEEP reaches 675, so it is closer to total
        //sleep than to deep sleep, and LIGHT_SLEEP is almost always zero.
        //Recorded as given rather than reinterpreted here.
        case 5:
            write_stat(conn,  "sleep_deep", parse_int(data_buffers[3], 4));
            break;

        case 6:
            write_stat(conn,  "sleep_light", parse_int(data_buffers[3], 4));
            break;

        //98 on every night with data and 0 on every night without, so this is
        //a "was it recorded" flag rather than a quality score. Kept because it
        //costs nothing, but it is not worth plotting on its own.
        case 7:
            write_stat(conn,  "sleep_score", parse_int(data_buffers[3], 3));
            break;

        //8 to 12 are scored on the watch from its own accelerometer log, by
        //the van Hees angle heuristic, and are the ones worth reading. They
        //are sleep and wake and the timings that follow - not sleep stages,
        //which a wrist accelerometer cannot see whatever the vendor firmware
        //claims by reporting eleven hours of "deep sleep".
        //
        //Standing caveat of all actigraphy: it spots sleep well and wake
        //poorly, because lying still looks like sleeping. Expect total sleep
        //to run long and awakenings to run short.
        case 8:
            write_stat(conn,  "sleep_tst", parse_int(data_buffers[3], 4));
            break;

        case 9:
            write_stat(conn,  "sleep_spt", parse_int(data_buffers[3], 4));
            break;

        case 10:
            write_stat(conn,  "sleep_waso", parse_int(data_buffers[3], 4));
            break;

        case 11:
            write_stat(conn,  "sleep_efficiency", parse_int(data_buffers[3], 3));
            break;

        case 12:
            write_stat(conn,  "sleep_wakeups", parse_int(data_buffers[3], 3));
            break;

        //asleep right now, 1 or 0, sent on every change and refreshed every
        //half hour in between. the chart breaks a series at a 45 minute gap,
        //so without the refresh a night would be two points and no line.
        case 13:
            write_stat(conn,  "sleeping", parse_int(data_buffers[3], 1));
            break;

        //minutes slept so far today, counted against the day the sleep ended:
        //last night plus any nap since.
        case 14:
            write_stat(conn,  "sleep_day", parse_int(data_buffers[3], 4));
            break;

        default:
            break;
    }

    //any recognised reading answers the outstanding poll, whichever kind it was.
    //sleep deliberately does not: it is a nightly summary arriving hours late,
    //not an answer to a health poll, and counting it as one would make the
    //server think a stale reading had satisfied a request it had just made.
    if (type >= 1 && type <= 4) {
        note_health(conn);
    }

    conn->device_time = position_clock;
}



void thinkrace_process_message(connection * conn, char * string, size_t length) {
    time_t now = time(NULL);
    struct tm * t = gmtime(&now); //gmtime for gmt
    unsigned char bufstr[BUF_SIZE] = {0};
    unsigned char response[BUF_SIZE] = {0};
    unsigned char * data_buffers[40] = {0};
    unsigned char imei[64] = {0};
    uint8_t message_type = 0;
    string = strip_whitespace(string);
    log_line(conn, "got message: %s\n", string);

    //TQ is handled further down now, where it at least leaves a record of having been asked
    if (memcmp(string, "IWAPVR", 6) == 0 || memcmp(string, "IWAPXL", 6) == 0) {
        return;
    }

    memset(imei, 0, 64);
    memset(bufstr, 0, BUF_SIZE);
    memset(response, 0, BUF_SIZE);
    memset(data_buffers, 0, sizeof(data_buffers));
    memcpy(bufstr, string, min(length, BUF_SIZE - 1));
    message_type = parse_int(string + 4, 2);
    size_t str_count = split_to(',', bufstr + 6, BUF_SIZE, data_buffers, 40);
    log_line(conn, "split message:", string);

    for (size_t i = 0; i < str_count; i++) {
        logprintf(conn, " [%u] %s", i, data_buffers[i]);
    }

    logprintf(conn, "\n");

    if (memcmp(string, "IWAPT6,", 7) == 0) {
        send_string(conn, "IWBPT6,1,1#");
        return;
    }

    if (memcmp(string, "IWAP05", 6) == 0) {
        send_string(conn, "IWBP05,1#");
        return;
    }

    if (memcmp(string, "IWAPHT", 6) == 0) {
        thinkrace_process_heartrate(conn, str_count, data_buffers);
    }

    if (memcmp(string, "IWAPTP", 6) == 0) {
        thinkrace_process_temperature(conn, str_count, data_buffers);
    }

    if (memcmp(string, "IWAPSP", 6) == 0) {
        thinkrace_process_saturation(conn, str_count, data_buffers);
    }

    /*
     * IWAPWR,IMEI,<worn>,<timestamp># - 1 while the watch is on the body, 0 once it is taken
     * off. Nothing read it before. It matters for more than the record: the health recovery
     * in poll_health() restarts a watch that stops sending readings, and a watch on a table
     * stops sending readings for an entirely good reason. Knowing which it is means the
     * recovery can leave a removed watch alone instead of rebooting it in someone's drawer.
     */
    /*
     * IWAPBL,IMEI,<beacons>,<own MAC>,<timestamp># where the beacon list is
     * name|MAC|rssi separated by '&'. A scan of the bluetooth beacons around the watch.
     *
     * Recorded rather than positioned from: turning beacons into a location needs a table of
     * where the beacons are, which this server does not have. What it can do is say how many
     * were seen and which they were, which is what an indoor fix would be built from later.
     */
    if (memcmp(string, "IWAPBL", 6) == 0 && str_count > 2) {
        unsigned char * beacons[24] = {0};
        size_t count = split_to('&', data_buffers[2], strlen(data_buffers[2]) + 1, beacons, 24);
        log_line(conn, "bluetooth scan: %u beacon(s) around\n", (unsigned)count);

        for (size_t i = 0; i < count; i++) {
            log_line(conn, "   beacon: %s\n", beacons[i]);
        }

        write_stat(conn, "ble_beacons", count);
        send_string(conn, "IWBPBL#");
        return;
    }

    /*
     * IWAPTQ - the watch asking the server for local weather. Answering means integrating a
     * weather service and matching a response format the protocol document does not specify:
     * it says only that the terminal firmware has to be customised to display it. There is no
     * BPTQ parser in the firmware here to read the format off either, and the weather app it
     * would feed is not installed on this watch. So the request is recorded and left
     * unanswered, which the spec explicitly allows, rather than answered with a guess.
     */
    if (memcmp(string, "IWAPTQ", 6) == 0) {
        log_line(conn, "asked for weather - not answered, no weather source configured\n");
        return;
    }

    if (memcmp(string, "IWAPWR", 6) == 0 && str_count > 2) {
        //[0] is empty, [1] is the imei, [2] is the flag
        bool worn = parse_int(data_buffers[2], 1) != 0;
        write_stat(conn, "worn", worn ? 1 : 0);
        note_worn(conn, worn);
        log_line(conn, "wearing status: %s\n", worn ? "on the body" : "removed");
        log_event(conn, worn ? "watch put on" : "watch removed");
        return;
    }

    if (memcmp(string, "IWAPJK", 6) == 0 && str_count > 2) {
        thinkrace_process_stat(conn, str_count, data_buffers);
        //the protocol wants the type from the uploaded packet echoed back - "IWBPJK,2#"
        //for a heart rate. string[2] is the letter 'A' of "IWAPJK", so this replied
        //"IWBPJK,A#", and without the return below the generic responder then appended a
        //bare "IWBPJK#" as well. The device was being told twice, wrongly, that its health
        //packet had been received.
        snprintf(response, sizeof(response), "IWBPJK,%s#", data_buffers[2]);
        send_string(conn, response);

        return;
    }

    if (!isdigit(string[4]) || !isdigit(string[5])) {
        snprintf(response, sizeof(response), "IWBP%c%c#", string[4], string[5]);
        send_string(conn, response);
        return;
    }

    switch (message_type) {
        case 0:
            //data_buffers[0] is attacker sized; imei is 64 bytes
            snprintf(imei, sizeof(imei), "%s", data_buffers[0]);
            pad_imei(imei);
            memcpy(conn->imei, imei, strlen(imei) + 1);
            init_imei(conn);
            break;

        case 1:
            thinkrace_process_position(conn, str_count, data_buffers);
            break;

        case 3:
            thinkrace_process_heartbeat(conn, str_count, data_buffers);
            break;

        /*
         * IWAP04,<battery level>#. The device's own low battery alarm - it sends this when it
         * decides it is low, rather than the server working it out from a position packet.
         * It was acknowledged and thrown away, so the one packet the watch sends specifically
         * to say "I am nearly flat" did nothing at all.
         */
        case 4:
            //the split starts at the comma, so field 0 is empty and the value is field 1
            if (str_count > 1 && strlen(data_buffers[1])) {
                int level = parse_int(data_buffers[1], 3);
                write_stat(conn, "battery_level", level);
                log_line(conn, "low battery alarm: %d%%\n", level);

                /*
                 * Logged whatever the timer says. That timer starts at time(0) when a
                 * connection is made, so it suppresses the first ten minutes of every one -
                 * which for a watch that reconnects every quarter of an hour means an alarm
                 * could be swallowed indefinitely. The other protocols derive low battery
                 * from a level in a position packet, where rate limiting is what you want;
                 * this is the device saying so itself, deliberately and rarely, and it should
                 * not be dropped. The timer is still stamped so the derived check does not
                 * immediately log the same thing twice.
                 */
                conn->since_battalm = time(0);
                log_event(conn, "low battery");
            }

            break;

        //IWAP49,<heartrate>#. A pulse on its own, separate from the JK frame.
        case 49:
            if (str_count > 1 && strlen(data_buffers[1])) {
                int bpm = parse_int(data_buffers[1], 3);

                if (bpm > 0) {
                    write_stat(conn, "heartrate", bpm);
                    note_heartrate(conn, bpm);
                    note_health(conn);
                }
            }

            break;

        case 10:
            thinkrace_process_event(conn, str_count, data_buffers);
            break;

        /*
         * Responses to commands the server sent. These must not be answered: the default
         * branch below replies "IWBP<nn>#", and for a downlink id that is a command in its
         * own right - so acknowledging the terminal's acknowledgement re-issues the order.
         *
         * 46 was missing, and it is the take-picture command. The device answered AP46 to
         * say it was taking the photo, the server replied IWBP46# - a bare take-picture with
         * no parameters - and the device threw away the upload it had started and took a new
         * one. That is why every attempt announced a different number of packets (17, 24, 15)
         * and never got past the first: it was being told to start over each time.
         *
         * The rest are the other downlink ids the protocol defines an AP reply for, listed so
         * the same thing cannot happen again as commands are added.
         */
        case 12:    //SOS numbers
        case 14:    //white list
        case 15:    //location interval
        case 16:    //locate now
        case 17:    //factory reset
        case 18:    //restart
        case 31:    //shutdown
        case 33:    //working mode
        case 34:    //location working mode
        case 40:    //shortcut command
        case 42:    //picture upload, handled before the text path
        case 46:    //take picture
        case 50:    //detect heart rate
        case 51:    //phone book
        case 52:    //delete phone book
        case 84:    //whitelist switch
        case 86:    //health monitoring interval
        case 88:    //find the terminal
            return;
    }

    //send responses
    switch (message_type) {
        case 0:
            /*
             * Only the time sync. This also sent TIMES=0000@2359#, which became a malformed
             * BP34 - see the LOCMODE= handler above - on every single login. The location
             * interval is owned by BP15 through UPDATE= and by the adaptive tracking, so
             * there was nothing for it to set here even had it been well formed.
             */
            thinkrace_send_command(conn, "SYNCTIME#");
            break;

        case 1:
            send_string(conn, "IWBP01#");
//            thinkrace_send_command(conn, "HEARTRATE#");
            break;

        default:
            snprintf(response, sizeof(response), "IWBP%02d#", message_type);
            send_string(conn, response);
    }
}



/*
 * AP42 carries the image bytes raw, and the ordinary text path would destroy them: it
 * replaces every NUL with a space (a JPEG is full of them), then cuts the message at the
 * first '#' (0x23 occurs in image data constantly), then splits the result on commas
 * (0x2C likewise). So this packet is taken apart by length instead of by delimiter, before
 * any of that runs.
 *
 *   IWAP42,<yyyymmddhhmmss>,<total packets>,<packet no>,<length>,<length bytes>#
 *
 * Returns the number of bytes consumed from the front of recv_buffer, 0 for "not an image
 * packet at all", or IMAGE_INCOMPLETE for "an image packet whose tail has not arrived yet".
 * Those last two must not be confused: falling through to the text framing while a partial
 * image sits in the buffer lets rep() rewrite every NUL in it to a space, which quietly
 * corrupts the picture. Hex payloads survived that because they contain no NULs; raw ones
 * did not.
 */
#define IMAGE_INCOMPLETE ((size_t) -1)

static unsigned hexval(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return 0;
}


static size_t thinkrace_try_image_packet(connection * conn) {
    /*
     * The manual calls this packet AP42. The PT880 firmware does not: it sends the type as
     * "null", so the packet arrives as "IWnull,..." - the field was left empty and printed
     * through a %s. Both are accepted here, because the rest of the packet is exactly the
     * AP42 layout and refusing it on the strength of a firmware typo helps nobody.
     */
    //AP42 is the picture id, AP07 the voice one, and "IWnull" is what this firmware emits
    //when its id lookup comes back empty - which it does for at least the picture. All three
    //carry the identical five-field header, and what the payload actually is gets decided
    //from its own leading bytes when the transfer completes.
    static const char * const prefixes[] = { "IWAP42,", "IWnull,", "IWAP07," };
    size_t plen = 0;
    bool broken_id = false;

    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t l = strlen(prefixes[i]);

        if (conn->read_count >= l && memcmp(conn->recv_buffer, prefixes[i], l) == 0) {
            plen = l;
            broken_id = (i == 1);
            break;
        }
    }

    if (plen == 0) {
        return 0;
    }

    //the four header fields are ASCII digits; scanning past a sane header length would mean
    //walking into the image itself, so give up rather than search the whole buffer
    const size_t max_header = 80;
    size_t scan_to = min(conn->read_count, max_header);
    size_t field_start[5] = {0};
    size_t fields = 0;
    field_start[fields++] = plen;

    size_t data_start = 0;

    for (size_t i = plen; i < scan_to && fields <= 4; i++) {
        if (conn->recv_buffer[i] == ',') {
            if (fields == 4) {
                data_start = i + 1;
                break;
            }

            field_start[fields++] = i + 1;
        }
    }

    if (data_start == 0) {
        //header never completed inside a plausible length: this is not a packet we can use
        if (conn->read_count >= max_header) {
            log_line(conn, "  image: header did not complete, dropping buffer\n");
            conn->read_count = 0;
            return 0;
        }

        //the prefix matched, so this is an image packet whose header is still arriving
        return IMAGE_INCOMPLETE;
    }

    char devtime[16] = {0};
    size_t tlen = min(sizeof(devtime) - 1, field_start[1] - field_start[0] - 1);
    memcpy(devtime, conn->recv_buffer + field_start[0], tlen);
    size_t total   = (size_t)parse_int(conn->recv_buffer + field_start[1], 8);
    size_t packet  = (size_t)parse_int(conn->recv_buffer + field_start[2], 8);
    size_t datalen = (size_t)parse_int(conn->recv_buffer + field_start[3], 8);

    //a single packet is defined as 1024 bytes, the last one shorter - but the payload may
    //be hex, in which case the length counts hex characters and doubles. Anything past that
    //is a malformed header, and waiting for that many bytes would only stall the connection.
    if (datalen > 4096) {
        log_line(conn, "  image: implausible packet length %u, dropping buffer\n", (unsigned)datalen);
        conn->read_count = 0;
        image_discard(conn);
        return 0;
    }

    size_t needed = data_start + datalen + 1;   // + the closing '#'

    if (conn->read_count < needed) {
        return IMAGE_INCOMPLETE;                 // wait for the rest of it, untouched
    }

    if (conn->recv_buffer[needed - 1] != '#') {
        log_line(conn, "  image: AP42 packet %u not terminated where its length said, discarding\n",
                 (unsigned)packet);
        image_discard(conn);
        return needed;                           // still consume it, or it blocks the stream
    }

    /*
     * The manual describes the payload as the picture bytes themselves. This firmware sends
     * it hex encoded instead - a 1024 byte chunk arrives as 2048 characters of "ffd8ffe0..."
     * with the length field counting the characters, not the bytes. Decide from the data
     * rather than from either assumption: an even run of nothing but hex digits is decoded,
     * anything else is taken as the raw bytes the manual describes.
     */
    unsigned char decoded[2048];
    const unsigned char * payload = conn->recv_buffer + data_start;
    size_t paylen = datalen;
    bool looks_hex = (datalen >= 2) && ((datalen % 2) == 0) && (datalen / 2 <= sizeof(decoded));

    for (size_t i = 0; looks_hex && i < datalen; i++) {
        looks_hex = isxdigit((unsigned char)payload[i]) != 0;
    }

    if (looks_hex) {
        for (size_t i = 0; i < datalen; i += 2) {
            decoded[i / 2] = (unsigned char)((hexval(payload[i]) << 4) | hexval(payload[i + 1]));
        }

        payload = decoded;
        paylen = datalen / 2;
    }

    if (packet == 1) {
        image_begin(conn, devtime, total);
    }

    bool ok = image_append(conn, packet, payload, paylen);
    log_line(conn, "  image: packet %u/%u, %u bytes%s, %s\n", (unsigned)packet, (unsigned)total,
             (unsigned)paylen, looks_hex ? " (hex decoded)" : "", ok ? "accepted" : "rejected");

    //the device waits for this before sending the next packet, and repeats the current one
    //if it does not arrive or comes back as a failure
    /*
     * The manual's response is BP42. This firmware announced itself as "IWnull" - its packet
     * type came out of a %s as an empty string - and it sent one packet and then waited, so
     * it did not take BP42 as the acknowledgement it was waiting for. Its response matcher
     * is very likely broken in the same way its sender is, so when the upload arrived under
     * the broken name the acknowledgement is sent under both names. A device that wants only
     * one of them ignores the other: these packets are '#' delimited and an unrecognised id
     * is discarded, which is exactly what this firmware did to BP42.
     */
    char response[96] = {0};
    snprintf(response, sizeof(response), "IWBP42,%s,%u,%u,%u#", devtime,
             (unsigned)total, (unsigned)packet, ok ? 1u : 0u);
    send_string(conn, response);
    log_line(conn, "  image: replied %s\n", response);

    /*
     * BP42 is not what moves the upload along, whatever the manual says.
     *
     * From the watch's own firmware (L009_Protocol.odex, com.ic.protocols.protocol_beehome,
     * deodexed against the device framework): the picture is pushed through the voice-packet
     * sender, whose position is held in voicePacket.currentIndexToSent. Across the whole
     * beehome protocol that field is written from exactly one place - handleBP07. handleBP42
     * parses our reply, logs it, and never touches the index, which is why the device sat
     * after packet one having cheerfully acknowledged us at the TCP level.
     *
     * handleBP07 splits on ',', insists on exactly five fields, and advances only when
     *
     *     "1".equals(field[4]) && field[3] != field[2]
     *
     * with field[2] the total packet count, field[3] the packet just received - which it
     * then stores as the new index. So the acknowledgement it is actually waiting for is
     * the same five fields under BP07.
     *
     * BP42 is still sent first, because the manual documents it, the device does parse it,
     * and it costs one short message.
     */
    char advance[96] = {0};
    snprintf(advance, sizeof(advance), "IWBP07,%s,%u,%u,1#", devtime,
             (unsigned)total, (unsigned)packet);
    send_string(conn, advance);
    log_line(conn, "  image: advanced with %s\n", advance);
    (void)broken_id;

    if (ok && image_complete(conn)) {
        image_store(conn);
    }

    return needed;
}

void thinkrace_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

    //image packets are binary and must be handled before the text framing below mangles them
    size_t consumed = thinkrace_try_image_packet(conn);

    if (consumed == IMAGE_INCOMPLETE) {
        //leave the buffer exactly as it is; the text path below would mangle the bytes
        return;
    }

    if (consumed > 0) {
        memmove(conn->recv_buffer, conn->recv_buffer + consumed, conn->read_count - consumed);
        conn->read_count -= consumed;
        return;
    }

    //if we've at least got a header
    if (conn->read_count > 5) {
        rep(conn->recv_buffer, 0, ' ', conn->read_count); //remove all null characters up to read count
        size_t index = idx(conn->recv_buffer, '#');

        if (index > 0 && index < conn->read_count) {
            conn->recv_buffer[index] = 0;
            index++;
            thinkrace_process_message(conn, conn->recv_buffer, index);
            memmove(conn->recv_buffer, conn->recv_buffer + index, conn->read_count - index);
            conn->read_count -= index;
            return;
        }
    }
}

void thinkrace_warn(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    snprintf(buffer, sizeof(buffer), "MSG=%s", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void thinkrace_warn_audio(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    snprintf(buffer, sizeof(buffer), "MSG=%s", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void thinkrace_identify(void * vp) {
    connection * conn = (connection *)vp;
    const uint8_t thinkrace_start_contains[] = "IWAP";
    uint8_t first_bytes[13];
    memset(first_bytes, 0, sizeof(first_bytes));
    memcpy(first_bytes, conn->recv_buffer, 12);

    if (strstr(first_bytes, thinkrace_start_contains) != 0) {
        fprintf(stdout, "  device type is thinkrace\n");
        thinkrace_send_command(conn, "SYNCTIME#");
        conn->PROCESS_FUNCTION = thinkrace_process;
        conn->COMMAND_FUNCTION = thinkrace_send_command;
        //BP15 sets the location interval and BPXL asks for a health reading, so this
        //protocol can take part in adaptive tracking
        conn->supports_interval = true;
        //BP86 sets the watch's own heart rate and blood pressure periods; confirmed against
        //hardware, which answers IWAP86
        conn->supports_health_interval = true;
        conn->supports_health_poll = true;
        conn->WARNING_FUNCTION = thinkrace_warn;
        conn->AUDIO_WARNING_FUNCTION = thinkrace_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = thinkrace_warn;
        conn->timeout_time = time(0) + THINKRACE_TIMEOUT;
    }
}

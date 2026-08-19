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
#include "../tracking.h"

#define THINKRACE_TIMEOUT 1200

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
        sprintf(buffer, "%i#", tz);
        strcat(response, buffer);
        send_string(conn, response);

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

    } else if (strlen(cmd) > 6 && memcmp(cmd, "TIMES=", 6) == 0) {
        send_string(conn, "IWBP34,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,1,");
        send_string(conn, cmd + 7);

    } else if (strlen(cmd) > 6 && memcmp(cmd, "MSG=", 4) == 0) {
        send_string(conn, "IWBP40,");
        send_string(conn, conn->imei);
        send_string(conn, ",080835,");

        for (char * p = cmd + 4; *p != 0 && *p != '\n'; p++) {
            sprintf(buffer, "%02X", *p);
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

    //A position that could not be resolved still tells us what time the device thinks it
    //is. Only move_to() used to set conn->device_time, so once the fixes went void and
    //both the wifi and tower lookups failed the clock stopped at the last resolved fix -
    //and every later log line, event and stat was written at that one instant. Hours of
    //heart rate readings landed on a single timestamp and showed up as a stack of
    //duplicate looking points. Advance the clock from the packet itself, whether or not
    //we managed to place the device. Only ever forwards, so a stale fix replayed by a
    //second connection cannot drag it back.
    if (dt > conn->device_time) {
        conn->device_time = dt;
    }

    if (valid_position) {
        //if we're fairly certain about our location do trigger fences
        move_to(conn, dt, position_type, lat, lng);
        write_stat(conn, "battery_level", battery_level);
        write_sat_count(conn, position_type, num_sats);
        write_stat(conn, "signal", signal_strength);
        conn->timeout_time = time(0) + THINKRACE_TIMEOUT;
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
}


void thinkrace_process_temperature(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 3) {
        log_line(conn, "   invalid temperature package.\n");
        return;
    }

    write_stat(conn,  "temperature", parse_float(data_buffers[1]));
}




void thinkrace_process_saturation(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 2) {
        log_line(conn, "   invalid SPO2 package.\n");
        return;
    }

    write_stat(conn,  "SPO2", parse_float(data_buffers[1]));
}

void thinkrace_process_stat(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 4) {
        log_line(conn, "   invalid temperature package.\n");
        return;
    }

    int type = parse_int(data_buffers[2], 1);
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

        default:
            break;
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

    if (memcmp(string, "IWAPTQ", 6) == 0 || memcmp(string, "IWAPVR", 6) == 0 || memcmp(string, "IWAPXL", 6) == 0) {
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

    if (memcmp(string, "IWAPJK", 6) == 0 && str_count > 2) {
        thinkrace_process_stat(conn, str_count, data_buffers);
        //the protocol wants the type from the uploaded packet echoed back - "IWBPJK,2#"
        //for a heart rate. string[2] is the letter 'A' of "IWAPJK", so this replied
        //"IWBPJK,A#", and without the return below the generic responder then appended a
        //bare "IWBPJK#" as well. The device was being told twice, wrongly, that its health
        //packet had been received.
        sprintf(response, "IWBPJK,%s#", data_buffers[2]);
        send_string(conn, response);

        return;
    }

    if (!isdigit(string[4]) || !isdigit(string[5])) {
        sprintf(response, "IWBP%c%c#", string[4], string[5]);
        send_string(conn, response);
        return;
    }

    switch (message_type) {
        case 0:
            strcpy(imei, data_buffers[0]);
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

        case 10:
            thinkrace_process_event(conn, str_count, data_buffers);
            break;

        //responses to server commands
        case 15:
        case 16:
        case 17:
        case 18:
        case 31:
        case 33:
        case 34:
            return;
    }

    //send responses
    switch (message_type) {
        case 0:
            thinkrace_send_command(conn, "SYNCTIME#");
            thinkrace_send_command(conn, "TIMES=0000@2359#");
            break;

        case 1:
            send_string(conn, "IWBP01#");
//            thinkrace_send_command(conn, "HEARTRATE#");
            break;

        default:
            sprintf(response, "IWBP%02d#", message_type);
            send_string(conn, response);
    }
}



void thinkrace_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

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
    sprintf(buffer, "MSG=%s", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void thinkrace_warn_audio(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    sprintf(buffer, "MSG=%s", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void thinkrace_identify(void * vp) {
    connection * conn = (connection *)vp;
    const uint8_t thinkrace_start_contains[] = "IWAP";
    const uint8_t first_bytes[13];
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
        conn->supports_health_poll = true;
        conn->WARNING_FUNCTION = thinkrace_warn;
        conn->AUDIO_WARNING_FUNCTION = thinkrace_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = thinkrace_warn;
        conn->timeout_time = time(0) + THINKRACE_TIMEOUT;
    }
}

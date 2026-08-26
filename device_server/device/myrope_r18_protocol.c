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
#include "myrope_r18_protocol.h"

#define MYROPE_TIMEOUT 600

//most info from
// https://archive.ph/NO8KX
bool myrope_r18_send_command( void * c,  const char * command) {
    char * cmd = (char *)command;
    connection * conn = (connection *)c;
    char buffer[BUF_SIZE] = {0};
    size_t start = conn->send_count;
    char * imei = conn->imei + 1;

    if (strcmp(cmd, "HEARTRATE#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*000a*hrtstart,1]");

    } else  if (strcmp(cmd, "TEMPERATURE#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*0006*btemp2]");

    } else if (memcmp(cmd, "CENTER=", 7) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");

        if (cmd[strlen(cmd) - 1] == '#') {
            cmd[strlen(cmd) - 1] = 0;
        }

        snprintf(buffer, sizeof(buffer), "%04X*CENTER,", strlen(cmd));
        send_string(conn, buffer);
        send_string(conn, cmd + 7);
        send_string(conn, "]");

    } else if (memcmp(cmd, "SOS=", 4) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*SOS1,", strlen(cmd) + 1);
        send_string(conn, buffer);
        send_string(conn, cmd + 4);
        send_string(conn, "]");

    } else if (strcmp(cmd, "MONITOR#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*0007*MONITOR]");

    } else if (strcmp(cmd, "LOCATE#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*0002*CR]");

    } else if (strcmp(cmd, "FACTORYALL#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, conn->imei + 1);
        send_string(conn, "*0007*FACTORY]");

    } else if (strcmp(cmd, "REBOOT#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, conn->imei + 1);
        send_string(conn, "*0005*RESET]");

    } else if (strcmp(cmd, "SHUTDOWN#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, conn->imei + 1);
        send_string(conn, "*0008*POWEROFF]");

    } else if (strcmp(cmd, "FIND#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*0004*FIND]");

    } else if (strcmp(cmd, "EMERGENCY#") == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*0006*EMMODE]");

    } else if (strlen(cmd) > 7 && memcmp(cmd, "UPLOAD=", 7) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");

        if (cmd[strlen(cmd) - 1] == '#') {
            cmd[strlen(cmd) - 1] = 0;
        }

        snprintf(buffer, sizeof(buffer), "%04X*UPLOAD,", strlen(cmd));
        send_string(conn, buffer);
        send_string(conn, cmd + 7);
        send_string(conn, "]");

    } else if (strlen(cmd) > 7 && memcmp(cmd, "LOWBAT=", 7) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*LOWBAT,", strlen(cmd));
        send_string(conn, buffer);
        send_string(conn, cmd + 7);
        send_string(conn, "]");

    } else if (strlen(cmd) > 7 && memcmp(cmd, "OFFAL=", 7) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*OFFAL1,", strlen(cmd));
        send_string(conn, buffer);
        send_string(conn, cmd + 7);
        send_string(conn, "]");

    } else if (strlen(cmd) > 7 && memcmp(cmd, "SOSSMS=", 7) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*SOSSMS,", strlen(cmd));
        send_string(conn, buffer);
        send_string(conn, cmd + 7);
        send_string(conn, "]");

    } else if (strlen(cmd) > 7 && memcmp(cmd, "REMOVESMS=", 10) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*REMOVESMS,", strlen(cmd));
        send_string(conn, buffer);
        send_string(conn, cmd + 10);
        send_string(conn, "]");

    } else if (strlen(cmd) > 7 && memcmp(cmd, "REMOVE=", 7) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*REMOVE,", strlen(cmd));
        send_string(conn, buffer);
        send_string(conn, cmd + 7);
        send_string(conn, "]");

    } else if (strlen(cmd) > 6 && memcmp(cmd, "OWNER=", 6) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*DEVOWNER,", 11 + ((strlen(cmd) - 6) * 4));
        send_string(conn, buffer);

        for (char * p = cmd + 6; *p != 0 && *p != '\n'; p++) {
            snprintf(buffer, sizeof(buffer), "00%02X", *p);
            send_string(conn, buffer);
        }

        send_string(conn, ",,]");

    } else if (strlen(cmd) > 4 && memcmp(cmd, "MSG=", 4) == 0) {
        send_string(conn, "[3G*");
        send_string(conn, imei);
        send_string(conn, "*");
        snprintf(buffer, sizeof(buffer), "%04X*MESSAGE,", 8 + ((strlen(cmd) - 4) * 4));
        send_string(conn, buffer);

        for (char * p = cmd + 4; *p != 0 && *p != '\n'; p++) {
            snprintf(buffer, sizeof(buffer), "00%02X", *p);
            send_string(conn, buffer);
        }

        send_string(conn, "]");

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



void myrope_r18_process_position(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
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
    wifi_db_entry db_entry;
    unsigned char * current_network[3];

    //data_buffers[22] (cell id) is read below, so 22 fields is one short - at exactly 22
    //that slot is still null and strlen() walked into it.
    if (parse_count < 23) {
        log_line(conn, "   invalid location package.\n");
        return;
    }

    sscanf(data_buffers[1], "%2d%2d%2d", &day, &month, &year);
    year += 2000;
    sscanf(data_buffers[2], "%2d%2d%2d", &hour, &minute, &second);
    lat = parse_float(data_buffers[4]);
    lat = data_buffers[5][0] == 'S' ? -lat : lat;
    lng = parse_float(data_buffers[6]);
    lng = data_buffers[7][0] == 'W' ? -lng : lng;
    num_sats = parse_int(data_buffers[11], 3);
    signal_strength = parse_int(data_buffers[12], 3);
    battery_level = parse_int(data_buffers[13], 3);

    if (data_buffers[3][0] != 'V') {
        valid_position = true;
        position_type = 0;
    }

    /*
     * Where the wifi list starts, counted past the cell towers in front of it. Real traffic
     * from an R18 lays out as:
     *
     *   [16] 00000000   status
     *   [17] 1
     *   [18] 1          number of cell towers
     *   [19] 204  [20] 08  [21] 3270  [22] 1561888  [23] 100     one tower: mcc mnc lac cid rssi
     *   [24] 3          number of wifi networks
     *   [25] Armor_3W  [26] 16:83:25:7d:e0:79  ...
     *
     * so the towers are five fields each starting at 19. The old "18 + 6 * count" happens to
     * give the same 24 for a single tower, which is all this device has ever reported here,
     * and is wrong by one more field for every tower after that - it would have read the
     * wifi count out of the middle of the last tower.
     *
     * The count is one device-supplied digit, so this still has to be treated as arbitrary:
     * it lands anywhere up to 64, well past the 40 pointers that exist, and the bounds check
     * below is what keeps that from being read.
     */
    size_t wifi_offs = 19 + (5 * (size_t)parse_int(data_buffers[18], 1));

    if (wifi_offs >= parse_count) {
        log_line(conn, "   wifi offset %u past the end of the message, ignoring wifi.\n", wifi_offs);
        db_entry.network_count = 0;

    } else {
        db_entry.network_count = parse_int(data_buffers[wifi_offs], 2);
    }

    //network_buffer holds WIFI_LOOKUP_MAX entries but the count is two device supplied
    //digits, so it can claim up to 99 networks and the copy below would run off the end
    //of db_entry entirely.
    if (db_entry.network_count > WIFI_LOOKUP_MAX) {
        db_entry.network_count = WIFI_LOOKUP_MAX;
    }

    //every network reads three more fields, so stop at whatever the message actually holds
    while (db_entry.network_count > 0
            && (wifi_offs + 2 + ((db_entry.network_count - 1) * 3)) >= parse_count) {
        db_entry.network_count--;
    }

    if ( db_entry.network_count > 2) {
        for (int i = 0; i <  db_entry.network_count; i++) {
            int values[6];
            sscanf( data_buffers[wifi_offs + 2 + (i * 3)], "%x:%x:%x:%x:%x:%x",
                    &values[0], &values[1], &values[2],
                    &values[3], &values[4], &values[5] );
            db_entry.network_buffer[i].mac_addr[0] =  values[0];
            db_entry.network_buffer[i].mac_addr[1] = values[1];
            db_entry.network_buffer[i].mac_addr[2] = values[2];
            db_entry.network_buffer[i].mac_addr[3] = values[3];
            db_entry.network_buffer[i].mac_addr[4] = values[4];
            db_entry.network_buffer[i].mac_addr[5] = values[5];
        }

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

    if (!valid_position) {
        //when falling back to LBS try to force GPS geolocation
        if ((time(0) - conn->since_last_locate ) > 60) {
            conn->since_last_locate = time(0);
        }

        cell_tower tower;
        tower.mcc = parse_int(data_buffers[19], strlen(data_buffers[19]));
        tower.mnc = parse_int(data_buffers[20], strlen(data_buffers[20]));
        tower.lac = parse_int(data_buffers[21], strlen(data_buffers[21]));
        tower.cell_id = parse_int(data_buffers[22], strlen(data_buffers[22]));
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
        //if we're fairly certain about our location do trigger fences
        move_to(conn, dt, position_type, lat, lng);
        set_status(conn, battery_level, signal_strength, position_type, num_sats);
        write_stat(conn, "battery_level", battery_level);
        write_sat_count(conn, position_type, num_sats);
        write_stat(conn, "signal", signal_strength);
        conn->timeout_time = time(0) + MYROPE_TIMEOUT;
    }
}


void myrope_r18_process_event(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 16) {
        log_line(conn, "   invalid heartbeat package.\n");
        return;
    }

    unsigned int data = 0;
    char * name = "unknown";
    sscanf(data_buffers[16], "%x", &data);

    if ((data >> 16) & 1) {
        name = "SOS";
    }

    if ((data >> 17) & 1) {
        name = "Low battery";
    }

    if ((data >> 30) & 1) {
        name = "Remove watch";
    }

    log_event(conn, name);
}


void myrope_r18_process_heartbeat(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 3) {
        log_line(conn, "   invalid heartbeat package.\n");
        return;
    }

    log_line(conn, "number of steps: %s\n", data_buffers[1]);
}


void myrope_r18_process_message(connection * conn, char * string, size_t length) {
    time_t now = time(NULL);
    struct tm * t = gmtime(&now); //gmtime for gmt
    unsigned char bufstr[BUF_SIZE] = {0};
    unsigned char response[BUF_SIZE] = {0};
    unsigned char * data_buffers[40] = {0};
    unsigned char * first_message_part[16] = {0};
    unsigned char imei[64] = {0};
    string = strip_whitespace(string);
    log_line(conn, "got message: %s\n", string);
    memset(imei, 0, 64);
    memset(bufstr, 0, BUF_SIZE);
    memset(response, 0, BUF_SIZE);
    memset(data_buffers, 0, sizeof(data_buffers));
    memcpy(bufstr, string, min(length, BUF_SIZE - 1));
    size_t str_count = split_to(',', bufstr, BUF_SIZE, data_buffers, 40);
    log_time(conn);
    logprintf(conn, "split message: ");

    for (size_t i = 0; i < str_count; i++) {
        logprintf(conn, " [%u] %s", i, data_buffers[i]);
    }

    logprintf(conn, "\n");

    /*
     * This demanded a comma. The structure that actually matters is the '*' split checked
     * just below, and a whole class of message carries no comma at all - the bare
     * acknowledgements FIND, SOS1, CENTER and ppsync - so every one of them was rejected
     * here before reaching the dispatch below. The handlers that need several comma fields
     * check the count themselves.
     */
    if (str_count < 1) {
        log_line(conn, "empty message.\n", string);
        return;
    }

    size_t initial_count = split_to('*', data_buffers[0], BUF_SIZE, first_message_part, 16);

    //first_message_part[3] carries the message type and is read directly below, so three
    //fields is not enough - a header split into exactly three left that pointer null and
    //memcmp dereferenced it. A device only has to send "[3G*x*y]" to crash the server.
    if (initial_count < 4) {
        log_line(conn, "invalid message header size.\n", string);
        return;
    }

    //bounded: the field is attacker sized and imei is 64 bytes
    snprintf(imei, sizeof(imei), "%s", first_message_part[1]);

//keepalive message
    if (memcmp(first_message_part[3], "LK", 2) == 0) {
        memcpy(conn->imei, imei, strlen(imei) + 1);
        pad_imei(conn->imei);
        init_imei(conn);
        snprintf(response, sizeof(response), "[3g*%s*0002*LK]", imei);
        send_string(conn, response);
        conn->timeout_time = time(0) + MYROPE_TIMEOUT;
        myrope_r18_process_heartbeat(conn, str_count, data_buffers);
        return;
    }

    if (memcmp(first_message_part[3], "ICCID", 5) == 0) {
        snprintf(response, sizeof(response), "[3g*%s*0005*ICCID]", imei);
        send_string(conn, response);
        conn->timeout_time = time(0) + MYROPE_TIMEOUT;
        return;
    }

    if (memcmp(first_message_part[3], "AL", 2) == 0) {
        myrope_r18_process_event(conn, str_count, data_buffers);
        snprintf(response, sizeof(response), "[3g*%s*0002*AL]", imei);
        send_string(conn, response);
        return;
    }

    //this compared two bytes of a fifteen byte name, so every message beginning "DE" was
    //answered as if it were DEVICEFUNCCOUNT
    if (strlen(first_message_part[3]) >= 15 && memcmp(first_message_part[3], "DEVICEFUNCCOUNT", 15) == 0) {
        snprintf(response, sizeof(response), "[3g*%s*000f*DEVICEFUNCCOUNT]", imei);
        send_string(conn, response);
        return;
    }

    if (memcmp(first_message_part[3], "UD", 2) == 0) {
        myrope_r18_process_position(conn, str_count, data_buffers);
        return;
    }

    /*
     * Everything else used to fall off the end of this chain without a word. A watch of this
     * family acknowledges a command by sending its name back - FIND, CENTER, SOS1 - so those
     * acknowledgements were being discarded and a command sent to one of these devices
     * looked as though it had gone unanswered. Record it as the command reply it is, and
     * leave a line in the log naming anything genuinely unrecognised.
     */
    log_line(conn, "unhandled message: %s\n", first_message_part[3]);
    log_command_response(conn, first_message_part[3]);
}



void myrope_r18_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

    //if we've at least got a header
    if (conn->read_count > 5) {
        rep(conn->recv_buffer, 0, ' ', conn->read_count); //remove all null characters up to read count
        size_t index = idx(conn->recv_buffer, ']');

        if (index > 0 && index < conn->read_count) {
            conn->recv_buffer[index] = 0;
            index++;
            myrope_r18_process_message(conn, conn->recv_buffer, index);
            memmove(conn->recv_buffer, conn->recv_buffer + index, conn->read_count - index);
            conn->read_count -= index;
            return;
        }
    }
}

void myrope_r18_warn(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    snprintf(buffer, sizeof(buffer), "FIND#", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void myrope_r18_warn_audio(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    snprintf(buffer, sizeof(buffer), "FIND#", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void myrope_r18_identify(void * vp) {
    connection * conn = (connection *)vp;
    uint8_t first_bytes[13];
    memset(first_bytes, 0, sizeof(first_bytes));
    memcpy(first_bytes, conn->recv_buffer, 12);

    if (first_bytes[0] == '[' && (first_bytes[1] == '2' || first_bytes[1] == '3' || first_bytes[1] == '4') && first_bytes[2] == 'G' && first_bytes[3] == '*' ) {
        fprintf(stdout, "  device type is myrope r18\n");
        conn->PROCESS_FUNCTION = myrope_r18_process;
        conn->COMMAND_FUNCTION = myrope_r18_send_command;
        //the device sends hrtstart back as a bare echo and uploads no reading with it, so
        //there is nothing to feed note_health(). Advertising the poll only had the server
        //firing an unanswerable HEARTRATE# every interval, so leave it off until a real
        //health upload is understood for this protocol.
        conn->supports_health_poll = false;
        conn->WARNING_FUNCTION = myrope_r18_warn;
        conn->AUDIO_WARNING_FUNCTION = myrope_r18_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = myrope_r18_warn;
        conn->timeout_time = time(0) + MYROPE_TIMEOUT;
    }
}

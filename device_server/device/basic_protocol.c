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
#include "basic_protocol.h"

#define BASIC_TIMEOUT 1200

bool basic_send_command( void * c, const char * cmd) {
    connection * conn = (connection *)c;
    size_t start = conn->send_count;
    send_string(conn, cmd);
    send_string(conn, "\n");
    log_line(conn, "sent command: ");

    for (int i = start; i < conn->send_count; i++) {
        logprintf(conn, "%c", ((unsigned char *)conn->send_buffer)[i]);
    }

    return true;
}

char * unescape_str(unsigned char * str) {
    for (int i = 0; str[i] != 0; i++) {
        if (str[i] == 255) {
            str[i] = ';';
        }
    }

    return str;
}

void basic_process_message(connection * conn, char * string, size_t length) {
    unsigned char bufstr[BUF_SIZE];
    unsigned char * data_buffers[40];
    unsigned char imei[18] = {0};
    unsigned char * wifi_split[16];
    unsigned char * coord_split[2];
    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);
    unsigned int num_sats = 0;
    unsigned int position_type = 0;
    float lat = 0;
    float lon = 0;
    double speed = 0;
    double over_time;
    wifi_db_entry db_entry;
    unsigned char * current_network[3];
    memset(imei, 0, sizeof(imei));
    memset(bufstr, 0, sizeof(bufstr));
    memset(data_buffers, 0, sizeof(data_buffers));
    string = strip_whitespace(string);
    memcpy(bufstr, string, min(strlen(string), sizeof(bufstr) - 1));
    log_line(conn, "message recieved: %s\n", bufstr);
    size_t str_count = split_to(';', bufstr, BUF_SIZE, data_buffers, 40);
    conn->timeout_time = time(0) + BASIC_TIMEOUT;
    log_line(conn, "  parsed message: ");

    for (size_t i = 0; i < str_count; i++) {
        logprintf(conn, "  [%u]: %s ", i, data_buffers[i]);
    }

    logprintf(conn, "\n");

    if ( str_count < 3) {
        log_line(conn, "  invalid response length.\n");
        return;
    }

    if (strcmp(data_buffers[0], "CMDRESULT") == 0) {
        log_command_response(conn, unescape_str(data_buffers[1]));
        return;
    }

    if ( str_count < 5) {
        log_line(conn, "  invalid location response length.\n");
        return;
    }

    memcpy(imei, data_buffers[1], min(strlen(data_buffers[1]), 16));
    pad_imei(imei);

    if (strlen(conn->imei) <= 1 || strcmp(conn->imei, imei) != 0) {
        memcpy(conn->imei, imei, strlen(imei) + 1);
        init_imei(conn);
    }

    if ( strlen(data_buffers[3]) > 4 ) {
        unsigned int coord_count = split_to(',', data_buffers[3], strlen(data_buffers[3]) + 1, coord_split, 2);

        if ( coord_count == 2 ) {
            lat = parse_float(coord_split[0]);
            lon = parse_float(coord_split[1]);
            num_sats = 1;

        } else {
            log_line(conn, "  missing lat or longitude.\n");
            num_sats = 0;
        }
    }

    unsigned int battery_level = parse_int( data_buffers[2], 3);

    if (battery_level < 20 && (( time(0) - conn->since_battalm) > 600)) {
        log_event(conn, lat, lon, 0, "low battery");
    }

    if ( strlen(data_buffers[4]) > 1 ) {
        db_entry.network_count = split_to('|', data_buffers[4], strlen(data_buffers[4]) + 1, wifi_split, 16);

        if ( db_entry.network_count > 2) {
            for (int i = 0; i <  db_entry.network_count; i++) {
                int values[6];
                sscanf( wifi_split[i], "%2x:%2x:%2x:%2x:%2x:%2x",
                        &values[0], &values[1], &values[2],
                        &values[3], &values[4], &values[5] );
                db_entry.network_buffer[i].mac_addr[0] =  values[0];
                db_entry.network_buffer[i].mac_addr[1] = values[1];
                db_entry.network_buffer[i].mac_addr[2] = values[2];
                db_entry.network_buffer[i].mac_addr[3] = values[3];
                db_entry.network_buffer[i].mac_addr[4] = values[4];
                db_entry.network_buffer[i].mac_addr[5] = values[5];
            }

            db_entry.result =  wifi_lookup(db_entry.network_buffer,  db_entry.network_count, conn->since_last_position, conn->current_lat, conn->current_lon);

            if (db_entry.result.valid) {
                position_type = 2;
                lat = db_entry.result.lat;
                lon = db_entry.result.lng;
                num_sats =  db_entry.network_count;

            } else {
                db_entry.result.lat = lat;
                db_entry.result.lng = lon;
                db_entry.result.last_tried = time(0);
                db_entry.result.valid = true;
                db_entry.result.radius = 10;
                db_entry.result = wifi_to_cache(db_entry);
            }
        }
    }

    if (num_sats > 0) {
        speed = haversineDistance(conn->current_lat, conn->current_lon, lat, lon);
        over_time = ((time(0) - conn->since_last_position) / 3600);
        over_time = over_time <= 0 ? 0.166666f : over_time;
        speed = speed / over_time;
        move_to(conn, true, lat, lon, speed);
        gpsprintf(conn,
                  "%u-%02u-%02uT%02u:%02u:%02uZ,%f,%f,%u,%u,%u,0,0\n",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  lat,
                  lon,
                  (int)speed,
                  position_type,
                  num_sats);
        statsprintf(conn,
                    "%u-%02u-%02uT%02u:%02u:%02uZ,%s,%.2f\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                    "speed",
                    speed);
        statusprintf(conn, "%u,%u,%u,%u\n",
                     battery_level,
                     0,//gsm signal level
                     position_type,
                     num_sats);//number of satelites
        statsprintf(conn,
                    "%u-%02u-%02uT%02u:%02u:%02uZ,%s,%u\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                    "battery_level",
                    battery_level);
    }
}

void basic_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

    //if we've at least got a header
    if (conn->read_count > 5) {
        rep(conn->recv_buffer, 0, ' ', conn->read_count); //remove all null characters up to read count
        size_t index = idx(conn->recv_buffer, '!');

        if (index > 0 && index < conn->read_count) {
            index++;
            basic_process_message(conn, conn->recv_buffer, index);
            memmove(conn->recv_buffer, conn->recv_buffer + index, conn->read_count - index);
            conn->read_count -= index;
            return;
        }
    }

    //if we're idle, and 5 minutes have passed we should get status from our device
    if (conn->read_count == 0 && ( time(0) - conn->since_last_status ) > 300) {
        conn->since_last_status = time(0);
    }
}

void basic_warn(void * vp, const char * reason ) {
}
void basic_warn_audio(void * vp, const char * reason) {
}

void basic_identify(void * vp) {
    connection * conn = (connection *)vp;
    const uint8_t basic_start_contains[] = "BASIC;";
    const uint8_t first_bytes[8];
    memset(first_bytes, 0, sizeof(first_bytes));
    memcpy(first_bytes, conn->recv_buffer, 7);

    if (strstr(first_bytes, basic_start_contains) != 0) {
        fprintf(stdout, "  device type is phone\n");
        conn->PROCESS_FUNCTION = basic_process;
        conn->COMMAND_FUNCTION = basic_send_command;
        conn->WARNING_FUNCTION = basic_warn;
        conn->AUDIO_WARNING_FUNCTION = basic_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = basic_warn;
        conn->timeout_time = time(0) + BASIC_TIMEOUT;
    }
}

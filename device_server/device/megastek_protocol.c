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
#include "megastek_protocol.h"

#define MTEK_TIMEOUT 600

bool megastek_send_command( void * c, const char * cmd) {
    connection * conn = (connection *)c;
    size_t start = conn->send_count;
    send_string(conn, "$GPRS,");
    send_string(conn, conn->imei + (*conn->imei == '0' ? 1 : 0));
    send_string(conn, ";");
    send_string(conn, cmd);
    send_string(conn, ";!");
    log_line(conn, "sent command: ");

    for (int i = start; i < conn->send_count; i++) {
        logprintf(conn, "%c", ((unsigned char *)conn->send_buffer)[i]);
    }

    logprintf(conn, "\n");
    return true;
}


float parseLat(char * str) {
    return parse_int(str, 2) + parse_float(str + 2) / 60.0f;
}

float parseLong(char * str) {
    return parse_int(str, 3) + parse_float(str + 3) / 60.0f;
}

void process_message(connection * conn, char * string, size_t length) {
    unsigned char bufstr[BUF_SIZE];
    unsigned char * data_buffers[40];
    unsigned char imei[64] = {0};
    unsigned char * wifi_split[16];
    wifi_db_entry db_entry;
    unsigned char * current_network[3];
    string = strip_whitespace(string);
    memset(imei, 0, 64);
    memset(bufstr, 0, sizeof(bufstr));
    memset(data_buffers, 0, sizeof(data_buffers));
    memcpy(bufstr, string, min(strlen(string), BUF_SIZE - 1));
    log_line(conn, "message recieved: %s\n", bufstr);
    size_t str_count = split_to(',', bufstr, BUF_SIZE, data_buffers, 40);
    conn->timeout_time = time(0) + MTEK_TIMEOUT;

    if (strstr(data_buffers[0], "CMV001") || strstr(data_buffers[0], ";")) {
        memcpy(bufstr, conn->recv_buffer, min(length, BUF_SIZE - 1));
        log_command_response(conn, bufstr);
        return;
    }

    log_line(conn, "  parsed message: ");

    for (size_t i = 0; i < str_count; i++) {
        logprintf(conn, "  [%u]: %s ", i, data_buffers[i]);
    }

    logprintf(conn, "\n");

    if ( str_count < 35) {
        log_line(conn, "  invalid response length.\n");
        return;
    }

    memcpy(imei, data_buffers[1], min(strlen(data_buffers[1]), 63));
    pad_imei(imei);

    if (strlen(conn->imei) <= 1 || strcmp(conn->imei, imei) != 0) {
        memcpy(conn->imei, imei, strlen(imei) + 1);
        init_imei(conn);
        //default wifi on
        megastek_send_command(conn, "W040,0");
        megastek_send_command(conn, "W039,1");
    }

    if ( strlen(data_buffers[7]) < 5 || strlen(data_buffers[9]) < 6 ) {
        log_line(conn, "  invalid lat or long coordinates.\n");
        return;
    }

    float lat = parseLat(data_buffers[7]);
    float lon = parseLong(data_buffers[9]);

    if (data_buffers[8][0] != 'N') {
        lon *= -1;
    }

    if (data_buffers[10][0] != 'E') {
        lat *= -1;
    }

    unsigned char * dtstr = data_buffers[4];
    unsigned char * timestr = data_buffers[5];
    float spd = parse_float(data_buffers[15]) * 1.852f;

    if ( strlen(dtstr) < 6 || strlen(timestr) < 6) {
        log_line(conn, "  invalid date or time.\n");
        return;
    }

    unsigned int battery_level = parse_int( data_buffers[33], 3);

    if (battery_level < 20 && (( time(0) - conn->since_battalm) > 600)) {
        conn->since_battalm = time(0);
        conn->WARNING_FUNCTION(conn, "low battery");
        log_event(conn,  "low battery");
    }

    uint8_t year = parse_int( dtstr + 4, 2);
    uint8_t month = parse_int( dtstr + 2, 2);
    uint8_t day = parse_int( dtstr, 2);
    uint8_t hour = parse_int( timestr, 2);
    uint8_t min = parse_int( timestr + 2, 2);
    uint8_t sec =  parse_int( timestr + 4, 2);
    time_t dt = date_to_time(year, month, day, hour, min, sec);
    size_t position_type = 0;
    size_t num_sats = parse_int(data_buffers[12], 2);

    if ( str_count > 35 && strlen(data_buffers[35]) > 1 ) {
        db_entry.network_count = split_to('|', data_buffers[35], strlen(data_buffers[35]) + 1, wifi_split, 16);

        if ( db_entry.network_count > 2) {
            for (int i = 0; i <  db_entry.network_count; i++) {
                int values[6];
                sscanf( wifi_split[i], "%2x%2x%2x%2x%2x%2x",
                        &values[0], &values[1], &values[2],
                        &values[3], &values[4], &values[5] );
                db_entry.network_buffer[i].mac_addr[0] =  values[0];
                db_entry.network_buffer[i].mac_addr[1] = values[1];
                db_entry.network_buffer[i].mac_addr[2] = values[2];
                db_entry.network_buffer[i].mac_addr[3] = values[3];
                db_entry.network_buffer[i].mac_addr[4] = values[4];
                db_entry.network_buffer[i].mac_addr[5] = values[5];
            }

            db_entry.result =  wifi_lookup(db_entry.network_buffer,  db_entry.network_count);

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

    if (strcmp(data_buffers[34], "Timer") == 0) {
        move_to(conn, dt, position_type, lat, lon);
        write_sat_count(conn, position_type, num_sats);

    } else {
        log_event(conn, data_buffers[34]);
    }

    int rssi = parse_int(data_buffers[23], 2) * 3.33f;
    set_status(conn, parse_float( data_buffers[33]), rssi, 0, num_sats);
    write_stat(conn, "battery_level", battery_level);
    write_stat(conn, "signal", rssi);
}

void megastek_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

    //if we've at least got a header
    if (conn->read_count > 5) {
        rep(conn->recv_buffer, 0, ' ', conn->read_count); //remove all null characters up to read count
        size_t index = idx(conn->recv_buffer, '!');

        if (index > 0 && index < conn->read_count) {
            index++;
            process_message(conn, conn->recv_buffer, index);
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

void megastek_warn(void * vp, const char * reason ) {
    ((connection *)vp)->COMMAND_FUNCTION(vp, "W036,10");
}

void megastek_warn_audio(void * vp, const char * reason) {
    ((connection *)vp)->COMMAND_FUNCTION(vp, "W043,1,1,1");
}

void megastek_identify(void * vp) {
    connection * conn = (connection *)vp;
    const uint8_t megastek_start_contains[] = "$MGV0";
    const uint8_t first_bytes[13];
    memset(first_bytes, 0, sizeof(first_bytes));
    memcpy(first_bytes, conn->recv_buffer, 12);

    if (strstr(first_bytes, megastek_start_contains) != 0) {
        // MEGASTEK device
        fprintf(stdout, "  device type is megastek\n");
        conn->PROCESS_FUNCTION = megastek_process;
        conn->COMMAND_FUNCTION = megastek_send_command;
        conn->WARNING_FUNCTION = megastek_warn;
        conn->AUDIO_WARNING_FUNCTION = megastek_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = megastek_warn;
        conn->timeout_time = time(0) + MTEK_TIMEOUT;
    }
}

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
#include "myrope_protocol.h"

#define MYROPE_TIMEOUT 600


bool myrope_send_string( void * c,  char * cmd) {
    char buffer[BUF_SIZE] = {0};
    connection * conn = (connection *)c;
    unsigned char cx = 0;

    for (size_t i = 0; i < strlen(cmd); i++) {
        cx ^= cmd[i];
    }

    sprintf(buffer, "%s,%x,\r\n", cmd, cx);
    send_string(conn, buffer);
}

bool myrope_send_command( void * c,  const char * command ) {
    char * cmd = (char *)command;
    connection * conn = (connection *)c;
    char buffer[BUF_SIZE] = {0};
    size_t start = conn->send_count;

    if (strlen(cmd) > 7 && memcmp(cmd, "UPDATE=", 7) == 0) {
        if (cmd[strlen(cmd) - 1] == '#') {
            cmd[strlen(cmd) - 1] = 0;
        }

        sprintf(buffer, "$HX,1002,%s,%s,#", conn->imei, cmd + 7);
        myrope_send_string(c, buffer);
        unsigned int interval = atoi(cmd + 7) / 60;
        interval += interval == 0;
        sprintf(buffer, "$HX,1014,%s,0/0/1/%u,#", conn->imei, interval);
        myrope_send_string(c, buffer);

    } else if (strlen(cmd) > 6 && memcmp(cmd, "MSG=", 4) == 0) {
        char hex_buffer[BUF_SIZE] = {0};

        if (cmd[strlen(cmd) - 1] == '#') {
            cmd[strlen(cmd) - 1] = 0;
        }

        for (char * p = cmd + 4; *p != 0 && *p != '\n'; p++) {
            char tmp[10] = {0};
            sprintf(tmp, "%2X00", *p);
            strcat(hex_buffer, tmp);
        }

        sprintf(buffer, "$HX,1011,%s,%s,#", conn->imei, hex_buffer);
        myrope_send_string(c, buffer);

    } else if (strlen(cmd) >= 5 && memcmp(cmd, "ALM=", 4) == 0) {
        if (cmd[strlen(cmd) - 1] == '#') {
            cmd[strlen(cmd) - 1] = 0;
        }

        sprintf(buffer, "$HX,1010,%s,0/%s,#", conn->imei, cmd + 4);
        myrope_send_string(c, buffer);

    } else {
        myrope_send_string(c, cmd);
    }

    log_line(conn, "sent command: ");

    for (int i = start; i < conn->send_count; i++) {
        unsigned char c = ((unsigned char *)conn->send_buffer)[i];

        if (isprint(c)) {
            logprintf(conn, "%c", c);

        } else {
            logprintf(conn, "\\0x%x", c);
        }
    }

    logprintf(conn, "\n");
    return true;
}



void myrope_process_position(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
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
    int num_sats = 0;
    int working_mode;
    int fortification_state;
    //program data
    bool valid_position = false;
    size_t position_type = 0;
    unsigned char * wifi_split[16];
    unsigned char * gps_split[16];
    unsigned char * cell_split[128];
    wifi_db_entry db_entry;
    cell_tower tower;
    unsigned char * current_network[3];

    if (parse_count <= 10) {
        log_line(conn, "   invalid location package.\n");
        return;
    }

    sscanf(data_buffers[4], "%2d%2d%2d", &year, &month, &day);
    sscanf(data_buffers[5], "%2d%2d%2d", &hour, &minute, &second);

    if (data_buffers[3][0] == 'S') {
        size_t loc_count = split_to('/', data_buffers[6], strlen(data_buffers[6]) + 1, gps_split, 16);
        sscanf(gps_split[2], " %2f%9f", &lat, &latdeg );
        sscanf(gps_split[4], " %3f%9f", &lng, &lngdeg );
        lat += latdeg / 60;
        lng += lngdeg / 60;
        lng = gps_split[3][0] == 'S' ?  -lng : lng;
        lat = gps_split[5][0] == 'W' ? -lat : lat;
        speed = parse_float(gps_split[6]);
        valid_position = true;
        num_sats = 4;

    } else {
        db_entry.network_count = split_to('/', data_buffers[9], strlen(data_buffers[9]) + 1, wifi_split, 16) / 2;

        if ( db_entry.network_count > 2) {
            for (int i = 0; i <  db_entry.network_count; i++) {
                int values[6];
                sscanf( wifi_split[i * 2], "%2x%2x%2x%2x%2x%2x",
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
                valid_position = true;
                position_type = 2;
                lat = db_entry.result.lat;
                lng = db_entry.result.lng;
                num_sats =  db_entry.network_count;
            }
        }
    }

    time_t dt = date_to_time(year, month, day, hour, minute, second);

    if (!valid_position) {
        //when falling back to LBS try to force GPS geolocation
        if ((time(0) - conn->since_last_locate ) > 60) {
            conn->since_last_locate = time(0);
            //   myrope_send_command(conn, "LOCATE#");
        }

        if (strlen(data_buffers[8]) > 0) {
            size_t tower_count = split_to('/', data_buffers[8], strlen(data_buffers[8]) + 1, cell_split, 128) / 5;
            size_t point_count = 0;
            multilaterate_point points[10];

            for (size_t i = 0; i < tower_count; i++) {
                size_t tower_idx = i * 5;
                tower.mcc = atoi(cell_split[tower_idx]);
                tower.mnc = atoi(cell_split[tower_idx + 1]);
                tower.lac = atoi(cell_split[tower_idx + 2]);
                tower.cell_id = atoi(cell_split[tower_idx + 3]);
                log_line(conn, "got tower mcc: %u mnc: %u cid: %u lac: %u rssi:%s\n", tower.mcc, tower.mnc, tower.cell_id, tower.lac, cell_split[tower_idx + 4]);
                tower.location = lbs_lookup(&tower, conn->current_lat, conn->current_lon);
                log_line(conn, "got tower location %f %f\n", tower.location.lat, tower.location.lng);

                if (tower.location.valid) {
                    points[point_count].lat = tower.location.lat;
                    points[point_count].lng = tower.location.lng;
                    points[point_count].strength = atoi(cell_split[tower_idx + 4]) * 25;
                    point_count++;
                }
            }

            if (point_count > 0) {
                multilaterate_point result = multilaterate(points, point_count);
                position_type = 1;
                lat = result.lat;
                lng = result.lng;
                valid_position = true;
                num_sats = point_count;
            }
        }
    }

    if (valid_position) {
        move_to(conn, dt, position_type, lat, lng);
        write_sat_count(conn, position_type, num_sats);

        if (strlen(data_buffers[10]) > 0) {
            char * alm = "unknown";

            if (strcmp(data_buffers[10], "A11") == 0) {
                alm = "device removed";
            }

            if (strcmp(data_buffers[10], "A8") == 0) {
                alm = "shutdown";
            }

            if (strcmp(data_buffers[10], "A14") == 0) {
                alm = "low battery";
            }

            log_event(conn, alm);
        }
    }

    conn->timeout_time = time(0) + MYROPE_TIMEOUT;
}



void myrope_process_heartbeat(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 3) {
        log_line(conn, "   invalid heartbeat package.\n");
        return;
    }

    set_status(conn,
               parse_float(data_buffers[4] + 3),
               parse_float(data_buffers[5] + 3),
               0,
               0);
    write_stat(conn, "battery_level", parse_float(data_buffers[4] + 3));
    write_stat(conn, "signal", parse_float(data_buffers[5] + 3));
    conn->timeout_time = time(0) + MYROPE_TIMEOUT;
}


void myrope_process_temperature(connection * conn, size_t parse_count, unsigned char * data_buffers[40]) {
    if (parse_count < 3) {
        log_line(conn, "   invalid temperature package.\n");
        return;
    }

    float temp = parse_float(data_buffers[7]);

    if (temp > 97) {
        return;
    }

    write_stat(conn, "temperature", temp);
    conn->timeout_time = time(0) + MYROPE_TIMEOUT;
}



void myrope_process_message(connection * conn, char * string, size_t length) {
    time_t now = time(NULL);
    struct tm * t = gmtime(&now); //gmtime for gmt
    unsigned char bufstr[BUF_SIZE] = {0};
    unsigned char response[BUF_SIZE] = {0};
    unsigned char * data_buffers[40] = {0};
    unsigned char imei[64] = {0};
    uint8_t message_type = 0;
    string = strip_whitespace(string);
    log_line(conn, "got message: %s\n", string);
    memset(imei, 0, 64);
    memset(bufstr, 0, BUF_SIZE);
    memset(response, 0, BUF_SIZE);
    memset(data_buffers, 0, sizeof(data_buffers));
    memcpy(bufstr, string, min(length, BUF_SIZE - 1));
    size_t str_count = split_to(',', bufstr, BUF_SIZE, data_buffers, 40);
    log_line(conn, "split message:", string);

    for (size_t i = 0; i < str_count; i++) {
        logprintf(conn, " [%u] %s", i, data_buffers[i]);
    }

    logprintf(conn, "\n");

    if (str_count < 2) {
        log_line(conn, "invalid message\n", string);
    }

    message_type = parse_int(data_buffers[1], 4);
    unsigned char cx = 0;

    switch (message_type) {
        case 1:
            strcpy(imei, data_buffers[2]);
            pad_imei(imei);
            memcpy(conn->imei, imei, strlen(imei) + 1);
            init_imei(conn);
            sprintf(response, "$HX,0001,%s,%s,OK,#", data_buffers[2], data_buffers[3]);
            myrope_send_command(conn, response);
            break;

        case 4:
            tzset();
            int tz = timezone / -3600 ;
            sprintf(response, "$HX,1004,%s,TIME,", conn->imei);
            strftime(bufstr, BUF_SIZE - 1, "%y%m%d%H%M%S,#",  t);
            strcat(response, bufstr);
            myrope_send_command(conn, response);
            break;

        case 5:
            myrope_process_position(conn, str_count, data_buffers);
            break;

        case 2:
            myrope_process_heartbeat(conn, str_count, data_buffers);
            break;

        case 8:
            myrope_process_temperature(conn, str_count, data_buffers);
            break;
    }
}



void myrope_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

    //if we've at least got a header
    if (conn->read_count > 5) {
        rep(conn->recv_buffer, 0, ' ', conn->read_count); //remove all null characters up to read count
        size_t index = idx(conn->recv_buffer, '\n');

        if (index > 0 && index < conn->read_count) {
            conn->recv_buffer[index] = 0;
            index++;
            myrope_process_message(conn, conn->recv_buffer, index);
            memmove(conn->recv_buffer, conn->recv_buffer + index, conn->read_count - index);
            conn->read_count -= index;
            return;
        }
    }
}

void myrope_warn(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    sprintf(buffer, "MSG=%s", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void myrope_warn_audio(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    sprintf(buffer, "MSG=%s", reason);
    ((connection *)vp)->COMMAND_FUNCTION(vp, buffer);
}

void myrope_identify(void * vp) {
    connection * conn = (connection *)vp;
    const uint8_t myrope_start_contains[] = "$HX,0001";
    const uint8_t first_bytes[256];
    memset(first_bytes, 0, sizeof(first_bytes));
    memcpy(first_bytes, conn->recv_buffer, 255);

    if (strstr(first_bytes, myrope_start_contains) != 0 ) {
        fprintf(stdout, "  device type is myrope\n");
        conn->PROCESS_FUNCTION = myrope_process;
        conn->COMMAND_FUNCTION = myrope_send_command;
        conn->WARNING_FUNCTION = myrope_warn;
        conn->AUDIO_WARNING_FUNCTION = myrope_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = myrope_warn;
        conn->timeout_time = time(0) + MYROPE_TIMEOUT;
    }
}

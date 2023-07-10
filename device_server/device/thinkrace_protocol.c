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

#define THINKRACE_TIMEOUT 600
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
    wifi_db_entry db_entry;
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
    lat = is_east == 'W' ? -lat : lat;
    lng = is_north == 'S' ? -lng : lng;

    if (lat != 0 || lng != 0) {
        valid_position = true;
        position_type = 0;
    }

    if ( parse_count > 5) {
        db_entry.network_count = split_to('&', data_buffers[5], strlen(data_buffers[5]) + 1, wifi_split, 16);

        if ( db_entry.network_count > 2) {
            for (int i = 0; i <  db_entry.network_count; i++) {
                size_t split_count = split_to('|', wifi_split[i], strlen(wifi_split[i]) + 1, current_network, 3);

                if (split_count == 3) {
                    int values[6];
                    sscanf( current_network[1], "%x:%x:%x:%x:%x:%x",
                            &values[0], &values[1], &values[2],
                            &values[3], &values[4], &values[5] );
                    db_entry.network_buffer[i].mac_addr[0] =  values[0];
                    db_entry.network_buffer[i].mac_addr[1] = values[1];
                    db_entry.network_buffer[i].mac_addr[2] = values[2];
                    db_entry.network_buffer[i].mac_addr[3] = values[3];
                    db_entry.network_buffer[i].mac_addr[4] = values[4];
                    db_entry.network_buffer[i].mac_addr[5] = values[5];
                }
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
    }

    if (!valid_position) {
        //when falling back to LBS try to force GPS geolocation
        if ( (time(0) - conn->since_last_locate ) > 60) {
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

    time_t dt = local_date_to_time(year, month, day, hour, minute, second);

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

        case 9:
            name = "Device has moved";
            break;

        case 10:
            name = "Low battery";
            break;

        case 12:
            name = "Enter GPS blind zone";
            break;

        case 15:
            name = "Exit GPS blind zone";
            break;

        case 16:
            name = "Device opened";
            break;

        case 20:
            name = "External low power alarm";
            break;

        case 21:
            name = "External power protection alarm";
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
            thinkrace_send_command(conn, "HEARTRATE#");
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
        conn->WARNING_FUNCTION = thinkrace_warn;
        conn->AUDIO_WARNING_FUNCTION = thinkrace_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = thinkrace_warn;
        conn->timeout_time = time(0) + THINKRACE_TIMEOUT;
    }
}

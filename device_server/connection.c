#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <memory.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <float.h>

#include "connection.h"
#include "string.h"
#include "util.h"
#include <time.h>
#include "logfiles.h"
#include "geofence.h"
#include "events.h"

//initial method to create a connection object
connection new_connection(int socket) {
    connection result;
    memset(&result, 0, sizeof(connection));
    result.can_log = false;
    result.imei[0] = 0;
    result.device_extra = 0;
    result.send_count = 0;
    result.read_count = 0;
    result.socket = socket;
    result.log_filehandle = 0;
    result.gps_filehandle = 0;
    result.stats_filehandle = 0;
    result.command_response_filehandle = 0;
    result.since_last_status = time(0);
    result.timeout_time = time(0) + 60;
    result.iteration = 0;
    result.fence_count = 0;
    result.current_lat = 0;
    result.current_lon = 0;
    result.current_speed = 0;
    result.since_battalm = time(0);
    result.just_connected = true;
    result.log_disconnect = true;
    result.log_connect = true;
    result.packet_index = 1;
    result.device_time = time(0);
    result.last_gps_lat = -999;
    result.last_gps_lon = -999;
    return result;
}

void close_connection(connection * conn) {
    if (conn->log_filehandle) {
        fclose(conn->log_filehandle);
        conn->log_filehandle = 0;
    }

    if (conn->gps_filehandle) {
        fclose(conn->gps_filehandle);
        conn->gps_filehandle = 0;
    }

    if (conn->event_filehandle) {
        fclose(conn->event_filehandle);
        conn->event_filehandle = 0;
    }

    if (conn->stats_filehandle) {
        fclose(conn->stats_filehandle);
        conn->stats_filehandle = 0;
    }

    if (conn->command_response_filehandle) {
        fclose(conn->command_response_filehandle);
        conn->command_response_filehandle = 0;
    }
}

void init_position(connection * conn) {
    FILE  * fp = fopen (conn->gps_outfile, "r");

    if (fp <= 0) {
        fprintf(stdout, "Failed to open file: %s\n", conn->gps_outfile);
        return;
    }

    /* space for all of that plus a nul terminator */
    char buf[BUF_SIZE ];
    memset(buf, 0, BUF_SIZE);
    /* now read that many bytes from the end of the file */
    fseek(fp, -(BUF_SIZE - 1), SEEK_END);
    size_t len = fread( buf, 1, BUF_SIZE - 1, fp);
    /* and find the last newline character (there must be one, right?) */
    char * last_line = 0;

    for (;;) {
        char * last_newline = strrchr(buf, '\n');

        if (last_newline <= 0) {
            last_line = 0;
            break;
        }

        *last_newline = 0;
        last_line = last_newline + 1;

        if (strlen(last_line) > 2) {
            break;
        }
    }

    if (last_line) {
        fprintf(stdout, " for imei: %s\n", conn->imei);
        fprintf(stdout, " last position: %s\n", last_line);
        uint8_t * p_list[6] = {0};
        size_t cnt = split_to(',', last_line, strlen(last_line), (unsigned char**)&p_list, 6);

        if (cnt > 3) {
            conn->device_time = parse_date(p_list[0]);
            conn->current_lat = parse_float(p_list[1]);
            conn->current_lon = parse_float(p_list[2]);
            conn->current_position_type = parse_int(p_list[4], 1);

            if (conn->current_position_type == 0) {
                conn->last_gps_lat = conn->current_lat;
                conn->last_gps_lon = conn->current_lon;
            }

            fprintf(stdout, " current lat/long: %f %f %u %u\n", conn->current_lat, conn->current_lon, conn->device_time, time(0));
        }
    }

    fclose(fp);
}

void init_imei(connection * conn) {
    //convert our imei and set up paths
    memcpy(conn->gps_outfile, OUTDIR, strlen(OUTDIR) + 1);
    strcat(conn->gps_outfile, conn->imei);
    //base paths are all the same
    memcpy(conn->log_outfile, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->event_outfile, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->command_response_outfile, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->command_infile, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->current_status_file, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->geofence_file, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->disabled_alarms_infile, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->stats_file, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    //but extensions are not
    strcat(conn->gps_outfile, ".gps.txt");
    strcat(conn->log_outfile, ".log.txt");
    strcat(conn->event_outfile, ".event.txt");
    strcat(conn->command_response_outfile, ".command-output.txt");
    strcat(conn->command_infile, ".command.txt");
    strcat(conn->current_status_file, ".status.txt");
    strcat(conn->geofence_file, ".fence.txt");
    strcat(conn->disabled_alarms_infile, ".disabled-alarms.txt");
    strcat(conn->stats_file, ".stats.txt");
    //now that we've got a path/imei we can log things
    conn->can_log = true;
    log_line(conn, "imei recieved: %s \n", conn->imei);
    init_position(conn);
    read_disabled_alarms(conn);
    read_geofence(conn);
    char message[1024] = {0};
    sprintf(message, "device reconnected after %lu seconds.", time(0) - conn->device_time);

    if (conn->log_connect) {
        log_event(conn,  message);
    }
}



void send_string(connection * conn, char * str) {
    strcpy(conn->send_buffer + conn->send_count, str);
    conn->send_count += strlen(str);
}

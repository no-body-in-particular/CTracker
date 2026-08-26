#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <ctype.h>

#include "connection.h"
#include "tracking.h"
#include "string.h"
#include "util.h"
#include <time.h>
#include "logfiles.h"
#include "geofence.h"
#include "events.h"

//initial method to create a connection object
static unsigned long next_connection_id = 1;

connection new_connection(int socket) {
    connection result;
    memset(&result, 0, sizeof(connection));
    //monotonic, so a larger id is always the more recent connection
    result.connection_id = __sync_fetch_and_add(&next_connection_id, 1);
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
    result.current_speed_valid = false;
    result.supports_interval = false;
    result.supports_health_poll = false;
    result.current_interval = 0;
    result.last_interval_change = 0;
    result.since_last_health_poll = 0;
    result.last_heartrate = 0;
    result.last_heartrate_time = 0;
    //not "now": a device that connects while genuinely still should settle to the idle
    //interval rather than be treated as active for the first dwell period
    result.last_activity = 0;
    result.last_stat_time = 0;
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

    //an upload interrupted half way leaves its assembly buffer behind otherwise
    if (conn->image_buffer) {
        free(conn->image_buffer);
        conn->image_buffer = 0;
        conn->image_len = 0;
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
        size_t cnt = split_to(',', last_line, strlen(last_line), (unsigned char **)&p_list, 6);

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

//An imei arrives from the device and is pasted straight into every path below, so anything
//it contains is a filename. A device that sends slashes or dot-dot picks the directory the
//server writes to; the fuzzer produced imeis like "0v7Q/IpiTJrn_sH$" and the server duly
//tried to open them. An imei is 15 decimal digits by definition, so anything that is not a
//digit is not part of one - drop it rather than fold it to a placeholder, which would let
//two different devices collide on one filename.
static void sanitise_imei(char * imei) {
    size_t w = 0;
    size_t r_seen = 0;

    for (size_t r = 0; imei[r]; r++) {
        r_seen++;
        unsigned char c = (unsigned char)imei[r];

        if (c >= '0' && c <= '9') {
            imei[w++] = (char)c;
        }
    }

    imei[w] = 0;

    //Worth saying out loud rather than quietly rewriting. The JIMI imei arrives BCD packed
    //and CONVERT_HEX turns each nibble into a hex character, so a letter here means the
    //device sent a nibble above 9 - the imei was not valid BCD. Stripping keeps the path
    //safe, but two devices differing only in the removed characters would now share a file,
    //so it should be looked at rather than left running.
    if (w != r_seen) {
        fprintf(stdout, "imei contained %u non-digit characters, stripped to '%s'"
                " - a valid imei is 15 decimal digits, so this device is sending"
                " malformed data\n", (unsigned)(r_seen - w), imei);
    }

    //nothing usable came back: refuse to build paths out of it at all
    if (w == 0) {
        snprintf(imei, 2, "0");
    }
}

void init_imei(connection * conn) {
    //convert our imei and set up paths
    sanitise_imei(conn->imei);
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
    memcpy(conn->tracking_file, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
    memcpy(conn->images_file, conn->gps_outfile, strlen(conn->gps_outfile) + 1);
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
    strcat(conn->tracking_file, ".tracking.txt");
    strcat(conn->images_file, ".images.db");
    //now that we've got a path/imei we can log things
    conn->can_log = true;
    //take over as the connection that sends to this device. a device often leaves an older
    //connection half open, and anything written to it is simply lost - which is why health
    //polls were being acknowledged on one socket while the watch never acted on them.
    claim_command_ownership(conn);
    log_line(conn, "imei recieved: %s \n", conn->imei);
    init_position(conn);
    read_disabled_alarms(conn);
    read_geofence(conn);
    //Only an absence worth noticing goes in the event log. This device re-opens its socket
    //constantly, so an unconditional entry here produced hundreds of "reconnected after 3
    //seconds" lines and pushed the real events off the end of the page.
    time_t away = time(0) - conn->device_time;

    if (conn->log_connect && away > EVENT_ABSENCE_MIN) {
        char message[1024] = {0};
        snprintf(message, sizeof(message), "device reconnected after %lu seconds.", (unsigned long)away);
        log_event(conn,  message);
    }
}



void send_string(connection * conn, char * str) {
    //send_buffer is a fixed BUF_SIZE and send_count only falls as the socket drains, so a
    //blocked or slow device leaves it high while commands keep arriving. The old strcpy
    //took no account of either and simply wrote past the end - and send_buffer is the
    //first member of struct connection, so the overflow ran straight through recv_buffer
    //and the filename fields behind it. Truncate instead: a clipped command is recoverable,
    //a corrupted connection struct is not.
    size_t len = strlen(str);
    size_t room = (conn->send_count < BUF_SIZE) ? (BUF_SIZE - 1 - conn->send_count) : 0;

    if (len > room) {
        fprintf(stdout, "send buffer full (%u used, %u wanted), truncating\n",
                (unsigned)conn->send_count, (unsigned)len);
        len = room;
    }

    if (len == 0) {
        return;
    }

    memcpy(conn->send_buffer + conn->send_count, str, len);
    conn->send_count += len;
    conn->send_buffer[conn->send_count] = 0;
}

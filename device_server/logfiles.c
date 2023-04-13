#include "logfiles.h"
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

bool openf(FILE ** fpp, unsigned char * fn) {
    if (*fpp <= 0) {
        *fpp = fopen(fn, "a");

        if (*fpp <= 0) {
            fprintf(stderr, "failed to open file for append: %s\n", fn);
            return false;
        }
    }

    return true;
}

void connFilePrintf(connection * conn, const char * fn, FILE ** handle, const char * format, va_list arglist) {
    if (!conn->can_log || strlen(fn) < 16) {
        return;
    }

    if (!openf(handle, fn)) {
        printf("Failed to open %s\n", conn->command_response_outfile);
        return;
    }

    vfprintf(*handle, format, arglist );
    fflush(*handle);
    fsync(fileno(*handle));
}

void commandvfprintf( connection * conn, const char * format, va_list arglist ) {
    connFilePrintf(conn, conn->command_response_outfile, &conn->command_response_filehandle, format, arglist);
}

void commandprintf( connection * conn, const char * format, ... ) {
    va_list arglist;
    va_start( arglist, format );
    commandvfprintf(conn, format, arglist);
    va_end( arglist );
}

void gpsvfprintf( connection * conn, const char * format, va_list arglist ) {
    connFilePrintf(conn, conn->gps_outfile, &conn->gps_filehandle, format, arglist);
}

void gpsprintf( connection * conn, const char * format, ... ) {
    va_list arglist;
    va_start( arglist, format );
    gpsvfprintf(conn, format, arglist);
    va_end( arglist );
}

void log_position(connection * conn, int type, float lat, float lng, float spd) {
    struct tm tm = *gmtime(&conn->device_time);
    gpsprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,%f,%f,%f,%u\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
              lat,
              lng,
              spd,
              type);
}



void logvfprintf( connection * conn, const char * format, va_list arglist) {
    connFilePrintf(conn, conn->log_outfile, &conn->log_filehandle, format, arglist);
}

void logprintf( connection * conn, const char * format, ... ) {
    va_list arglist;
    va_start( arglist, format );
    logvfprintf(conn, format, arglist);
    va_end( arglist );
}

void eventvfprintf( connection * conn, const char * format, va_list arglist) {
    connFilePrintf(conn, conn->event_outfile, &conn->event_filehandle, format, arglist);
}

void eventprintf( connection * conn, const char * format, ... ) {
    va_list arglist;
    va_start( arglist, format );
    eventvfprintf(conn, format, arglist);
    va_end( arglist );
}


void statsvfprintf( connection * conn, const char * format, va_list arglist) {
    connFilePrintf(conn, conn->stats_file, &conn->stats_filehandle, format, arglist);
}

void statsprintf( connection * conn, const char * format, ... ) {
    va_list arglist;
    va_start( arglist, format );
    statsvfprintf(conn, format, arglist);
    va_end( arglist );
}

void write_stat(connection * conn, char * value_name, float value) {
    struct tm tm = *gmtime(&conn->device_time);
    statsprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,%s,%.2f\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, value_name, value);
}

void write_sat_count(connection * conn, int position_type, int num_sats) {
    if (position_type == 0) {
        write_stat(conn, "gps_sats", num_sats);
    }

    if (position_type == 1) {
        write_stat(conn, "lbs_stations", num_sats);
    }

    if (position_type == 2) {
        write_stat(conn, "wifi_networks", num_sats);
    }
}
void statusvfprintf(connection * conn, const char * format, va_list args) {
    FILE * fp;

    if (!conn->can_log || strlen(conn->event_outfile) < 16) {
        return;
    }

    fp = fopen (conn->current_status_file, "w");

    if (fp <= 0 ) {
        return;
    }

    vfprintf(fp, format, args);
    fclose (fp);
}

void statusprintf(connection * conn, const char * format, ...) {
    va_list arglist;
    va_start( arglist, format );
    statusvfprintf(conn, format, arglist);
    va_end( arglist );
}

void log_command_response(connection * conn, const unsigned char * response) {
    struct tm tm = *gmtime(&conn->device_time);
    commandprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    for (size_t i = 0; i < strlen(response); i++) {
        unsigned char curr = response[i];

        if (isprint(curr)) {
            commandprintf(conn, "%c", curr);

        } else {
            commandprintf(conn, "\\0x%x", curr);
        }
    }

    commandprintf(conn, "\n");
}
/* va_list, va_start, va_arg, va_end */

void log_time(connection * conn) {
    struct tm tm = *gmtime(&conn->device_time);
    logprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void log_line( connection * conn, const char * format, ... ) {
    log_time(conn);
    va_list arglist;
    va_start( arglist, format );
    logvfprintf(conn, format, arglist);
    va_end( arglist );
}

void log_array(connection * conn, uint8_t * array, size_t len) {
    for (int n = 0; n < len; n++) {
        logprintf(conn, "%x ", array[n]);
    }
}

void log_buffer(connection * conn) {
    log_line(conn, "buffer contents : ");
    log_array(conn, conn->recv_buffer, conn->read_count);
    logprintf(conn, "\n");
}


void log_event(connection * conn, const unsigned char * response) {
    time_t t = conn->device_time;
    struct tm tm = *gmtime(&t);
    eventprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,%f,%f,%f,%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                conn->current_lat, conn->current_lon, conn->current_speed, response);
}

//if a file grows over 30mb reduce it's size
FILE * log_truncate( FILE * fp, char * name, size_t max_size) {
    if (fp == 0) {
        return 0;
    }

    size_t total_size = 0;
    char buf[BUF_SIZE + 1] = {0}; //initialize an empty buffer
    fseek(fp, 0, SEEK_END); // seek to end of file
    total_size = ftell(fp); // get current file pointer
    clearerr(fp);

    if (total_size <= max_size) {
        return fp;
    }

    char tmpname[32] = "/tmp/logXXXXXX";
    int fd = mkstemp(tmpname);
    FILE * tmpfile = fdopen(fd, "w+");

    if (fd <= 0 || tmpfile == 0) {
        fprintf(stdout, "Failed to open %s \n", tmpname);
        clearerr(fp);
        return fp;
    }

    fp = freopen(0, "r", fp);
    fseek(fp, -max_size, SEEK_END);//skip to the position where we have to read our date from
    clearerr(fp);
    int curr = 0;

    while (curr = fgetc(fp), curr != '\n' && curr != EOF  ); //move to the next line

    size_t read = 0;
    bool success = true;

    while ((read = fread(buf, 1, BUF_SIZE, fp)) > 0) {
        size_t writ = fwrite(buf, 1, read, tmpfile);

        if (writ != read) {
            success = false;
        }
    }

    if (success) {
        fp = freopen(0, "w", fp);
        rewind(tmpfile); // seek to begin of file

        while ((read = fread(buf, 1, BUF_SIZE, tmpfile)) > 0) {
            fwrite(buf, 1, read, fp);
        }
    }

    fp = freopen(0, "a", fp);
    tmpfile = freopen(0, "w", tmpfile);
    fclose(tmpfile);
    unlink(tmpname);
    return fp;
}

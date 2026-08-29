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
        //this reported command_response_outfile whichever file had actually failed, so a
        //permission problem on the event log looked like one on the command log and sent
        //the search off in the wrong direction
        printf("Failed to open %s\n", fn);
        return;
    }

    //a device often holds more than one connection open at once, and every connection
    //has its own FILE * onto the same per-imei file. two buffered writes could interleave
    //part way through a line and fuse two records into one, which is where rows like
    //"2026-01-03T12:02:112026-01-03T13:32:11Z,speed,0.00" came from - a mangled timestamp
    //that then sorts to the wrong place. format into a buffer and hand the kernel a
    //single write() on the O_APPEND descriptor instead, which is atomic against the
    //other connections rather than merely flushed soon after.
    char buffer[BUF_SIZE * 2];
    int written = vsnprintf(buffer, sizeof(buffer), format, arglist);

    if (written <= 0) {
        return;
    }

    if (written > (int)sizeof(buffer) - 1) {
        written = (int)sizeof(buffer) - 1;
    }

    if (write(fileno(*handle), buffer, (size_t)written) < 0) {
        fprintf(stderr, "failed to write to %s\n", fn);
        return;
    }

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

void log_position(connection * conn, int type, float lat, float lng, float spd, bool has_speed) {
    struct tm tm = *gmtime(&conn->device_time);

    //a tower fix cannot measure speed. writing a zero would claim the device had stopped,
    //which is its own wrong assertion, so leave the field empty - a reader can then tell
    //"not measured" apart from "measured as standing still".
    if (!has_speed) {
        gpsprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,%f,%f,,%u\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  lat,
                  lng,
                  type);
        return;
    }

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

void write_stat_at(connection * conn, char * value_name, float value, time_t when) {
    //A nudge that pulled a small backward step forward used to live here, to keep the file
    //sorted when the position and health clocks interleave. It went in at 14:13 and from
    //14:14 every health stat on the live device was written with the connection's frozen
    //clock instead of the measurement time, collapsing hours of readings onto one instant.
    //That behaviour could not be reproduced in isolation - single packets, bursts, and a
    //failed position lookup all wrote the correct time - so the mechanism is not
    //understood. Removed rather than kept on a guess: an occasionally unsorted file is a
    //far smaller problem than vitals that all share a timestamp, and timeline_sort fixes
    //the ordering without inventing times.

    /*
     * A clock that is years out is not a late reading, it is a watch that has not been told
     * the time.
     *
     * This is not the nudge described above and must not grow into it. That one moved small
     * backward steps to keep the file sorted, and cost every health reading its own
     * timestamp. This only replaces a stamp that cannot be a measurement time at all: three
     * clusters in this file sit on 2022-12-31, 2025-11-27 and 2025-12-31, the dates a device
     * reports before it has been given one, and a single 2022 row stretches a chart across
     * four years and squashes everything real into the right-hand edge.
     *
     * Thirty days is deliberately loose. A reading can legitimately arrive late - a sleep
     * summary is hours old by definition, and a watch out of signal buffers - so the window
     * has to be wide enough that nothing genuine is ever rewritten. Nothing arrives a month
     * late.
     *
     * The arrival time is used instead, and the substitution is logged, because a stat whose
     * timestamp was invented should say so somewhere.
     */
    time_t now = time(0);
    double drift = difftime(when, now);

    if (when <= 0 || drift > STAT_CLOCK_SANITY || drift < -STAT_CLOCK_SANITY) {
        log_line(conn, "stat %s: device clock reads %ld, %.0f days from now - using arrival time\n",
                 value_name, (long)when, drift / 86400.0);
        when = now;
    }

    struct tm tm = *gmtime(&when);
    statsprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,%s,%.2f\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, value_name, value);
}

void write_stat(connection * conn, char * value_name, float value) {
    write_stat_at(conn, value_name, value, conn->device_time);
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

void set_status(connection * conn, int battery_level, int gsm_signal, int position_type, int num_sats ) {
    statusprintf(conn, "%u,%u,%u,%u\n",
                 battery_level,
                 gsm_signal,
                 position_type,
                 num_sats);
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

/*
 * One line, one write.
 *
 * connFilePrintf hands the kernel a single write() per call, which is atomic
 * against the other connections on the same file - but a line was never one
 * call. log_line wrote the timestamp and then the text, log_array wrote one
 * call per byte, and thinkrace_send_command wrote one call per character of
 * the command. Twenty-odd writes from one thread with nothing stopping another
 * thread writing between any two of them, which is how entries ended up inside
 * each other:
 *
 *   2026-08-27T00:41:29Z,sent command: I2026-08-25T21:35:47Z,imei recieved: ...
 *
 * That is roughly one line in two thousand mangled, which is survivable, but
 * the same shape has a second cost that is not. connFilePrintf fsyncs every
 * call, and this log lives on the root ext2 where an fsync measures 4.3 ms. A
 * 22 byte command logged a character at a time is 94 ms of blocking fsync, and
 * it is spent inside the send path - between deciding to ask for a heart rate
 * and the bytes reaching the socket. A receive-buffer dump is longer still.
 *
 * So the text is built in a buffer and written once. It fixes the splicing and
 * takes a command's logging cost from 94 ms to 4.3 ms.
 */
void log_time(connection * conn) {
    struct tm tm = *gmtime(&conn->device_time);
    logprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/*
 * The timestamp every line starts with, into a caller supplied buffer.
 *
 * The server's clock, not the device's. This used conn->device_time, which is the watch's own
 * idea of the time, and that has three problems as a log stamp: it freezes across a reboot,
 * so fifty consecutive entries carry one value while real time moves on; it differs between
 * connections, so two threads writing the same file disagree; and it is occasionally hours
 * stale. Measured on this log: 93 lines out of 28911 go backwards, the worst by 5.4 hours.
 *
 * A log that does not sort is a log that cannot be read. It cost real time today - a reboot
 * looked instantaneous and landed in the wrong place on the timeline, and a crash appeared to
 * happen before the install that fixed it.
 *
 * The device's own clock is not lost. Every reading carries it inside the payload, which is
 * where it belongs: IWAPJK,2026-08-28 03:08:17,13,0 is the watch saying when it measured,
 * and that is a different fact from when the server heard about it.
 */
static int log_stamp(connection * conn, char * out, size_t size) {
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    (void)conn;
    return snprintf(out, size, "%d-%02d-%02dT%02d:%02d:%02dZ,",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void log_line( connection * conn, const char * format, ... ) {
    char line[BUF_SIZE * 2];
    int n = log_stamp(conn, line, sizeof(line));

    if (n < 0 || n >= (int)sizeof(line)) {
        return;
    }

    va_list arglist;
    va_start( arglist, format );
    vsnprintf(line + n, sizeof(line) - (size_t)n, format, arglist);
    va_end( arglist );

    logprintf(conn, "%s", line);
}

void log_array(connection * conn, uint8_t * array, size_t len) {
    char hex[BUF_SIZE * 2];
    size_t at = 0;

    for (size_t n = 0; n < len && at + 4 < sizeof(hex); n++) {
        at += (size_t)snprintf(hex + at, sizeof(hex) - at, "%x ", array[n]);
    }

    logprintf(conn, "%s", hex);
}

/*
 * The parsed fields of a message, in one write.
 *
 * Same fault as the send path had and the same fix. This looped a logprintf per field, so a
 * message with six fields was seven writes with nothing stopping another connection landing
 * between any two - which is where lines like
 *
 *   : [2] 10132026-08-27T18:05:06Z,imei recieved: ...
 *
 * came from, a field list with somebody else's entry fused into it. It survived the first
 * pass over this file because that pass went looking for "sent command" and this says
 * "split message".
 */
void log_fields(connection * conn, const char * prefix,
                unsigned char * fields[], size_t count) {
    char line[BUF_SIZE * 2];
    int at = log_stamp(conn, line, sizeof(line));

    if (at < 0 || at >= (int)sizeof(line)) {
        return;
    }

    at += snprintf(line + at, sizeof(line) - (size_t)at, "%s", prefix);

    for (size_t i = 0; i < count && at < (int)sizeof(line) - 32; i++) {
        at += snprintf(line + at, sizeof(line) - (size_t)at, " [%u] %s",
                       (unsigned)i, fields[i] ? (char *)fields[i] : "");
    }

    if (at < (int)sizeof(line) - 1) {
        line[at++] = '\n';
    }

    line[at] = 0;
    logprintf(conn, "%s", line);
}

/*
 * A whole "sent command: <bytes>" line in one write. Every protocol had its own
 * copy of this loop, each one a write and an fsync per character.
 *
 * printable_only is for the binary protocols, which logged unprintable bytes as
 * an escape rather than putting a control character in the log.
 */
void log_command_bytes(connection * conn, const unsigned char * buf, int start, int end, bool printable_only) {
    char line[BUF_SIZE * 2];
    int at = log_stamp(conn, line, sizeof(line));

    if (at < 0 || at >= (int)sizeof(line)) {
        return;
    }

    at += snprintf(line + at, sizeof(line) - (size_t)at, "sent command: ");

    for (int i = start; i < end && at < (int)sizeof(line) - 6; i++) {
        unsigned char c = buf[i];

        if (!printable_only || isprint(c)) {
            line[at++] = (char)c;

        } else {
            at += snprintf(line + at, sizeof(line) - (size_t)at, "<%02x>", c);
        }
    }

    if (at < (int)sizeof(line) - 1) {
        line[at++] = '\n';
    }

    line[at] = 0;
    logprintf(conn, "%s", line);
}

void log_buffer(connection * conn) {
    log_line(conn, "buffer contents : ");
    log_array(conn, conn->recv_buffer, conn->read_count);
    logprintf(conn, "\n");
}


void log_event(connection * conn, const unsigned char * response) {
    time_t t = conn->device_time;
    struct tm tm = *gmtime(&t);

    //same reasoning as log_position: if the last fix came from a tower there is no
    //measured speed to attach to this event, so the field is left empty
    if (!conn->current_speed_valid) {
        eventprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,%f,%f,,%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                    conn->current_lat, conn->current_lon, response);
        return;
    }

    eventprintf(conn, "%d-%02d-%02dT%02d:%02d:%02dZ,%f,%f,%.2f,%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
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

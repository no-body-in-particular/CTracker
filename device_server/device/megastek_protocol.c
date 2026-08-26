#include <time.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
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

#define MTEK_TIMEOUT 700

bool megastek_send_command( void * c, const char * cmd) {
    connection * conn = (connection *)c;
    size_t start = conn->send_count;
    char translated[64] = {0};

    //adaptive tracking hands every protocol the same UPDATE=<seconds># and lets each one
    //translate. W005 counts in units of 30 seconds - "W005,2" is a 60 second interval -
    //and 0 would switch uploading off altogether, so never round down past 1.
    if (strlen(cmd) > 7 && memcmp(cmd, "UPDATE=", 7) == 0) {
        unsigned int seconds = atoi(cmd + 7);
        unsigned int units = seconds / 30;

        if (units < 1) {
            units = 1;
        }

        if (units > 65535) {
            units = 65535;
        }

        snprintf(translated, sizeof(translated) - 1, "W005,%u", units);
        cmd = translated;
    }

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

/*
 * The alarm word this protocol sends, mapped onto the names the rest of the system uses.
 * Straight from the Megastek Communication Protocol V-A 1.1 alarm table, plus the ones this
 * server has actually seen from belt trackers, which the table does not list.
 *
 * The raw string used to be written into the event log as it arrived, so an SOS appeared as
 * "Help" and a flat battery as "LowBattery" - neither of which matches the "low battery" and
 * "SOS" the other protocols raise, so a rule written against one did not cover the other.
 */
static const struct {
    const char * device;
    const char * event;
} megastek_alarm_names[] = {
    {"Help",        "SOS"},
    {"SOS",         "SOS"},
    {"LowBattery",  "low battery"},
    {"Low Battery", "low battery"},
    {"OverSpeed",   "overspeed"},
    {"LowSpeed",    "low speed"},
    {"VIB",         "vibration"},
    {"Move in",     "moved into the fence"},
    {"Move out",    "moved out of the fence"},
    {"Geo in",      "moved into the fence"},
    {"Geo out",     "moved out of the fence"},
    {"BeltOn",      "belt closed"},
    {"BeltOff",     "belt opened"},
    {"PowerOff",    "powered off"},
    {"Restart",     "restarted"},
};

static const char * megastek_event_name(const char * raw) {
    if (!raw) {
        return "unknown";
    }

    for (size_t i = 0; i < sizeof(megastek_alarm_names) / sizeof(megastek_alarm_names[0]); i++) {
        if (strcasecmp(raw, megastek_alarm_names[i].device) == 0) {
            return megastek_alarm_names[i].event;
        }
    }

    //anything the table does not cover still reaches the log under the device's own name
    return raw;
}

/*
 * A coordinate in the form NMEA uses: degrees, then two digits of minutes, then a fraction.
 * The two helpers above assume a fixed width for the degrees - two for latitude, three for
 * longitude - which holds for the MGV messages, where 00830.77057 is 008 degrees 30.77
 * minutes. It does not hold for the STX family: 2321.931251 there is 23 degrees 21.93
 * minutes, with the leading zero left off.
 *
 * The minutes are always the two digits before the point, whatever the width, so find the
 * point and count back from it rather than counting forward from the start.
 */
static float nmea_coord(const char * str) {
    if (str == 0) {
        return 0;
    }

    const char * dot = strchr(str, '.');
    size_t digits = dot ? (size_t)(dot - str) : strlen(str);

    if (digits < 3) {
        return parse_float((char *)str);
    }

    char degrees[8] = {0};
    size_t degree_digits = digits - 2;

    if (degree_digits > sizeof(degrees) - 1) {
        degree_digits = sizeof(degrees) - 1;
    }

    memcpy(degrees, str, degree_digits);
    return parse_int(degrees, degree_digits) + parse_float((char *)(str + digits - 2)) / 60.0f;
}

/*
 * The other shape this family speaks. Where an MGV message is one long comma separated line
 * of its own design, an STX message wraps a standard NMEA RMC sentence:
 *
 *   STX,<id>,$GPRMC,<time>,<valid>,<lat>,<N/S>,<lon>,<E/W>,<speed>,<course>,<date>,,,<mode>
 *       ,<fix>,<alarm>,imei:<imei>,<sats>,<altitude>,Battery=<n>%,,<charging>,<mcc>,<mnc>,<lac>,<cid>;
 *
 * and some devices leave out the comma after STX and pad the id to a fixed width instead:
 *
 *   STX863070014949464   $GPRMC,...,<mcc>,<mnc>,<lac>,<cid>,...,<alarm>;
 *
 * so rather than count fields from the left, find the $GPRMC and read the sentence from
 * there, then pick the named fields - imei:, Battery= - out of whatever follows. What is left
 * over differs between the two shapes and between vendors, and naming the fields is the only
 * thing that survives that.
 */
static void process_stx_message(connection * conn, char * string, size_t length) {
    unsigned char bufstr[BUF_SIZE] = {0};
    unsigned char * fields[48] = {0};
    unsigned char imei[18] = {0};
    memcpy(bufstr, string, min(length, sizeof(bufstr) - 1));
    //the trailing ;<checksum> is not part of the last field
    unsigned char * semi = (unsigned char *)strchr((char *)bufstr, ';');

    if (semi) {
        *semi = 0;
    }

    size_t count = split_to(',', bufstr, BUF_SIZE, fields, 48);
    size_t gprmc = count;

    for (size_t i = 0; i < count; i++) {
        if (strstr((char *)fields[i], "$GPRMC")) {
            gprmc = i;
            break;
        }
    }

    if (gprmc == count || (gprmc + 9) >= count) {
        log_line(conn, "  STX message with no usable RMC sentence\n");
        return;
    }

    /*
     * The identifier. Named as imei:<n> where the device sends one, which is the only field
     * that is reliably the imei - the id right after STX is a user settable name on some of
     * these ("GerAL22" is one of the real ones), and is padded rubbish on others.
     */
    for (size_t i = 0; i < count; i++) {
        if (strncasecmp((char *)fields[i], "imei:", 5) == 0) {
            snprintf((char *)imei, sizeof(imei), "%s", fields[i] + 5);
            break;
        }
    }

    if (imei[0] == 0) {
        const char * id = (const char *)fields[0];

        if (gprmc == 0) {
            //STX<id>$GPRMC with no commas - the id sits between the two
            id += 3;

        } else if (count > 1) {
            id = (const char *)fields[1];
        }

        snprintf((char *)imei, sizeof(imei), "%s", id);
    }

    strip_whitespace((char *)imei);
    pad_imei((char *)imei);

    if (strlen(imei) <= 1) {
        log_line(conn, "  STX message with no identifier\n");
        return;
    }

    if (strlen(conn->imei) <= 1 || strcmp(conn->imei, (char *)imei) != 0) {
        snprintf(conn->imei, sizeof(conn->imei), "%s", imei);
        init_imei(conn);
    }

    log_line(conn, "  STX message: %s\n", bufstr);
    bool valid = fields[gprmc + 2][0] == 'A';
    float lat = nmea_coord((char *)fields[gprmc + 3]);
    float lon = nmea_coord((char *)fields[gprmc + 5]);

    if (fields[gprmc + 4][0] == 'S') {
        lat = -lat;
    }

    if (fields[gprmc + 6][0] == 'W') {
        lon = -lon;
    }

    //hhmmss.sss and ddmmyy
    const char * t = (const char *)fields[gprmc + 1];
    const char * d = (const char *)fields[gprmc + 9];
    time_t when = time(0);

    if (strlen(t) >= 6 && strlen(d) >= 6) {
        when = date_to_time(parse_int((char *)d + 4, 2), parse_int((char *)d + 2, 2), parse_int((char *)d, 2),
                            parse_int((char *)t, 2), parse_int((char *)t + 2, 2), parse_int((char *)t + 4, 2));
    }

    //named fields, wherever they landed
    int battery = -1;
    const char * alarm = 0;

    /*
     * An RMC sentence runs to twelve fields past the $GPRMC - time, validity, the two
     * coordinates and their hemispheres, speed, course, date, magnetic variation and its
     * hemisphere, then the mode with the checksum stuck to it. So the vendor's own fields
     * start at thirteen. Starting at ten put the scan inside the sentence and it took the
     * "A*62" of the mode and checksum for an alarm on every message.
     */
    for (size_t i = gprmc + 13; i < count; i++) {
        if (strncasecmp((char *)fields[i], "Battery=", 8) == 0) {
            battery = parse_int((char *)fields[i] + 8, 3);

        } else if (alarm == 0 && strlen((char *)fields[i]) > 2
                   && strchr((char *)fields[i], ':') == 0
                   && strchr((char *)fields[i], '*') == 0
                   && strspn((char *)fields[i], "0123456789.") != strlen((char *)fields[i])
                   //a location area or cell id is four hex digits, and one containing a letter
                   //otherwise reads as text - 0E6A and B20E were being reported as alarms
                   && !(strlen((char *)fields[i]) == 4
                        && strspn((char *)fields[i], "0123456789abcdefABCDEF") == 4)) {
            //the first field that is neither a number, an identifier nor a named value
            alarm = (const char *)fields[i];
        }
    }

    if (valid) {
        move_to(conn, when, 0, lat, lon);
        write_sat_count(conn, 0, 1);
    }

    if (battery >= 0) {
        write_stat(conn, "battery_level", battery);
        set_status(conn, battery, 0, 0, 1);
    }

    if (alarm && strcasecmp(alarm, "Nil-Alarms") != 0 && strcasecmp(alarm, "Timer") != 0) {
        log_event(conn, megastek_event_name(alarm));
    }

    conn->timeout_time = time(0) + MTEK_TIMEOUT;
}

void process_message(connection * conn, char * string, size_t length) {
    fprintf(stdout, "string: \n", string);
    msleep(1000);
    unsigned char bufstr[BUF_SIZE];
    //zeroed: split_to only fills as many slots as the message had fields, and every other
    //file here relies on the rest reading back as NULL rather than as whatever the stack
    //happened to hold
    unsigned char * data_buffers[40] = {0};
    unsigned char imei[64] = {0};
    unsigned char * wifi_split[16];
    wifi_db_entry db_entry;
    unsigned char * current_network[3];
    string = strip_whitespace(string);
    memset(imei, 0, 64);
    memset(bufstr, 0, BUF_SIZE);
    memset(data_buffers, 0, sizeof(data_buffers));
    size_t len = min(length, strlen(string));
    len = min(len, BUF_SIZE - 1);
    memcpy(bufstr, string, len);
    size_t str_count = split_to(',', bufstr, strlen(bufstr), data_buffers, 40);
    conn->timeout_time = time(0) + MTEK_TIMEOUT;

    if (memcmp(string, "STX", 3) == 0) {
        process_stx_message(conn, string, length);
        return;
    }

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

    if ( str_count < 2) {
        log_line(conn, "  invalid response length - no imei.\n");
        return;
    }

    memcpy(imei, data_buffers[1], min(strlen(data_buffers[1]), 63));
    pad_imei(imei);

    if (strlen(conn->imei) <= 1 || strcmp(conn->imei, imei) != 0) {
        snprintf(conn->imei, sizeof(conn->imei), "%s", imei);
        init_imei(conn);
        //default wifi on
        megastek_send_command(conn, "W040,0");
        megastek_send_command(conn, "W039,1");
        megastek_send_command(conn, "W005,20"); //update every 10 mins
    }

    if ( str_count < 35) {
        log_line(conn, "  invalid response length - less than 35: %i.\n", str_count);
        return;
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

    double spo2 = parse_float(data_buffers[31]);
    double temp = parse_float(data_buffers[29]);
    double hr = parse_float(data_buffers[24]);
    double steps = parse_float(data_buffers[25]);
    double activity_time = parse_float(data_buffers[26]);
    double shallow_sleep_time = parse_float(data_buffers[27]);
    double deep_sleep_time = parse_float(data_buffers[28]);

    if (strlen(data_buffers[34]) >= 5 && memcmp(data_buffers[34], "Timer", 5) == 0) {
        move_to(conn, dt, position_type, lat, lon);
        write_sat_count(conn, position_type, num_sats);

    } else {
        move_to(conn, dt, position_type, lat, lon);
        log_event(conn, megastek_event_name(data_buffers[34]));
    }

    /*
     * GSM signal strength is 0 to 31, and 99 is the value that means "not known" rather than
     * a measurement. Scaling it like a measurement gave 329 percent, thirty one times in the
     * recorded history, and a full scale 31 gave 103. Treat 99 as absent and hold the rest
     * inside a hundred.
     */
    int raw_rssi = parse_int(data_buffers[23], 2);
    int rssi = (raw_rssi >= 99) ? 0 : (int)(raw_rssi * 3.33f);

    if (rssi > 100) {
        rssi = 100;
    }
    set_status(conn, parse_float( data_buffers[33]), rssi, 0, num_sats);
    write_stat(conn, "battery_level", battery_level);
    write_stat(conn, "signal", rssi);

    if (spo2 > 0) {
        write_stat(conn, "spo2", spo2);
    }

    if (temp > 0) {
        write_stat(conn, "temperature", temp);
    }

    if (hr > 0) {
        write_stat(conn, "heart_rate", hr);
    }

    if (steps > 0) {
        write_stat(conn, "step_count", steps);
    }

    if (activity_time > 0) {
        write_stat(conn, "activity_time", activity_time);
    }

    if (shallow_sleep_time > 0) {
        write_stat(conn, "shallow_sleep_time", shallow_sleep_time);
    }

    if (deep_sleep_time > 0) {
        //this wrote shallow_sleep_time under the deep name, so deep_sleep_time
        //was parsed at the top of the function, used once as the condition
        //here, and then thrown away. every deep sleep row ever recorded was a
        //copy of the shallow one, and the two series were identical wherever
        //both were non-zero - which reads as agreement between two
        //measurements rather than as one measurement printed twice.
        write_stat(conn, "deep_sleep_time", deep_sleep_time);
    }
}

void megastek_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

    //if we've at least got a header
    if (conn->read_count > 5) {
        rep(conn->recv_buffer, 0, ' ', conn->read_count); //remove all null characters up to read count
        /*
         * An MGV message ends ";!" and an STX one ends ";<checksum>" followed by a newline,
         * so framing on '!' alone found the end of the first shape and never the second - the
         * STX devices were identified and then never had a message read off them at all.
         * Whichever terminator comes first ends the message.
         */
        size_t bang = idx(conn->recv_buffer, '!');
        size_t newline = idx(conn->recv_buffer, '\n');
        size_t index = bang;

        if (newline > 0 && newline < conn->read_count && (bang == 0 || bang >= conn->read_count || newline < bang)) {
            index = newline;
        }

        if (index > 0 && index < conn->read_count) {
            process_message(conn, conn->recv_buffer, index - 1);
            index++;
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
    uint8_t first_bytes[13];
    memset(first_bytes, 0, sizeof(first_bytes));
    memcpy(first_bytes, conn->recv_buffer, 12);

    /*
     * The same family sends two shapes and this only recognised one. An STX message wraps an
     * NMEA sentence and puts $GPRMC well past the twelve bytes searched here, so those devices
     * were never identified at all - they fell through every protocol and were dropped.
     */
    if (strstr(first_bytes, megastek_start_contains) != 0
            || memcmp(first_bytes, "STX", 3) == 0) {
        // MEGASTEK device
        fprintf(stdout, "  device type is megastek\n");
        conn->PROCESS_FUNCTION = megastek_process;
        conn->COMMAND_FUNCTION = megastek_send_command;
        //W005 sets the GPRS upload interval
        conn->supports_interval = true;
        conn->WARNING_FUNCTION = megastek_warn;
        conn->AUDIO_WARNING_FUNCTION = megastek_warn_audio;
        conn->MOTOR_WARNING_FUNCTION = megastek_warn;
        conn->timeout_time = time(0) + MTEK_TIMEOUT;
    }
}

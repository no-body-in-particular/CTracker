
#include "geofence.h"
#include "util.h"
#include "logfiles.h"
#include "string.h"
#include "commands.h"
#include <unistd.h>
#include <math.h>
#include "events.h"
#include "tracking.h"

int convert_wday(int day) {
    if (day >= 8 || day < 0) {
        return day;
    }

    return (day + 7) % 7;
}

time_t time_on_day(int day, int hour, int minute) {
    int months[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    tzset();
    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);
    tm.tm_mday += day - tm.tm_wday ;

    if (tm.tm_mday > months[tm.tm_mon]) {
        tm.tm_mday -= months[tm.tm_mon];
        tm.tm_mon + 1;
    }

    tm.tm_wday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute ;
    tm.tm_sec = 0;
    time_t ret = timegm(&tm);
    return ret;
}

geofence fence_from_str(char * str) {
    geofence ret;
    char * data_buffers[40];
    char * time_buffers[4] = {0};
    ret.valid = false;
    size_t str_count = split_to(',', str, BUF_SIZE, (unsigned char **)data_buffers, 39);
    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);
    time_t today_begin = time_on_day(tm.tm_wday, 0, 0) ;
    time_t tomorrow_begin = time_on_day(tm.tm_wday + 1, 0, 0) ;

    if (str_count < 9) {
        return ret;
    }

    if (strlen(data_buffers[0]) < 5) {
        return ret;
    }

    str_count = split_to(':', data_buffers[0], BUF_SIZE, (unsigned char **)time_buffers, 3);

    if (str_count < 2) {
        return ret;
    }

    ret.start_hour =      parse_int( time_buffers[0], 3);
    ret.start_minute = parse_int( time_buffers[1], 3);
    str_count = split_to(':', data_buffers[1], BUF_SIZE, (unsigned char **)time_buffers, 3);

    if (str_count < 2) {
        return ret;
    }

    ret.end_hour =	  parse_int( time_buffers[0], 3);
    ret.end_minute = parse_int( time_buffers[1], 3);

    if (strlen(data_buffers[2]) < 1) {
        return ret;
    }

    if (strlen(data_buffers[3]) < 1) {
        return ret;
    }

    ret.day_of_week = parse_int( data_buffers[2], 1);
    ret.day_of_week = convert_wday(ret.day_of_week);
    ret.type = parse_int( data_buffers[3], 1);

    if (strlen(data_buffers[4] ) < 3) {
        return ret;
    }

    if (strlen(data_buffers[5]) < 3) {
        return ret;
    }

    ret.lat = parse_float( data_buffers[4] );
    ret.lon = parse_float( data_buffers[5] );

    if (strlen(data_buffers[6]) < 1) {
        return ret;
    }

    ret.radius = parse_float( data_buffers[6] ) / 1000;

    if (strlen(data_buffers[7]) < 1) {
        return ret;
    }

    ret.warn_enable = parse_int(data_buffers[7], 1) > 0;

    if (strlen(data_buffers[8]) >= sizeof(ret.name)) {
        return ret;
    }

    strcpy(ret.name, data_buffers[8]);
    strip_unprintable(ret.name);

    //all day fence
    if ((ret.start_hour * 60 + ret.start_minute) == ( ret.end_hour * 60 + ret.end_minute)) {
        ret.start_hour = 0;
        ret.start_minute = 0;
        ret.end_hour = 23;
        ret.end_minute = 59;
    }

    //every day fence, set the start date to today
    if (ret.day_of_week >= 8) {
        ret.day_of_week = tm.tm_wday;
    }

    ret.fence_start_today = time_on_day(ret.day_of_week, ret.start_hour, ret.start_minute);
    ret.fence_end_today =  time_on_day(ret.day_of_week, ret.end_hour, ret.end_minute);

    if (ret.fence_start_today > ret.fence_end_today) {
        ret.fence_start_today = time_on_day(ret.day_of_week - 1, ret.start_hour, ret.start_minute);
        ret.fence_end_today =  time_on_day(ret.day_of_week, ret.end_hour, ret.end_minute);
    }

    // fprintf(stdout, "fence name: %s start: %u end: %u, current time: %u\n", ret.name, ret.fence_start_today, ret.fence_end_today, time(0));
    ret.valid = true;
    return ret;
}

void read_geofence(connection * conn) {
    char buffer[BUF_SIZE];
    memset(conn->fence_list, 0, sizeof(geofence)*MAX_FENCE);
    conn->fence_count = 0;
    FILE * fp = fopen(conn->geofence_file, "r");

    //if there's no commands file, well there is nothing to do
    if (fp <= 0) {
        return;
    }

    fseek(fp, 0L, SEEK_END);

    if (ftell(fp) < 2) {
        fclose(fp);
        return;
    }

    fseek(fp, 0L, SEEK_SET);
    memset(buffer, 0, BUF_SIZE);
    //read the file line by line and send our commands
    size_t fence_index = 0;

    while (fgets(buffer, BUF_SIZE - 1, fp) && fence_index < MAX_FENCE) {
        if (strlen(buffer) > 2) {
            geofence f = fence_from_str(buffer);

            if (f.valid) {
                conn->fence_list[fence_index] = f;
                fence_index++;
            }
        }
    }

    conn->fence_count = fence_index ;
    fclose(fp);
}


/*
 * This does two separate things and they want different treatment.
 *
 * The alarm is raised every time. "Outside of inclusion zone" describes a state that is still
 * true, and somebody who has left where they are meant to be should go on being told so until
 * they are back - not once every ten minutes with silence in between. That is the whole point
 * of a mandatory fence, and rate limiting it, as this briefly did, quietly turned a
 * continuous alarm into an occasional reminder.
 *
 * The event log entry is held down. That is where the storm was: 25957 pairs of identical
 * consecutive events ten seconds apart, one fence accounting for 22143 of them, in a log
 * somebody is supposed to be able to read afterwards. Writing the same line every ten seconds
 * does not make the alarm any louder, it only buries whatever else happened.
 */
void fence_alert(connection * conn, bool alarms, geofence fence, char * message, float lat, float lon, double speed) {
    char buffer[BUF_SIZE * 2] = {0};
    char what[96] = {0};
    //fence and message together, because two fences broken at once are two different things
    //to be told about, while the same one twice is not
    snprintf(what, sizeof(what), "%s: %s", fence.name, message);
    bool already_logged = strcmp(what, conn->last_fence_event) == 0
                          && (time(0) - conn->last_fence_event_time) < FENCE_REPEAT_INTERVAL;

    //every time, for as long as it is true
    if (alarms && fence.warn_enable && !is_alarm_disabled(conn, message)) {
        snprintf(buffer, sizeof(buffer), "%s: %s", fence.name, message);
        conn->WARNING_FUNCTION(conn, buffer);
    }

    if (already_logged) {
        return;
    }

    snprintf(conn->last_fence_event, sizeof(conn->last_fence_event), "%s", what);
    conn->last_fence_event_time = time(0);
    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, fence.name);
    strcat(buffer, ": ");
    strcat(buffer, message);
    log_event(conn, buffer);
}

void move_to(connection * conn, time_t device_time, int position_type, double lat, double lon) {
    /*
     * Every protocol arrives here, so this is the one place worth checking that a coordinate
     * is a coordinate at all. The recorded history has two rows reading
     * -17822271058268280592531456 degrees of latitude, written by a device in 2022 whose
     * parser handed over whatever happened to be in the variable. Two rows out of 1.8 million
     * is not a crisis, but a position file is read back by the map, by the distance
     * calculations and by the fence checks, and one value like that skews all three.
     *
     * A nan fails both comparisons, so it is caught here as well without needing its own test.
     */
    if (!(lat >= -90.0 && lat <= 90.0) || !(lon >= -180.0 && lon <= 180.0)) {
        log_line(conn, "position %f,%f is not on the planet, ignoring it\n", lat, lon);
        return;
    }

    time_t dt = fabs(device_time - conn->device_time);

    //a cell tower fix can sit kilometres from the device and hops as the serving tower
    //changes, so a speed measured against one describes the network rather than the
    //wearer. either end of the pair is enough to poison it. these were already kept out
    //of the speed stat, but log_position() still wrote them to the gps file, which is
    //where the several-hundred km/h rows came from.
    bool lbs_fix = (position_type == 1 || conn->current_position_type == 1);
    double speed = lbs_fix ? NAN : compute_speed(dt, conn->current_lat, conn->current_lon, lat, lon);
    bool allow_trigger = dt > 5 && dt < 1200;

    //a nan compares false against everything, so the old test let it through into the
    //logs untouched while inf was caught. check for it explicitly and first.
    //an implausible reading is discarded rather than rewritten to zero: zero is a claim
    //that the device stood still, which is a different statement from having no measurement
    bool measured = isfinite(speed) && speed >= 0 && speed <= MAX_PLAUSIBLE_SPEED;

    if (!measured) {
        speed = 0;
    }

    conn->current_speed_valid = measured;

    //adopt this fix's own timestamp before anything is written. log_position() and
    //write_stat() both stamp from conn->device_time, so while this assignment sat at the
    //end of the function every position row and speed sample was filed under the
    //previous fix's time - a fix measured at 19:24:19 was stored as 19:17:00. dt and
    //speed are already computed above, so nothing here still needs the old value.
    conn->device_time = device_time;
    //only a measured speed counts - a tower fix cannot produce one
    note_movement(conn, speed, measured);
    log_position(conn, position_type, lat, lon, speed, measured);

    if (measured) {
        write_stat(conn, "speed", speed);
    }

    if ( position_type != 1 && (conn->current_lat != 0 || conn->current_lon != 0) && conn->fence_count > 0) {
        bool fence_mandatory = false;
        bool in_mandatory = false;
        bool got_alert = false;

        for (size_t idx = 0; idx < conn->fence_count; idx++) {
            geofence f = conn->fence_list[idx];

            if ( time(0) > f.fence_start_today && time(0) < f.fence_end_today  ) {
                if ((f.type == FENCE_IN || f.type == FENCE_IN_OUT) && (haversineDistance(f.lat, f.lon, lat, lon) < f.radius) &&
                        (haversineDistance(f.lat, f.lon, conn->current_lat, conn->current_lon) >= f.radius)) {
                    fence_alert(conn, allow_trigger, f, "entered fence area", lat, lon, speed);
                    got_alert = true;
                    break;
                }

                if ((f.type == FENCE_OUT || f.type == FENCE_IN_OUT) && (haversineDistance(f.lat, f.lon, lat, lon) > f.radius) &&
                        (haversineDistance(f.lat, f.lon, conn->current_lat, conn->current_lon) <= f.radius)) {
                    fence_alert(conn, allow_trigger, f, "left fence area", lat, lon, speed);
                    got_alert = true;
                    break;
                }

                if ((f.type == FENCE_EXCLUDE ) && (haversineDistance(f.lat, f.lon, lat, lon) < f.radius) ) {
                    fence_alert(conn, allow_trigger,  f, "inside exclusion zone", lat, lon, speed);
                    got_alert = true;
                    break;
                }
            }

            //test if we have any notifications for in out or in and out
        }

        geofence f_outside;
        memset(&f_outside, 0, sizeof(f_outside));

        for (size_t idx = 0; idx < conn->fence_count; idx++) {
            geofence f = conn->fence_list[idx];
            //test if we have mandatory fences at this time
            //and we are in at least one

            if (f.type == FENCE_STAY &&
                    (time(0) > f.fence_start_today) && (time(0) < f.fence_end_today)) {
                fence_mandatory = true;

                if (haversineDistance(f.lat, f.lon, lat, lon) <= f.radius) {
                    in_mandatory = true;

                } else {
                    if (!f_outside.valid) {
                        f_outside = f;
                    }

                    if ( haversineDistance(f.lat, f.lon, lat, lon) < haversineDistance(f_outside.lat, f_outside.lon, lat, lon) ) {
                        f_outside = f;
                    }
                }
            }
        }

        if (fence_mandatory && false == in_mandatory && false == got_alert) {
            fence_alert(conn, allow_trigger, f_outside, "outside of inclusion zone", lat, lon, 0);
        }
    }

    conn->current_lat = lat;
    conn->current_lon = lon;
    conn->current_speed = speed;
    conn->current_position_type = position_type;

    if (position_type == 0) {
        conn->last_gps_lat = lat;
        conn->last_gps_lon = lon;
    }

    if (conn->just_connected) {
        conn->just_connected = false;
    }
}

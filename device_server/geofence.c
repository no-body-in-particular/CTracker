
#include "geofence.h"
#include "util.h"
#include "logfiles.h"
#include "string.h"
#include "commands.h"
#include <unistd.h>
#include "events.h"

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
    time_t ret = mktime(&tm) - timezone;
    return ret;
}

geofence fence_from_str(char * str) {
    geofence ret;
    char * data_buffers[40];
    char * time_buffers[4] = {0};
    ret.valid = false;
    size_t str_count = split_to(',', str, BUF_SIZE, data_buffers, 39);
    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);
    time_t today_begin = time_on_day(tm.tm_wday, 0, 0) ;

    if (str_count < 9) {
        return ret;
    }

    if (strlen(data_buffers[0]) < 5) {
        return ret;
    }

    str_count = split_to(':', data_buffers[0], BUF_SIZE, time_buffers, 3);

    if (str_count < 2) {
        return ret;
    }

    ret.start_hour =      parse_int( time_buffers[0], 3);
    ret.start_minute = parse_int( time_buffers[1], 3);
    str_count = split_to(':', data_buffers[1], BUF_SIZE, time_buffers, 3);

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
    bool start_is_end = (ret.start_hour * 60 + ret.start_minute) == ( ret.end_hour * 60 + ret.end_minute);

    if ( !start_is_end ) {
        if (ret.day_of_week >= 8) {
            ret.fence_start_today =  time_on_day(tm.tm_wday, ret.start_hour, ret.start_minute);
            ret.fence_end_today =  time_on_day(tm.tm_wday, ret.end_hour, ret.end_minute);

            if (ret.fence_start_today > ( today_begin +  60 * 60 * 24)) {
                ret.fence_start_today -=  60 * 60 * 24;
            }

            if (ret.fence_end_today > ( today_begin +  60 * 60 * 24)) {
                ret.fence_end_today -=  60 * 60 * 24;
            }

        } else {
            ret.fence_start_today = time_on_day(ret.day_of_week, ret.start_hour, ret.start_minute);
            ret.fence_end_today =  time_on_day(ret.day_of_week, ret.end_hour, ret.end_minute);
        }

        if ((ret.end_hour * 60 + ret.end_minute) <= (ret.start_hour * 60 + ret.start_minute)) {
            ret.fence_end_today += 60 * 60 * 24 ;
        }

    } else {
        if (ret.day_of_week >= 8) {
            ret.fence_start_today =  time_on_day(tm.tm_wday, 0, 0);
            ret.fence_end_today =  time_on_day(tm.tm_wday, 23, 59);

        } else {
            ret.fence_start_today =  time_on_day(ret.day_of_week, 0, 0);
            ret.fence_end_today =  time_on_day(ret.day_of_week, 23, 59);
        }
    }

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


void fence_alert(connection * conn, bool alarms, geofence fence, char * message, float lat, float lon, double speed) {
    char buffer[BUF_SIZE * 2] = {0};

    if (alarms && fence.warn_enable && !is_alarm_disabled(conn, message)) {
        sprintf(buffer, "%s: %s", fence.name, message);
        conn->WARNING_FUNCTION(conn, buffer);
    }

    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, fence.name);
    strcat(buffer, ": ");
    strcat(buffer, message);
    log_event(conn, buffer);
}

void move_to(connection * conn, time_t device_time, int position_type, double lat, double lon) {
    time_t dt = device_time - conn->device_time;
    double speed = compute_speed(dt, conn->current_lat, conn->current_lon, lat, lon);

    if ( speed < 0 | speed > 1600) {
        speed = 0;
    }

    log_position(conn, position_type, lat, lon, speed);

    if (position_type != 1 &&  conn->current_position_type != 1) {
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
                    fence_alert(conn, dt > 20, f, "entered fence area", lat, lon, speed);
                    got_alert = true;
                    break;
                }

                if ((f.type == FENCE_OUT || f.type == FENCE_IN_OUT) && (haversineDistance(f.lat, f.lon, lat, lon) > f.radius) &&
                        (haversineDistance(f.lat, f.lon, conn->current_lat, conn->current_lon) <= f.radius)) {
                    fence_alert(conn, dt > 20, f, "left fence area", lat, lon, speed);
                    got_alert = true;
                    break;
                }

                if ((f.type == FENCE_EXCLUDE ) && (haversineDistance(f.lat, f.lon, lat, lon) < f.radius) ) {
                    fence_alert(conn, dt > 20,  f, "inside exclusion zone", lat, lon, speed);
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
            fence_alert(conn, dt > 20, f_outside, "outside of inclusion zone", lat, lon, 0);
        }
    }

    conn->current_lat = lat;
    conn->current_lon = lon;
    conn->current_speed = speed;
    conn->device_time = device_time;
    conn->current_position_type = position_type;

    if (conn->just_connected) {
        conn->just_connected = false;
    }
}

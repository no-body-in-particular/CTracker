#include "util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

static const double PI = 3.14159265359;

bool file_exists (char * filename) {
    struct stat   buffer;
    return (stat (filename, &buffer) == 0);
}

void strip_unprintable(char * from) {
    for (size_t i = 0; i < strlen(from); i++) {
        if (!isprint(from[i])) {
            from[i] = ' ';
        }
    }
}
void convert_imei(unsigned char * from, char * to) {
    CONVERT_HEX(from[0], to[0], to[1]);
    CONVERT_HEX(from[1], to[2], to[3]);
    CONVERT_HEX(from[2], to[4], to[5]);
    CONVERT_HEX(from[3], to[6], to[7]);
    CONVERT_HEX(from[4], to[8], to[9]);
    CONVERT_HEX(from[5], to[10], to[11]);
    CONVERT_HEX(from[6], to[12], to[13]);
    CONVERT_HEX(from[7], to[14], to[15]);
    to[16] = 0;
}


time_t fileModifiedAgo(char * path) {
    struct stat attr;
    stat(path, &attr);
    return  time(0) - attr.st_mtime;
}


double deg2rad(double deg) {
    return deg * (PI / 180);
}


double haversineDistance(double lat1, double lon1, double lat2, double lon2) {
    double R = 6371; // Radius of the earth in km
    double dLat = deg2rad(lat2 - lat1); // deg2rad below
    double dLon = deg2rad(lon2 - lon1);
    double a =
        sin(dLat / 2) * sin(dLat / 2) +
        cos(deg2rad(lat1)) * cos(deg2rad(lat2)) *
        sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    double d = R * c; // Distance in km
    return d;
}

double compute_speed(time_t dt, double lat1, double lon1, double lat2, double lon2) {
    double speed = haversineDistance(lat1, lon1, lat2, lon2);
    double over_time = (fabs(dt) / 3600);
    over_time = over_time < 0.0f ? 0.166666f : over_time;
    speed = speed / over_time;
    return speed;
}

/* msleep(): Sleep for the requested number of milliseconds. */
int msleep(long msec) {
    struct timespec ts;
    int res;

    if (msec < 0) {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

size_t binary_replace(uint8_t * from, size_t fromlen, uint8_t * to, size_t tolen, uint8_t * in, size_t length, size_t safelength) {
    for (size_t i = 0; i < length; i++) {
        if (memcmp(in + i, from, fromlen) == 0) {
            if (fromlen > tolen && (length + tolen - fromlen) > safelength) {
                return 0;
            }

            //copy the data after the identifier the number of bytes ahead
            memmove(in + i + tolen, in + i + fromlen, length - i);
            memcpy(in + i, to, tolen);
            length += tolen - fromlen;
        }
    }

    return length;
}

time_t parse_date(const char * dt) {
    struct tm tmVar = {0};
    time_t timeVar = 0;

    if (sscanf(dt, "%d-%d-%dT%d:%d:%dZ", &tmVar.tm_year, &tmVar.tm_mon, & tmVar.tm_mday, &tmVar.tm_hour, &tmVar.tm_min, &tmVar.tm_sec) == 6) {
        tmVar.tm_year -= 1900;
        tmVar.tm_mon -= 1;
        return timegm(&tmVar);

    } else {
        return -1;
    }
}

time_t date_to_time(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec) {
    struct tm tmVar = {0};
    time_t timeVar = 0;
    tmVar.tm_year = year + 100;
    tmVar.tm_mon = month - 1;
    tmVar.tm_mday = day;
    tmVar.tm_hour = hour;
    tmVar.tm_min = min;
    tmVar.tm_sec = sec;
    tzset();
    timeVar = timegm(&tmVar);
    return timeVar;
}

time_t local_date_to_time(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec) {
    struct tm tmVar = {0};
    time_t timeVar = 0;
    tmVar.tm_year = year + 100;
    tmVar.tm_mon = month - 1;
    tmVar.tm_mday = day;
    tmVar.tm_hour = hour;
    tmVar.tm_min = min;
    tmVar.tm_sec = sec;
    tzset();
    timeVar = time(&tmVar);
    return timeVar;
}


void pad_imei(char * imei) {
    for (; strlen(imei) < 16;) {
        memmove(imei + 1, imei, strlen(imei));
        imei[0] = '0';
    }
}

float voltage_to_soc(float voltage) {
    if (voltage > 4.15) {
        return 100;
    }

    return exp(3.4f * (voltage - 2.74f));
}
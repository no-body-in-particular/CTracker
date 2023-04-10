
#include "multilaterate.h"
#include "util.h"

multilaterate_point multilaterate(multilaterate_point * input, size_t count) {
    multilaterate_point ret = {0, 0, 0};
    float min_lat = INT32_MAX;
    float min_long = INT32_MAX;
    float max_lat = -INT32_MAX;
    float max_long = -INT32_MAX;
    size_t divs = 256;

    if (count < 1) {
        return ret;
    }

    ret.lat = input[0].lat;
    ret.lng = input[0].lng;

    if (count == 1) {
        return ret;
    }

    for (size_t i = 0; i < count; i++) {
        min_lat = min(min_lat, input[i].lat);
        max_lat = max(max_lat, input[i].lat);
        min_long = min(min_long, input[i].lng);
        max_long = max(max_long, input[i].lng);
    }

    double lat_step = max(0.0001, (max_lat - min_lat) / divs);
    double long_step = max(0.0001, (max_long - min_long) / divs);
    double min_target_function = UINT64_MAX;

    //computationally intensive but easy multilateration function
    for (double current_lat = min_lat; current_lat <= max_lat; current_lat += lat_step) {
        for (double current_long = min_long; current_long <= max_long; current_long += long_step ) {
            double target_value = 0;

            for (size_t i = 0; i < count; i++) {
                //distance is quadratic to rssi
                target_value += exp(input[i].strength) * haversineDistance(current_lat, current_long, input[i].lat, input[i].lng);
            }

            if (target_value < min_target_function) {
                ret.lat = current_lat;
                ret.lng = current_long;
            }
        }
    }

    return ret;
}

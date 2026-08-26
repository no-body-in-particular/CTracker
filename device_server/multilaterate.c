
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

    //the span of the readings, for the relative weighting below
    float min_strength = input[0].strength;
    float max_strength = input[0].strength;

    for (size_t i = 0; i < count; i++) {
        min_strength = min(min_strength, input[i].strength);
        max_strength = max(max_strength, input[i].strength);
    }

    double strength_span = max_strength - min_strength;

    double lat_step = max(0.0001, (max_lat - min_lat) / divs);
    double long_step = max(0.0001, (max_long - min_long) / divs);
    double min_target_function = UINT64_MAX;

    /*
     * Walk a grid over the bounding box and keep the point where the weighted sum of
     * distances to every transmitter is smallest - a weighted geometric median.
     *
     * The score of the best point has to be remembered, and it was not: min_target_function
     * was set once and never updated, so every point scored below it and the answer was
     * whichever point the loop happened to visit last. That is the far corner of the bounding
     * box, every time, whatever the inputs. Three transmitters in a triangle returned
     * 52.089901,5.319900 when the answer was near 52.085,5.3067.
     *
     * The weighting is now relative to the readings themselves rather than a function of
     * their absolute value. It used to be exp(strength), which assumed the field held dBm -
     * and even for dBm exp(-50) against exp(-84) is a ratio of about 6e14, so the strongest
     * signal did not weigh more, it was the only one that counted. The one caller that
     * actually uses this passes signal bars scaled to 0..100, where exp() is worse still.
     *
     * Scaling each reading into the range the set covers keeps the weighting monotonic -
     * a stronger transmitter always pulls harder - without any assumption about what unit the
     * caller measured in, and without a spread of a few tens of units turning into a factor
     * of 1e14. Readings that are all equal weigh equally and give a plain geometric median.
     */
    for (double current_lat = min_lat; current_lat <= max_lat; current_lat += lat_step) {
        for (double current_long = min_long; current_long <= max_long; current_long += long_step ) {
            double target_value = 0;

            for (size_t i = 0; i < count; i++) {
                //1 for the strongest reading, down to about 0.1 for the weakest
                double weight = strength_span > 0
                                ? 0.1 + 0.9 * ((input[i].strength - min_strength) / strength_span)
                                : 1.0;
                target_value += weight * haversineDistance(current_lat, current_long, input[i].lat, input[i].lng);
            }

            if (target_value < min_target_function) {
                min_target_function = target_value;
                ret.lat = current_lat;
                ret.lng = current_long;
            }
        }
    }

    return ret;
}

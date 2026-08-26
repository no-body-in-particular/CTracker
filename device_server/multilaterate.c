#include "multilaterate.h"
#include "util.h"
#include <math.h>
#include <float.h>

/*
 * The point that best explains a set of position estimates.
 *
 * Every estimate here comes from looking up one cell tower or one set of wifi networks, and
 * each comes back with an accuracy - how far out the positioning service thinks it might be.
 * So this is not really multilateration in the ranging sense: nothing measures a distance to
 * anything. It is the problem of combining several estimates of the same point, each with its
 * own uncertainty, into one.
 *
 * The answer wanted is the weighted geometric median - the point minimising the weighted sum
 * of distances to the estimates. The median rather than the mean because one badly placed
 * tower should not drag the answer: a mean moves by a tenth of the error when one estimate in
 * ten is wrong, a median barely moves at all, and one estimate in ten being wrong is an
 * ordinary Tuesday for cell tower geolocation.
 *
 * This used to find that point by scoring a 256 by 256 grid over the bounding box of the
 * inputs, which is around sixty five thousand evaluations, each one a haversine per estimate.
 * Weiszfeld's algorithm finds the same point by iterating
 *
 *     x <- sum(w_i * p_i / |x - p_i|) / sum(w_i / |x - p_i|)
 *
 * which converges in a couple of dozen passes of one distance per estimate. Three orders of
 * magnitude less arithmetic, and it is better as well as cheaper for three reasons: the grid
 * could not return an answer outside the bounding box, though the true optimum can be; it
 * could not resolve finer than its step, which was eleven metres at best and four hundred at
 * worst; and it snapped every answer to a grid line.
 */

//metres per degree. Good to a fraction of a percent over the few kilometres these sets span
#define METRES_PER_DEG_LAT 110574.0
#define METRES_PER_DEG_LNG 111320.0
//an estimate is never treated as better than this, so one improbably confident lookup cannot
//take the whole answer on its own
#define ACCURACY_FLOOR_M 25.0
//used when a caller reports no accuracy at all - deliberately vague, so any point that does
//report one is trusted further
#define ACCURACY_UNKNOWN_M 2000.0
#define WEISZFELD_ITERATIONS 64
//a step smaller than a centimetre is not a better answer, it is arithmetic
#define WEISZFELD_TOLERANCE_M 0.01
//guards the division when the iterate lands exactly on one of the estimates
#define WEISZFELD_EPSILON_M 0.5

/*
 * How much this estimate counts for.
 *
 * An accuracy is a standard deviation in metres, so 1/accuracy^2 is inverse variance
 * weighting - the weighting that makes the combination the most likely position rather than
 * merely a reasonable one. A tower reported to 300 metres therefore counts about eleven times
 * a tower reported to a kilometre, which is the right ratio rather than a chosen one.
 *
 * Where no accuracy is given the signal strength stands in for it. It is a poor substitute -
 * it says which estimate is probably nearer, not how far out any of them are - so it only
 * spreads the weights over a factor of ten, while a real accuracy is allowed to matter more.
 */
static double weight_of(const multilaterate_point * p, float min_strength, float strength_span)
{
    if (p->accuracy > 0) {
        double a = p->accuracy < ACCURACY_FLOOR_M ? ACCURACY_FLOOR_M : p->accuracy;
        return 1.0 / (a * a);
    }

    double relative = strength_span > 0 ? (p->strength - min_strength) / strength_span : 1.0;
    double a = ACCURACY_UNKNOWN_M / (1.0 + 9.0 * relative);
    return 1.0 / (a * a);
}

multilaterate_point multilaterate(multilaterate_point * input, size_t count) {
    multilaterate_point ret = {0, 0, 0, 0};

    if (count < 1) {
        return ret;
    }

    ret.lat = input[0].lat;
    ret.lng = input[0].lng;
    ret.accuracy = input[0].accuracy;

    if (count == 1) {
        return ret;
    }

    float min_strength = input[0].strength;
    float max_strength = input[0].strength;

    for (size_t i = 0; i < count; i++) {
        min_strength = min(min_strength, input[i].strength);
        max_strength = max(max_strength, input[i].strength);
    }

    float strength_span = max_strength - min_strength;

    /*
     * Work in metres on a plane centred on the estimates rather than in degrees. A degree of
     * longitude is a different distance from a degree of latitude everywhere except the
     * equator, so treating the two as interchangeable stretches the geometry east to west -
     * at this latitude by about a third, which is enough to move the answer.
     */
    double lat0 = 0;
    double lng0 = 0;

    for (size_t i = 0; i < count; i++) {
        lat0 += input[i].lat;
        lng0 += input[i].lng;
    }

    lat0 /= count;
    lng0 /= count;
    double lng_scale = METRES_PER_DEG_LNG * cos(lat0 * M_PI / 180.0);

    double xs[64];
    double ys[64];
    double ws[64];
    size_t n = count > 64 ? 64 : count;
    double wsum = 0;
    double x = 0;
    double y = 0;

    for (size_t i = 0; i < n; i++) {
        xs[i] = (input[i].lng - lng0) * lng_scale;
        ys[i] = (input[i].lat - lat0) * METRES_PER_DEG_LAT;
        ws[i] = weight_of(&input[i], min_strength, strength_span);
        wsum += ws[i];
        x += ws[i] * xs[i];
        y += ws[i] * ys[i];
    }

    if (wsum <= 0) {
        return ret;
    }

    //start from the weighted mean, which is already a decent answer and a good starting point
    x /= wsum;
    y /= wsum;

    for (int iter = 0; iter < WEISZFELD_ITERATIONS; iter++) {
        double num_x = 0;
        double num_y = 0;
        double den = 0;

        for (size_t i = 0; i < n; i++) {
            double dx = x - xs[i];
            double dy = y - ys[i];
            double d = sqrt(dx * dx + dy * dy);

            //landing exactly on an estimate would divide by zero, and near it would let one
            //estimate swamp the sum, so no estimate is ever closer than this
            if (d < WEISZFELD_EPSILON_M) {
                d = WEISZFELD_EPSILON_M;
            }

            double w = ws[i] / d;
            num_x += w * xs[i];
            num_y += w * ys[i];
            den += w;
        }

        if (den <= 0) {
            break;
        }

        double next_x = num_x / den;
        double next_y = num_y / den;
        double step = sqrt((next_x - x) * (next_x - x) + (next_y - y) * (next_y - y));
        x = next_x;
        y = next_y;

        if (step < WEISZFELD_TOLERANCE_M) {
            break;
        }
    }

    ret.lng = (float)(lng0 + x / lng_scale);
    ret.lat = (float)(lat0 + y / METRES_PER_DEG_LAT);

    /*
     * And say how good the answer is, which the caller could not know before. Combining
     * independent estimates leaves an uncertainty of 1/sqrt(sum of weights); the spread of the
     * estimates around the answer is taken as well, and the larger of the two wins, because
     * estimates that disagree with each other are evidence that at least one of them is more
     * wrong than it admitted.
     */
    double combined = 1.0 / sqrt(wsum);
    double spread = 0;

    for (size_t i = 0; i < n; i++) {
        double dx = x - xs[i];
        double dy = y - ys[i];
        spread += ws[i] * sqrt(dx * dx + dy * dy);
    }

    spread /= wsum;
    ret.accuracy = (float)(combined > spread ? combined : spread);
    return ret;
}

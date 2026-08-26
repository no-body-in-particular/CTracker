#ifndef MULTILATERATE_H_INCLUDED
#define MULTILATERATE_H_INCLUDED
#include <stdint.h>
#include <stddef.h>

typedef struct __attribute__((packed)) {
    float lat;
    float lng;
    //relative signal quality. Only used to weight a point whose accuracy is unknown, and only
    //relative to the other points in the same call - no unit is assumed.
    float strength;
    //how far out the lookup that produced this point thinks it might be, in metres. This is
    //what the positioning service reports alongside a fix and it is the better weight by far,
    //because it is an actual statement about uncertainty rather than a proxy for one. 0 when
    //the caller has nothing to say.
    float accuracy;
}

multilaterate_point;
multilaterate_point multilaterate(multilaterate_point * input, size_t count);

#endif // MULTILATERATE_H_INCLUDED

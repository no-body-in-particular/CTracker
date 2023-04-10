#ifndef MULTILATERATE_H_INCLUDED
#define MULTILATERATE_H_INCLUDED
#include <stdint.h>
#include <stddef.h>

typedef struct __attribute__((packed)) {
    float lat;
    float lng;
    float strength;
}
multilaterate_point;
multilaterate_point multilaterate(multilaterate_point * input, size_t count);

#endif // MULTILATERATE_H_INCLUDED

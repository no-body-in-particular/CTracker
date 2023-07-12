#include "stdint.h"
#include "stdbool.h"

#ifndef _GEOLOCATE_H_
#define  _GEOLOCATE_H_

typedef struct __attribute__((packed)) {
    float lat;
    float lng;
    float radius;
    bool valid;
    uint64_t last_tried;
}
location_result;

typedef struct __attribute__((packed)) {
    uint8_t mac_addr[6];//adding nulls means you can compare it with a simple uint64 comparison. this is relatively quick.
    uint8_t nulls[2];
    uint8_t rssi;
}
wifi_network;

typedef struct __attribute__((packed)) {
    uint16_t mcc;
    uint16_t lac;
    uint8_t mnc;
    uint32_t cell_id;//since we use a little endian system and this is 4 bytes, we can just use an uint64 to cover all the data in the beginning of this struct
    location_result location;
}
cell_tower;

location_result google_geolocate_tower(cell_tower * tower) ;
location_result here_geolocate_tower(cell_tower * tower) ;
location_result google_geolocate_wifi(wifi_network * network, size_t network_count) ;
location_result here_geolocate_wifi(wifi_network * network, size_t network_count);
location_result geolocate_tower(cell_tower * tower);
location_result geolocate_wifi(wifi_network * network, size_t network_count);

#endif

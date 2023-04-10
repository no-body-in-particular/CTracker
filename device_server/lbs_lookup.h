#ifndef LBS_LOOKUP_H_INCLUDED
#define LBS_LOOKUP_H_INCLUDED


#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <pthread.h>
#include "config.h"
#include "web_geolocate.h"

typedef struct __attribute__((packed)) {
    cell_tower * tower_buffer ;
    cell_tower * current_cache ;
    size_t cache_size;
    size_t tower_memory_size;
    size_t cache_memory_size;
    size_t tower_count;
    time_t cache_age;
    pthread_mutex_t mutex;
}
cell_db;

void init_lbs();
location_result lbs_lookup(cell_tower * to_find, float last_lat, float last_lng);
#endif // LBS_LOOKUP_H_INCLUDED

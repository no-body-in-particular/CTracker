#ifndef WIFI_LOOKUP_H_INCLUDED
#define WIFI_LOOKUP_H_INCLUDED


#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <pthread.h>
#include "config.h"
#include "web_geolocate.h"



typedef struct __attribute__((packed)) {
    wifi_network network_buffer[16] ;
    size_t network_count;
    location_result result;
}
wifi_db_entry;


typedef struct  {
    wifi_db_entry * network_buffer ;
    wifi_db_entry * network_cache ;
    size_t network_count;
    size_t network_buffer_size;
    size_t cache_count;
    size_t network_cache_size;
    time_t cache_age;
    pthread_mutex_t mutex;
}
wifi_db;




location_result wifi_lookup(wifi_network * first, size_t network_count) ;
void init_wifi() ;
location_result wifi_to_cache(wifi_db_entry  networks);
void test();
#endif // WIFI_LOOKUP_H_INCLUDED

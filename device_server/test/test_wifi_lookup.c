#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <err.h>
#include <sys/select.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../wifi_lookup.c"

void set_network1(wifi_network * network) {
    uint8_t mac_list[5][6] = {
        {0x2A, 0xF5, 0xA2, 0x84, 0x0F, 0x35},
        {0x96, 0x6A, 0xB0, 0x44, 0xAB, 0xAE},
        {0x96, 0x6A, 0xB0, 0x4A, 0x2A, 0x72},
        {0xB0, 0xB9, 0x8A, 0x51, 0x8E, 0xAC},
        {0xB0, 0xB9, 0x8A, 0x61, 0x4F, 0xBB}
    };
    memset(network, 0, sizeof(wifi_network) * 5);
    memcpy(network[0].mac_addr, mac_list[0], 6);
    memcpy(network[1].mac_addr, mac_list[1], 6);
    memcpy(network[2].mac_addr, mac_list[2], 6);
    memcpy(network[3].mac_addr, mac_list[3], 6);
    memcpy(network[4].mac_addr, mac_list[4], 6);
}

void set_network2(wifi_network * network) {
    uint8_t mac_list[6][6] = {
        {0x0, 0x16, 0x83, 0x25, 0x7d, 0xe0},
        {0x1, 0x60, 0xa4, 0xb7, 0x68, 0xee},
        {0x2, 0x60, 0xa4, 0xb7, 0x68, 0xf1},
        {0x3, 0x66, 0xa4, 0xb7, 0x68, 0xf1},
        {0x4, 0xd8, 0x47, 0x32, 0x49, 0xe2},
        {0x5, 0xfc, 0x45, 0xc3, 0x96, 0xef}

    };
    memset(network, 0, sizeof(wifi_network) * 6);
    memcpy(network[0].mac_addr, mac_list[0], 6);
    memcpy(network[1].mac_addr, mac_list[1], 6);
    memcpy(network[2].mac_addr, mac_list[2], 6);
    memcpy(network[3].mac_addr, mac_list[3], 6);
    memcpy(network[4].mac_addr, mac_list[4], 6);
    memcpy(network[5].mac_addr, mac_list[5], 6);
}

int main(int argc, char * argv[]) {
    wifi_network network[6];
    set_network1(network);
        fprintf(stdout,"test\n");

    init_wifi();
    fprintf(stdout,"test\n");
    wifi_cache_to_database(&wifi_database);
    location_result result = wifi_lookup(network, 5);
    fprintf(stdout, "%u %f %f\n", result.valid, result.lat, result.lng);
    wifi_cache_to_database(&wifi_database);
    result = wifi_lookup(network, 5);
    fprintf(stdout, "%u %f %f\n", result.valid, result.lat, result.lng);
    /*   set_network2(network);
       result = wifi_lookup(network, 5);
       fprintf(stdout, "%u %f %f\n", result.valid, result.lat, result.lng);
       wifi_cache_to_database(&wifi_database);
       set_network1(network);
       result = wifi_lookup(network, 5);
       fprintf(stdout, "%u %f %f\n", result.valid, result.lat, result.lng);
       set_network2(network);
       result = wifi_lookup(network, 5);
       fprintf(stdout, "%u %f %f\n", result.valid, result.lat, result.lng);*/
    return 0;
}

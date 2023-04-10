#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#ifndef _TYPEDEFS_H_
#define _TYPEDEFS_H_
#include "../config.h"
#include "../util.h"
#include "../connection.h"

typedef struct __attribute__((packed)) {
    uint8_t start_bit[2];
    uint8_t length;
    uint8_t protocol_number;
}
data_packet_header;


typedef struct __attribute__((packed)) {
    uint8_t start_bit[2];
    uint16_t length;
    uint8_t protocol_number;
    uint8_t information;
}
data_packet_v2_header;

typedef struct __attribute__((packed)) {
    uint16_t serial_number;
    uint16_t crc;
    uint8_t stop_bit[2];
}
data_packet_footer;

typedef struct __attribute__((packed)) {
    data_packet_header header;
    data_packet_v2_header v2_header;
    uint8_t  data[1024];
    data_packet_footer footer;
}
data_packet;

typedef struct __attribute__((packed)) {
    uint8_t imei[8];	//imei information when printed as hex
}
device_info;


typedef struct __attribute__((packed)) {
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
}
date_time_info;

typedef struct __attribute__((packed)) {
    unsigned char num_satelites;
    uint32_t lattitude;
    uint32_t longitude;
    unsigned char speed;
    uint16_t status_and_course;
}
gps_information;


typedef struct __attribute__((packed)) {
    unsigned short mcc;
    unsigned char mnc;
    unsigned short lac;
    unsigned char cellID[3];
}
lbs_information;

typedef struct __attribute__((packed)) {
    unsigned char terminal_info;
    unsigned char voltage;
    unsigned char gsm_strength;
    unsigned char alert;
    unsigned char language;
    unsigned char fence;
}
status_information;

typedef struct __attribute__((packed)) {
    unsigned char status;
    unsigned short voltage;
    unsigned char rssi;
}
heartbeat_information;


typedef struct __attribute__((packed)) {
    gps_information gps_info;
    lbs_information lbs_info;
}
gps_lbs_information;


typedef struct __attribute__((packed)) {
    gps_information gps_info;
    lbs_information lbs_info;
    status_information status_info;
}
gps_lbs_status_information;

typedef struct __attribute__((packed)) {
    date_time_info date_time;
    unsigned char information[255];
}
information_package;

typedef struct __attribute__((packed)) {
    unsigned short lac;
    unsigned char cellID[3];
    uint8_t rssi;
}
lbs_info_entry;

typedef struct __attribute__((packed)) {
    unsigned short mcc;
    unsigned char mnc;
    lbs_info_entry entries[6];
}
lbs_information_package;

typedef struct __attribute__((packed)) {
    uint8_t length;
    uint32_t server_flags;
    unsigned char cmd[250];
}
command_packet;


typedef struct __attribute__((packed)) {
    uint8_t flags;
    uint8_t type;
    unsigned char cmd[250];
}
command_response;

#endif

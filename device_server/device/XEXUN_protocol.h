#ifndef _XEXUN_PROTOCOL_H_
#define _XEXUN_PROTOCOL_H_
#include "../config.h"
#include "../wifi_lookup.h"

typedef struct __attribute__((packed)) {
    uint8_t start_bit[2];
    uint16_t message_id;
    uint16_t serial_number;
    uint8_t imei[8];
    uint16_t body_attributes;
    uint16_t ecc;
    uint8_t data[BUF_SIZE];
}
xexun_data;

typedef struct __attribute__((packed)) {
    uint8_t packet_count;
    uint16_t packet_size[BUF_SIZE];
}
position_header;

typedef struct __attribute__((packed)) {
    uint8_t serial;
    uint32_t timestamp;
    uint8_t csq;
    uint16_t batt_attributes;
    uint8_t position_type;
    uint8_t data[BUF_SIZE];
}
position_packet;

typedef struct __attribute__((packed)) {
    uint8_t data_type;
    uint8_t data[BUF_SIZE];
}
position_data;

typedef struct __attribute__((packed)) {
    uint8_t sat_count;
    float lon;
    float lat;
}
gps_data;


typedef struct __attribute__((packed)) {
    uint8_t mac_addr[6];
    uint8_t rssi;
}
xex_wifi_network;

typedef struct __attribute__((packed)) {
    uint8_t network_count;
    xex_wifi_network networks[6];
}
wifi_data;

typedef struct __attribute__((packed)) {
    uint16_t mcc;
    uint16_t mnc;
    uint32_t lac;
    uint32_t cid;
    uint8_t rssi;
}
lbs_network;


typedef struct __attribute__((packed)) {
    uint8_t network_count;
    lbs_network networks[6];
}
lbs_data;

typedef struct __attribute__((packed)) {
    uint32_t binding_serial;
    uint32_t update_time;
    uint16_t rangind_distance;
    uint16_t base_station_battery_status;
}
tof_network;


typedef struct __attribute__((packed)) {
    uint8_t tof_count;
    tof_network networks[4];
}
tof_data;

typedef struct __attribute__((packed)) {
    uint16_t speed;
    uint16_t direction;
}
speed_data;

typedef struct __attribute__((packed)) {
    double lon;
    double lat;
}
differential_gps_data;

typedef struct __attribute__((packed)) {
    uint32_t fingerprint;
}
fingerprint_data;

typedef struct __attribute__((packed)) {
    uint8_t version[20];
    uint8_t imsi[8];
    uint8_t iccid[10];
}
version_data;


typedef struct __attribute__((packed)) {
    uint8_t timezone;
    uint16_t positioning_interval;
    uint8_t packet_send_interval;
    uint8_t tof_binding_no;
    uint8_t starting_time;
    uint8_t end_time;
    uint8_t ranging_treshold;
    uint32_t binding_id;
}
tof_parameters;

typedef struct __attribute__((packed)) {
    uint8_t nfc_data_type;
    uint8_t number_of_cards;
    uint8_t card_id[3];
}
nfc_data;

typedef struct __attribute__((packed)) {
    uint32_t extended_data_type;
    uint8_t data[BUF_SIZE];
}
extended_data;

typedef struct __attribute__((packed)) {
    float temperature;
    float raw_temperature;
    float temperature_calibration_value;
}
temperature_data;

typedef struct __attribute__((packed)) {
    uint8_t heart_rate;
    uint8_t low_pressure;
    uint8_t high_pressure;
    uint16_t step_count;
    uint8_t blood_oxygen;
}
human_body_data;


typedef struct __attribute__((packed)) {
    uint32_t sos: 1;
    uint32_t dismantle: 1;
    uint32_t na1: 1;
    uint32_t na2: 1;
    uint32_t charging: 1;
    uint32_t na3: 1;
    uint32_t turn_on: 1;
    uint32_t heart_rate: 1;
    uint32_t check_switch: 1;
    uint32_t disconnect: 1;
    uint32_t connection: 1;
    uint32_t gravity_alarm: 1;
    uint32_t movement: 1;
    uint32_t static_alm: 1;
    uint32_t na4: 1;
    uint32_t fall_down: 1;
    uint32_t optical_switch: 1;
    uint32_t not_touched: 1;
    uint32_t touch: 1;
    uint32_t ankle_bracelet_tag_offline: 1;
    uint32_t ankle_bracelet_tag_distance: 1;
    uint32_t coordinates_out_of_bounds: 1;
    uint32_t car_power_down: 1;
}
alarm_data;




void XEXUN_process(void * conn);
void XEXUN_identify(void * vp);

#endif

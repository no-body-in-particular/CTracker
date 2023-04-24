#include <time.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <unistd.h>

#include "../util.h"
#include "../config.h"
#include "../crc16.h"
#include "../util.h"

#include "jimi_packet.h"
#include "jimi_protocol.h"
#include "jimi_util.h"
#include "../connection.h"
#include "../commands.h"
#include "../logfiles.h"
#include "../events.h"
#include "../geofence.h"
#include "../lbs_lookup.h"
#include "../multilaterate.h"

#define WARNONCE "MUTESW,OFF#\nWARNING#\nWAIT#\nWARNOFF#\nMUTESW,ON#"
#define JIMI_TIMEOUT 400
//default settings used for alerts and the device itself
//disabled by default, rename DEFAULTS_ALT to DEFAULTS to enable this feature
#define DEFAULTS(CONN);
#define DEFAULTS_ALT(CONN) \
        add_command(conn, "MUTESW,ON#");\
        add_command(conn, "TASK,ON#");\
        add_command(conn, "MODE,1,40#");\
        add_command(conn, "GPSMODE,2#");\
        add_command(conn, "SPEED,1,100#");\
        add_command(conn, "POWERALM,OFF#");\
        add_command(conn, "SERVER,1,coredump.ws,9000,0#");\
        add_command(conn, "GMT,W,1,0#");\
        add_command(conn, "SENALM,ON#");

#define COORD(X)  SWAP_UINT32(X) / 1800000.0f;


//reads a date from file into the local time format.
void print_gps_info(connection * conn, information_package package) {
    gps_information info;
    char time_prefix[128];
    memset(time_prefix, 0, 128);
    memcpy(&info, package.information, sizeof(gps_information));
    float lat = COORD(info.lattitude);
    float lon = COORD(info.longitude) ;
    info.status_and_course = SWAP_UINT16(info.status_and_course);

    if (info.status_and_course & 8192) {
        lat *= -1;
    }

    if (info.status_and_course & 16384) {
        lon *= -1;
    }

    log_line(conn, "got gps info, lat/long: %f %f\n", lat, lon);
    time_t dt = date_to_time(package.date_time.year,
                             package.date_time.month,
                             package.date_time.day,
                             package.date_time.hour,
                             package.date_time.minute,
                             package.date_time.second);

    if ((info.num_satelites & 0x0f) >= 4 ) {
        move_to(conn, dt, 0, lat, lon);
    }

    write_stat(conn, "gps_sats", info.num_satelites & 0x0f);
}

void process_lbs_info(connection * conn, information_package package) {
    lbs_information_package lbs_information;
    cell_tower tower[6];
    multilaterate_point points[6];
    //test if we have a recent gps position - if not we add the lbs position
    memcpy(&lbs_information, package.information, sizeof(lbs_information_package));
    lbs_information.mcc = SWAP_UINT16(lbs_information.mcc);
    log_line(conn, "got lbs location package %u %u\n", lbs_information.mcc, lbs_information.mnc);
    size_t network_count = 0;

    for (size_t i = 0; i < 6; i++) {
        tower[i].mcc = lbs_information.mcc;
        tower[i].mnc = lbs_information.mnc;
        tower[i].cell_id = lbs_information.entries[i].cellID[0] << 16 | lbs_information.entries[i].cellID[1] << 8 | lbs_information.entries[i].cellID[2] ;
        tower[i].lac = SWAP_UINT16(lbs_information.entries[i].lac);

        if (tower[i].cell_id == 0 && tower[i].lac == 0) {
            break;
        }

        log_line(conn, "got tower mcc: %u mnc: %u cid: %u lac: %u rssi:%u\n", lbs_information.mcc, lbs_information.mnc, tower[i].cell_id, tower[i].lac, lbs_information.entries[i].rssi);
        network_count++;
    }

    size_t point_count = 0;

    for (size_t i = 0; i < network_count; i++) {
        tower[i].location = lbs_lookup(&tower[i], conn->current_lat, conn->current_lon);
        log_line(conn, "got tower location %f %f\n", tower[i].location.lat, tower[i].location.lng);

        if (tower[i].location.valid) {
            points[point_count].lat = tower[i].location.lat;
            points[point_count].lng = tower[i].location.lng;
            points[point_count].strength = lbs_information.entries[i].rssi;
            point_count++;
        }
    }

    time_t dt = date_to_time(package.date_time.year,
                             package.date_time.month,
                             package.date_time.day,
                             package.date_time.hour,
                             package.date_time.minute,
                             package.date_time.second);

    if (point_count > 0) {
        multilaterate_point result = multilaterate(points, point_count);
        conn->current_position_type = 1;
        conn->current_sat_count = point_count;
        move_to(conn, dt, 1, result.lat, result.lng);
        write_sat_count(conn, 1, point_count);
    }
}


void print_status_info(connection * conn, status_information status) {
    bool fortified = status.terminal_info & 1;
    bool acc = status.terminal_info & 2;
    bool charged = status.terminal_info & 4;
    uint8_t alm = status.terminal_info >> 3 & 7;
    bool located = status.terminal_info & 0x40;
    uint8_t voltage = 0;
    log_line(conn, "got status package, acc: %u  charged: %u alarm:%u located: %u power: %u voltage: %u terminal info: %u\n", fortified, acc, charged, alm, located, status.voltage, status.terminal_info);
    voltage = status.voltage;
    set_status(conn,
               voltage,
               status.gsm_strength * 25,
               conn->current_position_type,
               conn->current_sat_count);
    write_stat(conn, "battery_level", voltage);
    write_stat(conn, "signal", status.gsm_strength * 25);

    if (status.voltage < 20 && (( time(0) - conn->since_battalm) > 600)) {
        //low voltage alarm
        conn->since_battalm = time(0);
        conn->WARNING_FUNCTION(conn, "low battery");
        log_event(conn, "low battery");
    }
}



void print_heartbeat_info(connection * conn, heartbeat_information hbt) {
    hbt.voltage = SWAP_UINT16(hbt.voltage);
    log_line(conn, "got heartbeat package [status: %x voltage: %f rssi: %x]\n", hbt.status, hbt.voltage / 100.0f, hbt.rssi);
    float voltage = voltage_to_soc(hbt.voltage / 100.0f) ;
    set_status(conn, voltage, hbt.rssi * 25, conn->current_position_type, conn->current_sat_count );
    write_stat(conn, "battery_level", voltage);
    write_stat(conn, "signal", hbt.rssi * 25);

    if (voltage < 20 && (( time(0) - conn->since_battalm) > 600)) {
        //low voltage alarm
        conn->since_battalm = time(0);
        conn->WARNING_FUNCTION(conn, "low battery");
        log_event(conn,  "low battery");
    }
}

void process_alarm(connection * conn, information_package package) {
    gps_lbs_status_information info;
    uint8_t lbs_size = *(package.information + sizeof(gps_information));
    memcpy(&info.gps_info, package.information, sizeof(gps_information));
    memcpy(&info.lbs_info, package.information + 1 + sizeof(gps_information), min(lbs_size, sizeof(lbs_information)));
    memcpy(&info.status_info, package.information  + sizeof(gps_information) + lbs_size, sizeof(status_information));
    float lat = COORD(info.gps_info.lattitude);
    float lon = COORD(info.gps_info.longitude);
    time_t dt = date_to_time(package.date_time.year,
                             package.date_time.month,
                             package.date_time.day,
                             package.date_time.hour,
                             package.date_time.minute,
                             package.date_time.second);
    info.gps_info.status_and_course = SWAP_UINT16(info.gps_info.status_and_course);

    if (info.gps_info.status_and_course & 8192) {
        lat *= -1;
    }

    if (info.gps_info.status_and_course & 16384) {
        lon *= -1;
    }

    const char * typemsg = decode_alarm_code(info.status_info.alert);
    log_line(conn, "got alarm, info %x %x %x %x\n", info.status_info.terminal_info, info.status_info.voltage, info.status_info.gsm_strength, info.status_info.alert);
    log_line(conn, "got alarm, lat/long: %f %f\n", lat, lon);
    move_to(conn, dt, 0, lat, lon);
    log_event(conn, typemsg);
    print_status_info(conn, info.status_info);

    if (!is_alarm_disabled(conn, typemsg)) {
        conn->WARNING_FUNCTION(conn, typemsg);
    }
}

void process_information_package(connection * conn, data_packet packet) {
    information_package package;
    int dataLength = packet.header.length - 5;
    memcpy(&package, packet.data, min(dataLength, sizeof(date_time_info)));
    memcpy(package.information, packet.data + sizeof(date_time_info), dataLength - sizeof(date_time_info));
    log_line(conn, "got information package %x data starting with %x %x\n", packet.header.protocol_number, package.information[0], package.information[1]);

    switch (packet.header.protocol_number) {
        case 0x22:
        case 0x2D:
        case 0x10:	//gps information
            print_gps_info(conn, package);
            break;

        case 0x12:	//gps/lbs merged information	rm test 2>/dev/null
            log_line(conn, "got gps/lbs merged info \n");
            print_gps_info(conn, package);
            break;

        case 0x19:
            //lbs alarm
            break;

        case 0x28:
        case 0x2e:
        case 0x2c:
            process_lbs_info(conn, package);
            //LBS information
            break;

        case 0x16:	//gps/lbs status merged information ( alarm packet )
        case 0x26: //alarm packet specific for this device
        case 0x27:
            process_alarm(conn, package);
            break;

        default:
            log_line(conn, "weird information package, protocol number: %u\n", packet.header.protocol_number);
    }
}


void process_location_modular(connection * conn, uint8_t * data, size_t data_length) {
    size_t num_satelites = 0;
    size_t battery_level = 0;
    cell_tower tower = {0};
    float lat = 0;
    float lon = 0;
    uint16_t status_and_course = 0;
    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);
    uint16_t event_len = 0;
    size_t location_type = 0;
    bool location_valid = false;
    uint16_t msg_type = 0;
    uint16_t sz = 0;
    log_line(conn, "parsing modular location package.\n", sz);

    while (data_length > 6) {
        msg_type = SWAP_UINT16(*((uint16_t *) data));
        data += 2;
        sz = SWAP_UINT16(*((uint16_t *) data));
        data += 2;
        data_length -= 4;

        if (sz > data_length) {
            log_line(conn, "   invalid sub-package length: %u\n", sz);
            break;
        }

        switch (msg_type) {
            case 0x09:
            case 0x0a:
                if (sz < 1) {
                    log_line(conn, "   invalid sat info length: %u\n", sz);
                    break;
                }

                num_satelites = *data;
                data++;
                data_length--;
                break;

            case 0x11:
                if (sz < 11) {
                    log_line(conn, "   invalid LBS length: %u\n", sz);
                    break;
                }

                //public static CellTower from(int mcc, int mnc, int lac, long cid, int rssi)
                tower.mcc = SWAP_UINT16(*((uint16_t *) data));
                data += 2;
                tower.mnc = SWAP_UINT16(*((uint16_t *) data));
                data += 2;
                tower.lac = SWAP_UINT16(*((uint16_t *) data));
                data += 2;
                tower.cell_id = SWAP_UINT32(*((uint32_t *) data));
                data += 5;  //next byte is RSSI
                data_length -= 11;
                tower.location = lbs_lookup(&tower, conn->current_lat, conn->current_lon);
                log_line(conn, "   got tower location %f %f\n", tower.location.lat, tower.location.lng);

                if (tower.location.valid) {
                    location_valid = true;
                    lat = tower.location.lat;
                    lon = tower.location.lng;
                    num_satelites = 1;
                    location_type = 1;
                }

                break;

            case 0x18:
                if (sz < 2) {
                    log_line(conn, "   invalid battery level length: %u\n", sz);
                    break;
                }

                battery_level = SWAP_UINT16(*((uint16_t *) data)) * 0.01;
                data += 2;
                data_length -= 2;
                break;

            case 0x33:
                if (sz < 16) {
                    log_line(conn, "   invalid GPS data length: %u\n", sz);
                    break;
                }

                t = SWAP_UINT32(*((uint32_t *) data));
                data += 4;
                tm = *gmtime(&t);
                num_satelites = *data;
                data += 3; //skip altitude
                lat = COORD(*((uint32_t *) data));
                data += 4;
                lon = COORD(*((uint32_t *) data));
                data += 4;
                //speed = *data;
                data++;
                status_and_course = SWAP_UINT16(*((uint16_t *) data));
                data += 2;
                data_length -= 18;

                if (status_and_course & 8192) {
                    lat *= -1;
                }

                if (status_and_course & 16384) {
                    lon *= -1;
                }

                location_type = 0;
                location_valid = true;
                log_line(conn, "   got GPS location lat/lng: %f %f\n", lat, lon);
                break;

            default:
                data += sz;
                data_length = data_length >= sz ? (data_length - sz) : 0;
                break;
        }
    }

    if (location_valid) {
        //location_type
        move_to(conn, t, location_type, lat, lon);
        write_sat_count(conn, location_type, num_satelites);
    }

    send_data_packet(conn, create_response(0x70, *((uint16_t *) data)));
}

void process_v2(connection * conn) {
    uint8_t response[BUF_SIZE];
    size_t length = data_length(PACKET(conn));
    data_packet_v2_header header = PACKET(conn).v2_header;
    size_t information_size = header.information;
    size_t data_offs = 4 + information_size;

    if (length <= data_offs) {
        return;
    }

    conn->timeout_time = time(0) + JIMI_TIMEOUT;
    memset(response, 0, BUF_SIZE);
    memcpy(response, PACKET(conn).data + data_offs, min(length - data_offs, BUF_SIZE - 1));

    //this is a long packet. for some reason every 251 bytes they've added a header. we need to remove it from our data to parse it.
    for (int i = 1; i < (length - data_offs); i++) {
        if (i % 251 == 0) {
            memmove(&response[i], &response[i + 6], length - i - 6);
            length -= 6;
        }
    }

    //0x01 means ascii

    switch (header.protocol_number) {
        case 0x01:
            log_line(conn, "command response of length %u : %s\n", data_length(PACKET(conn)), response);
            log_command_response(conn, response);
            break;

        case 0x21:
            log_line(conn, "command response of length %u : %s\n", data_length(PACKET(conn)) - 1, response + 4);
            log_command_response(conn, response + 4);
            break;

        case 0x70:
            conn->device_extra = 1;
            process_location_modular(conn, response, length);
            break;
    }
}

data_packet create_time_response(uint16_t serial_number) {
    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);
    data_packet response = create_response(0x8a, serial_number);
    response.data[0] = tm.tm_year - 100;
    response.data[1] = tm.tm_mon + 1;
    response.data[2] = tm.tm_mday;
    response.data[3] = tm.tm_hour;
    response.data[4] = tm.tm_min;
    response.data[5] = tm.tm_sec;
    response.header.length += 6;
    response.footer.crc = crc16(response);
    return response;
}

void process_v1(connection * conn) {
    int dataLength =  data_length(PACKET(conn));
    conn->timeout_time = time(0) + JIMI_TIMEOUT;

    switch (PACKET(conn).header.protocol_number) {
        case 0x01:	//login information
            convert_imei(PACKET(conn).data, conn->imei);
            init_imei(conn);
            send_data_packet(conn, create_response(PACKET(conn).header.protocol_number, PACKET(conn).footer.serial_number));
            add_command(conn, "MUTESW,ON#");
            break;

        case 0x2C://lbs information
        case 0x10:	//gps information
        case 0x11:	//lbs information
        case 0x22:
        case 0x12:	//gps/lbs merged information
            process_information_package(conn, PACKET(conn));
            break;

        case 0x2D:
            process_information_package(conn, PACKET(conn));
            send_data_packet(conn, create_response(PACKET(conn).header.protocol_number, PACKET(conn).footer.serial_number));
            break;

        case 0x23:
            print_heartbeat_info(conn, *((heartbeat_information *)PACKET(conn).data));
            send_data_packet(conn, create_response(PACKET(conn).header.protocol_number, PACKET(conn).footer.serial_number));
            break;

        case 0x13:	//status information ( keepalive packet )
            print_status_info(conn, *((status_information *)PACKET(conn).data));
            send_data_packet(conn, create_response(PACKET(conn).header.protocol_number, PACKET(conn).footer.serial_number));
            break;

        case 0x16:	//gps/lbs status merged information ( alarm packet )
        case 0x27:
        case 0x28:
        case 0x26: //alarm packet specific for this device
            process_information_package(conn, PACKET(conn));
            break;

        case 0x2E: //alarm packet specific for this device
            process_information_package(conn, PACKET(conn));
            send_data_packet(conn, create_response(PACKET(conn).header.protocol_number, PACKET(conn).footer.serial_number));
            break;

        case 0x8a://time request
            send_data_packet(conn, create_time_response(PACKET(conn).footer.serial_number));
            break;

        default:
            break;
    }
}

void process_current_packet(connection * conn) {
    //test if crc and end bytes valid. not for a v2 packet because the checksum for that is weird.
    if (PACKET(conn).footer.stop_bit[0] != 0xD ||
            PACKET(conn).footer.stop_bit[1] != 0xA || (
                !is_v2(PACKET(conn)) && PACKET(conn).footer.crc != crc16(PACKET(conn)))) {
        log_line(conn, "invalid footer bytes or checksum.\n");
    }

    switch (PACKET(conn).header.start_bit[0]) {
        case 0x79:
            process_v2(conn);
            break;

        case 0x78:
            process_v1(conn);
            break;

        default:
            break;
    }
}
//add wifi support sometime

bool validate_startbits(data_packet p) {
    const uint8_t v1_start[] = {0x78, 0x78};
    const uint8_t v2_start[] = {0x79, 0x79};
    return memcmp(p.header.start_bit, v1_start, 2) == 0 || memcmp(p.header.start_bit, v2_start, 2) == 0 ;
}

void log_current_packet(connection * conn) {
    log_line(conn, "current packet: ");
    log_array(conn, ((uint8_t *)&PACKET(conn).header), sizeof(data_packet_header));
    log_array(conn, PACKET(conn).data, data_length(PACKET(conn)));
    log_array(conn, ((uint8_t *)&PACKET(conn).footer), sizeof(data_packet_footer));
    logprintf(conn, "\n");
}


void JIMI_process(void * c) {
    connection * conn = (connection *)c;
    conn->current_packet_valid = false;

    //if we've at least got a header
    if (conn->read_count > sizeof(data_packet_header)) {
        memcpy(&PACKET(conn).header, conn->recv_buffer, min(conn->read_count, sizeof(data_packet_header)));
        memcpy(&PACKET(conn).v2_header, conn->recv_buffer, min(conn->read_count, sizeof(data_packet_v2_header)));

        //with valid start bits
        if (!validate_startbits(PACKET(conn))) {
            log_line(conn, "invalid header starting with byte: %u\n", PACKET(conn).header.start_bit[0]);
            memmove(conn->recv_buffer, conn->recv_buffer + 1, conn->read_count - 1);
            conn->read_count -= 1;

        } else {
            size_t dataLength = data_length(PACKET(conn));
            size_t header_size = (is_v2(PACKET(conn)) ? sizeof(data_packet_v2_header) : sizeof(data_packet_header));
            size_t totalSize = header_size +  dataLength +  sizeof(data_packet_footer);

            //and maybe enough data to reconstruct our entire packet
            if (conn->read_count >= totalSize) {
                memcpy(PACKET(conn).data, conn->recv_buffer + header_size, dataLength);
                memcpy(&PACKET(conn).footer, conn->recv_buffer + header_size + dataLength, sizeof(data_packet_footer));
                log_line(conn, "packet recieved.\n");
                log_current_packet(conn);
                process_current_packet(conn);
                memmove(conn->recv_buffer, conn->recv_buffer + totalSize, conn->read_count - totalSize);
                conn->read_count -= totalSize;
            }
        }

    } else {
        //if we're idle, and 5 minutes have passed we should get status from our device
        if (conn->read_count == 0 && ( time(0) - conn->since_last_status ) > 300) {
            conn->COMMAND_FUNCTION(conn, "STATUS#");
            conn->since_last_status = time(0);
        }
    }
}

void JIMI_warn(void * c, const char * reason) {
    add_command((connection *) c, WARNONCE);
}

void JIMI_do_nothing(void * c, const char * reason) {
}

void JIMI_identify(void * c) {
    connection * conn = (connection *)c;

    if (conn->read_count > sizeof(data_packet_header)) {
        memcpy(&PACKET(conn).header, conn->recv_buffer, min(conn->read_count, sizeof(data_packet_header)));
        memcpy(&PACKET(conn).v2_header, conn->recv_buffer, min(conn->read_count, sizeof(data_packet_v2_header)));
    }

    if (validate_startbits(PACKET(conn))) {
        fprintf(stdout, "  device type is JIMI\n");
        conn->PROCESS_FUNCTION = JIMI_process;
        conn->COMMAND_FUNCTION = JIMI_send_command;
        conn->WARNING_FUNCTION = JIMI_warn;
        conn->AUDIO_WARNING_FUNCTION = JIMI_warn;
        conn->MOTOR_WARNING_FUNCTION = JIMI_do_nothing;
    }
}

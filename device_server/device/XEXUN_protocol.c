#include <time.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>

#include "../util.h"
#include "../config.h"
#include "../crc16.h"
#include "../string.h"
#include "../connection.h"
#include "../logfiles.h"
#include "../geofence.h"
#include "../commands.h"
#include "../wifi_lookup.h"
#include "../multilaterate.h"
#include "XEXUN_protocol.h"

const uint8_t xex_start[] = {0xfa, 0xaf};
#define XEXUN_TIMEOUT 40

size_t unescape_packet(unsigned char * packet, size_t length) {
    static const uint8_t xex_a[] = {0xfb, 0xbf, 0x01};
    static const uint8_t xex_a_dest[] = {0xfa, 0xaf};
    static const uint8_t xex_b[] = {0xfb, 0xbf, 0x02};
    static const uint8_t xex_b_dest[] = {0xfb, 0xbf};
    length = binary_replace(xex_a, 3, xex_a_dest, 2, packet, length, BUF_SIZE);
    return binary_replace(xex_b, 3, xex_b_dest, 2, packet, length, BUF_SIZE);
}

size_t escape_packet(unsigned char * packet, size_t length) {
    static const uint8_t xex_a_dest[] = {0xfb, 0xbf, 0x01};
    static const uint8_t xex_a[] = {0xfa, 0xaf};
    static const uint8_t xex_b_dest[] = {0xfb, 0xbf, 0x02};
    static const uint8_t xex_b[] = {0xfb, 0xbf};
    length = binary_replace(xex_a, 2, xex_a_dest, 3, packet, length, BUF_SIZE);
    return binary_replace(xex_b, 2, xex_b_dest, 3, packet, length, BUF_SIZE);
}

uint16_t xex_checksum(uint8_t * data, size_t len) {
    uint32_t sum = 0;

    for (; len > 1; len -= 1) {
        sum += *data++;

        if (sum & 0x80000000) {
            sum = (sum & 0xffff) + (sum >> 16);
        }
    }

    if (len == 1) {
        uint16_t i = 0;
        *(uint8_t *)(&i) = *(uint8_t *)data;
        sum += i;
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    sum = (sum == 0xffff) ? sum : ~sum;
    return SWAP_UINT16(sum);
}


xexun_data xex_make_packet(connection * conn, uint16_t packet_serial, uint16_t message_id, size_t data_length) {
    xexun_data ret;
    memset(&ret, 0, sizeof(ret));
    memcpy(ret.start_bit, xex_start, 2);
    ret.message_id = SWAP_UINT16(message_id);
    ret.serial_number = SWAP_UINT16(packet_serial);
    ret.message_id = SWAP_UINT16(message_id);
    ret.body_attributes = SWAP_UINT16(data_length);
    memcpy(ret.data + data_length, xex_start, 2);
    IMEI_TO_BYTES(conn->imei, ret.imei);
    return ret;
}

xexun_data compute_checksum(xexun_data packet) {
    packet.ecc = xex_checksum(((uint8_t *)&packet) + 18, SWAP_UINT16(packet.body_attributes));//skip the last few bytes as well
    return packet;
}

void xex_send_packet(connection * conn, xexun_data data) {
    uint8_t tmp_buffer[BUF_SIZE];
    size_t data_size = 18 + SWAP_UINT16(data.body_attributes) + 2;
    memcpy(tmp_buffer, ((uint8_t *)&data) + 2, data_size - 4);
    data_size = escape_packet(tmp_buffer, data_size - 4);
    memcpy(conn->send_buffer + conn->send_count, xex_start, 2);
    conn->send_count += 2;
    memcpy(conn->send_buffer + conn->send_count, tmp_buffer, data_size);
    conn->send_count += data_size;
    memcpy(conn->send_buffer + conn->send_count, xex_start, 2);
    conn->send_count += 2;
    logprintf(conn, "sending response: ");

    for (size_t i = 0; i < conn->send_count; i++) {
        logprintf(conn, " 0x%x", conn->send_buffer[i]);
    }

    logprintf(conn, "\n");
}


bool XEXUN_send_command( void * c, const char * cmd) {
    connection * conn = (connection *)c;

    if (!conn->can_log ) {
        return false;
    }

    xexun_data d = xex_make_packet(conn, conn->packet_index++, 0x07, strlen(cmd) + 1);
    memcpy(d.data, cmd, strlen(cmd) + 1);
    d = compute_checksum(d);

    if (memcmp(conn->previous_command_packet + 18, ((uint8_t *)&d) + 18, SWAP_UINT16(d.body_attributes) + 2) == 0 ) {
        return true;
    }

    xex_send_packet(conn, d);
    return false;
}

void XEXUN_send_response( void * c, uint16_t packet_index) {
    connection * conn = (connection *)c;

    if (!conn->can_log) {
        return;
    }

    xexun_data d = xex_make_packet(conn, packet_index, 0x14, 1);
    d.data[0] = 1;
    d = compute_checksum(d);
    xex_send_packet(conn, d);
}

const char * xex_translate_alarm(alarm_data * d) {
    if (d->ankle_bracelet_tag_distance) {
        return "tag out of range";
    }

    if (d->ankle_bracelet_tag_offline) {
        return "tag offline";
    }

    if (d->car_power_down) {
        return "car turned off";
    }

    if (d->charging) {
        return "charging";
    }

    if ( d->check_switch) {
        return  "check switch";
    }

    if (d->connection) {
        return "connection";
    }

    if ( d->coordinates_out_of_bounds) {
        return "coordinates out of bounds";
    }

    if (d->disconnect) {
        return "disconnect";
    }

    if ( d->dismantle) {
        return "dismantle/removal";
    }

    if ( d->fall_down) {
        return "fall";
    }

    if (  d->gravity_alarm) {
        return "gravity";
    }

    if (  d->heart_rate) {
        return "heart rate";
    }

    if (   d->movement) {
        return "movement";
    }

    if (   d->not_touched) {
        return "not touched";
    }

    if (    d->optical_switch) {
        return "optical switch";
    }

    if (d->static_alm) {
        return "static";
    }

    if (  d->touch) {
        return "touch";
    }

    if (d->turn_on) {
        return "turn on";
    }

    if (d->sos) {
        return "SOS";
    }

    return "unknown";
}

size_t xex_process_alarm(connection * c, alarm_data * d) {
    *((uint32_t *)d) = SWAP_UINT32(*((uint32_t *)d));
    log_line(c, "got alarm bytes: %x\n", *((uint32_t *)d));
    char * msg = xex_translate_alarm(d);
    log_event(c,   msg);
    return sizeof(alarm_data);
}

//handle east/west correctly. need new formula.
float parseCoordinate(float f) {
    char buf[16] = {0};
    bool neg = f < 0;
    f *= (neg ? -1 : 1);
    float n = f * 1000000;
    size_t idx = (f <= 10000.0f) + (f <= 1000.0f);
    memset(buf, '0', idx);
    gcvt (n, 10,  buf + idx);
    return (neg ? -1 : 1) * ( parse_int(buf, 3) + (parse_int(buf + 3, strlen(buf) - 3) / (6 * (pow(10, strlen(buf) - 4)))));
}

size_t process_gps_data(connection * c, time_t timestamp,  position_packet * packet, gps_data * d) {
    time_t t = timestamp;
    struct tm tm = *gmtime(&t);
    char buf[16] = {0};
    double speed = 0;
    double over_time = 0;
    SWAP_FLOAT(d->lat);
    SWAP_FLOAT(d->lon);
    d->lat = parseCoordinate(d->lat);
    d->lon = parseCoordinate(d->lon);
    log_line(c, "got GPS position lat/long/timestamp: %f/%f/%llu\n", d->lat, d->lon, timestamp);
    set_status(c, packet->batt_attributes & 0xFF, packet->csq * 3.33f, 0, d->sat_count);
    move_to(c, t, 0, d->lat, d->lon);
    write_stat(c, "gps_sats", d->sat_count);
    write_stat(c, "signal", packet->csq * 3.33f);
    c->device_time = time(0);
    return sizeof(gps_data);
}

size_t process_speed_data(connection * c, speed_data * d) {
    c->current_speed = SWAP_UINT16( d->speed) / 10;
    log_line(c, "got speed: %u\n", d->speed);
    return sizeof(speed_data);
}

size_t process_lbs_data(connection * c, position_packet * packet, lbs_data * d) {
    if (d->network_count > 6) {
        d->network_count = 6;
        log_line(c, "  invalid number of lbs networks: %u\n", d->network_count);

    } else {
        log_line(c, "lbs network count: %u\n", d->network_count);
    }

    return d->network_count * sizeof(lbs_network) + 1;
}

size_t process_wifi_data(connection * c, time_t timestamp, position_packet * packet, wifi_data * d) {
    time_t t = timestamp;
    struct tm tm = *gmtime(&t);
    double speed = 0;
    double over_time = 0;

    if (d->network_count > 6) {
        d->network_count = 6;
        log_line(c, "  invalid number of wifi networks: %u\n", d->network_count);

    } else {
        log_line(c, "wifi count: %u\n", d->network_count);
    }

    for (size_t i = 0; i < d->network_count; i++) {
        log_line(c, "wifi network [%u]: %x:%x:%x:%x:%x:%x with rssi %u\n", i,
                 d->networks[i].mac_addr[0],
                 d->networks[i].mac_addr[1],
                 d->networks[i].mac_addr[2],
                 d->networks[i].mac_addr[3],
                 d->networks[i].mac_addr[4],
                 d->networks[i].mac_addr[5],
                 d->networks[i].rssi
                );
    }

    wifi_network networks[7];

    for (int i = 0; i < d->network_count; i++) {
        memset(&networks[i], 0, sizeof(wifi_network));
        memcpy(networks[i].mac_addr, d->networks[i].mac_addr, 6);
        networks[i].rssi = d->networks[i].rssi;
    }

    location_result result =  wifi_lookup(networks, d->network_count);

    if (result.valid) {
        move_to(c, t, 2, result.lat, result.lng);
        set_status(c,  SWAP_UINT16(packet->batt_attributes) & 0xFF, packet->csq * 3.33f, 2, d->network_count);
        c->device_time = time(0);
    }

    //multilaterate here
    return d->network_count * sizeof(xex_wifi_network) + 1;
}

size_t process_tof_data(connection * c, tof_data * d) {
    if (d->tof_count > 4) {
        d->tof_count = 4;
        log_line(c, "  invalid number of tof networks: %u\n", d->tof_count);

    } else {
        log_line(c, "tof network count: %u\n", d->tof_count);
    }

    return d->tof_count * sizeof(tof_network) + 1;
}

size_t process_differential_gps(connection * c, differential_gps_data * d) {
    return sizeof(differential_gps_data);
}

size_t process_position_data(connection * c, time_t timestamp, position_packet * packet, position_data * d) {
    size_t offset = 0;
    log_line(c, "got position data, type: %u ", d->data_type);

    if (d->data_type & 1) {
        offset += process_gps_data(c, timestamp, packet, (gps_data *)(d->data + offset));
    }

    if (d->data_type & 2) {
        offset += process_wifi_data(c, timestamp, packet, (wifi_data *)(d->data + offset));
    }

    if (d->data_type & 4) {
        offset += process_lbs_data(c, packet, (lbs_data *)(d->data + offset));
    }

    if (d->data_type & 8) {
        //manual says n/a
    }

    if (d->data_type & 16) {
        offset += process_speed_data(c, (speed_data *)(d->data + offset));
    }

    if (d->data_type & 32) {
        offset += process_differential_gps(c, (d->data + offset));
    }

    return offset + 1;
}

size_t process_human_body_data(connection * c, time_t timestamp, human_body_data * d) {
    d->step_count = SWAP_UINT16(d->step_count);
    write_stat(c, "heart_rate", d->heart_rate);
    log_line(c, "body data [ hr, diastole,systole, steps, oxygen]: %u %u %u %u %u\n", d->heart_rate, d->low_pressure, d->high_pressure, d->step_count, d->blood_oxygen);
    return sizeof(human_body_data);
}

size_t process_temperature_data(connection * c, time_t timestamp, temperature_data * d) {
    SWAP_FLOAT(d->temperature);
    write_stat(c, "body_temperature", d->temperature);
    log_line(c, "body temperature %f \n", d->temperature);
    return sizeof(temperature_data);
}

size_t process_extended_data(connection * c, time_t timestamp, extended_data * d) {
    d->extended_data_type = SWAP_UINT32(d->extended_data_type);
    size_t offset = 0;

    if (d->extended_data_type & 1) {
        offset +=  process_temperature_data(c, timestamp, (temperature_data *)(d->data + offset));
    }

    if (d->extended_data_type & 2) {
        offset +=  process_human_body_data(c, timestamp, (human_body_data *)(d->data + offset));
    }

    return offset + 1;
}

size_t process_fingerprint_data(connection * c, fingerprint_data * d) {
    return sizeof(fingerprint_data);
}

size_t process_version_data(connection * c, version_data * p) {
    return sizeof(version_data);
}

size_t process_tof_parameters(connection * c, tof_parameters * p) {
    return sizeof(tof_parameters);
}

size_t process_nfc_data(connection * c, nfc_data * p) {
    return 1 + (p->number_of_cards * 3);
}

size_t process_position(connection * c, position_packet * p) {
    size_t offset = 0;
    log_line(c, "got position type:%x battery percent: %u\n", p->position_type, SWAP_UINT16(p->batt_attributes) & 0xFF);
    write_stat(c,  "battery_level", SWAP_UINT16(p->batt_attributes) & 0xFF);
    p->timestamp = SWAP_UINT32(p->timestamp);

    if (p->position_type & 1) {
        offset += xex_process_alarm(c, (alarm_data *)(p->data + offset));
    }

    if (p->position_type & 2) {
        offset += process_position_data(c, p->timestamp, p, (position_data *)(p->data + offset));
    }

    if (p->position_type & 8) {
        offset += process_fingerprint_data(c, (fingerprint_data *)(p->data + offset));
    }

    if (p->position_type & 16) {
        offset += process_version_data(c, (version_data *)(p->data + offset));
    }

    if (p->position_type & 32) {
        offset += process_tof_parameters(c, (tof_parameters *)(p->data + offset));
    }

    if (p->position_type & 64) {
        offset += process_nfc_data(c, (nfc_data *)(p->data + offset));
    }

    if (p->position_type & 0x80) {
        offset += process_extended_data(c, p->timestamp, (extended_data *)(p->data + offset)); //extended data
    }

    return offset + 1;
}

void XEXUN_process(void * vp) {
    connection * conn = (connection *)vp;
    conn->current_packet_valid = false;

    //if we've at least got a header
    if (conn->read_count > 5) {
        size_t start = 0;
        size_t end = 0;
        bool end_found = false;

        for (; (start + 1) < conn->read_count; start++) {
            if (memcmp(conn->recv_buffer + start, xex_start, 2) == 0) {
                break;
            }
        }

        for (end = start + 2; (end + 1) < conn->read_count; end++) {
            if (memcmp(conn->recv_buffer + end, xex_start, 2) == 0) {
                end_found = true;
                break;
            }
        }

        size_t len = end - start + 2;

        if (end_found) {
            conn->timeout_time = time(0) + XEXUN_TIMEOUT;
            memcpy(conn->current_packet, conn->recv_buffer + start, len);
            //log the current packet
            log_line(conn, "recieved packet: ");
            log_array(conn, conn->current_packet + start, len);
            logprintf(conn, "\n");

            if (len >= 20) {
                len = unescape_packet(conn->current_packet, len);
                xexun_data * hp = (xexun_data *)conn->current_packet;
                bool same_as_previous = memcmp(conn->previous_packet + 18, ((uint8_t *)hp) + 18, SWAP_UINT16(hp->body_attributes) + 2) == 0;
                hp->message_id = SWAP_UINT16(hp->message_id); //swap endianness
                hp->serial_number = SWAP_UINT16(hp->serial_number); //swap endianness

                if (!same_as_previous ) {
                    if (hp->message_id == 0x07) {
                        memcpy(conn->previous_command_packet, conn->current_packet, len);
                    }

                    memcpy(conn->previous_packet, conn->current_packet, len);

                    if (xex_checksum(conn->current_packet + 18, len - 20) != hp->ecc) {
                        log_line(conn, "invalid checksum: %x %x\n", xex_checksum(conn->current_packet + 18, len - 20), hp->ecc);
                    }

                    if (!conn->can_log) {
                        convert_imei(hp->imei, conn->imei);
                        init_imei(conn);
                    }

                    if (hp->message_id == 0x14) {
                        position_header * ph = (position_header *)hp->data;
                        size_t data_offset = 1 + (ph->packet_count * 2);

                        for (size_t n = 0; n < ph->packet_count; n++) {
                            ph->packet_size[n] = SWAP_UINT16(ph->packet_size[n]); //swap to correct endianness
                        }

                        //test  checksum
                        //server commands
                        for (size_t n = 0; n < ph->packet_count; n++) {
                            log_line(conn, "process position packet starting at: %u\n", data_offset);

                            if ((ph->packet_size[n] > 512) || (data_offset + ph->packet_size[n] >= BUF_SIZE)) {
                                log_line(conn, "  invalid packet size/offset: %u/%u\n", ph->packet_size[n], data_offset);
                                break;
                            }

                            position_packet * pp = ( position_packet *)(hp->data + data_offset);
                            process_position(conn, pp);
                            //test for valid size
                            data_offset += ph->packet_size[n];
                        }
                    }
                }

                if (hp->message_id == 0x14) {
                    XEXUN_send_response(conn, hp->serial_number);
                }
            }

//print packet in std output
            memmove(conn->recv_buffer, conn->recv_buffer + end + 2, conn->read_count - end - 2);
            conn->read_count -= end;
            conn->read_count -= 2;
        }
    }

    //if we're idle, and 5 minutes have passed we should get status from our device
    if (conn->read_count == 0 && ( time(0) - conn->since_last_status ) > 600) {
        conn->since_last_status = time(0);
    }
}

void XEXUN_warn(void * vp, const char * reason) {
    char buffer[BUF_SIZE] = {0};
    sprintf(buffer, "mg=%s", reason);
    XEXUN_send_command(vp, buffer);
}

void XEXUN_do_nothing(void * vp, const char * reason) {
}

void XEXUN_identify(void * vp) {
    connection * conn = (connection *)vp;

    if (memcmp(conn->recv_buffer, xex_start, 2) == 0) {
        fprintf(stdout, "   device is xexun\n");
        conn->PROCESS_FUNCTION = XEXUN_process;
        conn->COMMAND_FUNCTION = XEXUN_send_command;
        conn->WARNING_FUNCTION = XEXUN_warn;
        conn->MOTOR_WARNING_FUNCTION = XEXUN_warn;
        conn->AUDIO_WARNING_FUNCTION = XEXUN_do_nothing;
        conn->log_disconnect = false;
        conn->log_connect = false;
    }
}

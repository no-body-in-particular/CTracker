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
#include "../web_geolocate.h"
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

/*
 * 0x94, information transfer: everything that is not a location. The sub-type is the byte
 * straight after the protocol number - v2_header.information - and the payload follows it,
 * which is data[0] onwards. That is not the "information as a length" arrangement the other
 * v2 packets use, so this is parsed from the packet rather than from the de-chunked
 * "response" buffer the caller builds.
 *
 * The manual documents a server reply for this packet but the copy here does not render its
 * layout, so nothing is sent back - which is also what happened before, since 0x94 fell
 * through to the default. Parsing it is the improvement; answering it needs a spec.
 */
static void process_information_transfer(connection * conn, size_t length) {
    uint8_t info_type = PACKET(conn).v2_header.information;
    const uint8_t * payload = PACKET(conn).data;

    switch (info_type) {
        case 0x00: {
            //two bytes of hex, hundredths of a volt
            if (length < 2) {
                break;
            }

            float volts = ((payload[0] << 8) | payload[1]) / 100.0f;
            log_line(conn, "external battery: %.2fV\n", volts);
            write_stat(conn, "external_battery", volts);
            break;
        }

        case 0x0A: {
            /*
             * Not ascii. The payload is binary coded decimal - a real one from a device reads
             * 03 59 85 70 82 36 67 58 ... 89 66 05 19 12 40 80 52 45 2f, which printed as
             * characters is the line noise this used to log. Written out as hex it is the
             * identifiers themselves, the iccid being the 8966... at the end.
             */
            char iccid[160] = {0};
            size_t w = 0;

            for (size_t i = 0; i < length && w + 3 < sizeof(iccid); i++) {
                w += snprintf(iccid + w, sizeof(iccid) - w, "%02X", payload[i]);
            }

            log_line(conn, "iccid packet: %s\n", iccid);
            break;
        }

        case 0x1B: {
            //an rfid card, sent as ascii - "RFID:008FB2BEBA39"
            char card[96] = {0};
            snprintf(card, sizeof(card), "%.*s", (int)min(length, sizeof(card) - 1), payload);
            strip_unprintable(card);
            log_line(conn, "rfid: %s\n", card);
            log_command_response(conn, (unsigned char *)card);
            break;
        }

        case 0x04: {
            //terminal status synchronisation: ascii "ID=value;ID=value;..."
            char status[BUF_SIZE] = {0};
            snprintf(status, sizeof(status), "%.*s", (int)min(length, sizeof(status) - 1), payload);
            strip_unprintable(status);
            log_line(conn, "terminal status: %s\n", status);
            break;
        }

        case 0x09:
            log_line(conn, "satellite status packet, %u bytes\n", (unsigned)length);
            break;

        default:
            log_line(conn, "information transfer type 0x%02X, %u bytes\n", info_type, (unsigned)length);
            break;
    }
}

/*
 * A short printable rendering of a payload, for the diagnostics below. Several of these
 * packets carry ASCII (parameter dumps, command echoes) and a hex blob in the log tells you
 * far less than the first few readable characters do.
 */
static const char * preview_ascii(const uint8_t * data, size_t length, char * out, size_t out_size) {
    size_t w = 0;

    for (size_t i = 0; i < length && w + 1 < out_size; i++) {
        out[w++] = (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.';
    }

    out[w] = 0;
    return out;
}

/*
 * The address request, in both the forms this protocol family uses.
 *
 * 0x1A and 0x2A carry a GPS fix: date and time, satellite count, latitude, longitude, speed
 * and course, then the phone number and a language flag. 0x17 and 0xA7 carry the serving cell
 * instead - mcc, mnc, lac and cell id - for a device with no fix. Either way the device is
 * asking the server to turn a position into a street address and send it to that number.
 *
 * The cell form is worth having for more than the address: it tells us where the device is,
 * which is a position we can resolve and record even when the request itself cannot be
 * answered.
 *
 * The reply goes back as 0x17 for Chinese or 0x97 for English, framed as the specification
 * describes: a length byte, four bytes of server flag, then ALARMSMS&&<address>&&<phone>##.
 * That frame is what traccar sends too, which is worth something given it is deployed against
 * far more of these devices than this server will ever see.
 */
static void send_address_response(connection * conn, const char * text, uint8_t protocol) {
    data_packet response = get_basic_packet();
    size_t textlen = strlen(text);

    if (textlen > sizeof(response.data) - 8) {
        textlen = sizeof(response.data) - 8;
    }

    response.header.protocol_number = protocol;
    //length of what sits between the server flag and the serial number
    response.data[0] = (uint8_t)textlen;
    //server flag bit, which nothing here uses
    response.data[1] = 0;
    response.data[2] = 0;
    response.data[3] = 0;
    response.data[4] = 0;
    memcpy(response.data + 5, text, textlen);
    response.header.length = (uint8_t)min(255, 5 + 5 + textlen);
    response.footer.serial_number = PACKET(conn).footer.serial_number;
    response.footer.crc = crc16(response);
    send_data_packet(conn, response);
}

void process_address_request(connection * conn, bool cell_form) {
    char number[32] = {0};
    char address[BUF_SIZE] = {0};
    char line[BUF_SIZE] = {0};
    char text[BUF_SIZE] = {0};
    size_t length = data_length(PACKET(conn));
    const uint8_t * d = PACKET(conn).data;
    size_t w = 0;
    size_t phone_offset;
    float lat = conn->current_lat;
    float lon = conn->current_lon;
    bool have_position = (lat != 0 || lon != 0);

    if (cell_form) {
        /*
         * mcc(2) mnc(1 or 2) lac(2) cellid(3). The top bit of the mcc says how wide the mnc
         * is - set means two bytes - which is how a country whose codes need two is told
         * apart from one whose codes do not.
         */
        if (length < 8) {
            log_line(conn, "cell address request too short: %u bytes\n", (unsigned)length);
            return;
        }

        uint16_t mcc_raw = (uint16_t)((d[0] << 8) | d[1]);
        size_t mnc_len = (mcc_raw & 0x8000) ? 2 : 1;
        cell_tower tower;
        memset(&tower, 0, sizeof(tower));
        tower.mcc = mcc_raw & 0x7fff;
        tower.mnc = d[2];

        if (mnc_len == 2) {
            tower.mnc = (uint8_t)((d[2] << 8) | d[3]);
        }

        size_t p = 2 + mnc_len;
        tower.lac = (uint16_t)((d[p] << 8) | d[p + 1]);
        tower.cell_id = (uint32_t)((d[p + 2] << 16) | (d[p + 3] << 8) | d[p + 4]);
        phone_offset = p + 5;
        log_line(conn, "address request from cell mcc %u mnc %u lac %u cell %u\n",
                 (unsigned)tower.mcc, (unsigned)tower.mnc, (unsigned)tower.lac,
                 (unsigned)tower.cell_id);
        location_result found = geolocate_tower(&tower);

        if (found.valid) {
            lat = found.lat;
            lon = found.lng;
            have_position = true;
            //a request that carries a cell is also a position report, so record it
            move_to(conn, time(0), 2, lat, lon);
        }

    } else {
        //datetime(6) sats(1) lat(4) lon(4) speed(1) course and status(2)
        phone_offset = 18;
    }

    for (size_t i = 0; i < 21 && phone_offset + i < length && w + 1 < sizeof(number); i++) {
        uint8_t c = d[phone_offset + i];

        if (c == 0) {
            break;
        }

        if ((c >= '0' && c <= '9') || c == '+') {
            number[w++] = (char)c;
        }
    }

    number[w] = 0;

    //the language flag sits straight after the number; 1 is Chinese, anything else English
    uint8_t language = 2;

    if (phone_offset + 22 < length) {
        language = d[phone_offset + 21 + 1];
    }

    if (!have_position) {
        snprintf(address, sizeof(address), "NA");
        snprintf(line, sizeof(line), "address request for %s, but no position is known yet",
                 w ? number : "an unnamed number");

    } else if (here_reverse_geocode(lat, lon, address, sizeof(address))) {
        snprintf(line, sizeof(line), "address request for %s: %s",
                 w ? number : "an unnamed number", address);

    } else {
        //no geocoder to hand, so answer with the position itself rather than nothing
        snprintf(address, sizeof(address), "%f,%f", lat, lon);
        snprintf(line, sizeof(line), "address request for %s at %s - no street address available",
                 w ? number : "an unnamed number", address);
    }

    log_line(conn, "%s\n", line);
    log_command_response(conn, (unsigned char *)line);
    snprintf(text, sizeof(text), "ALARMSMS&&%s&&%s##", address, w ? number : "0");
    send_address_response(conn, text, language == 1 ? 0x17 : 0x97);
}

void process_v2(connection * conn) {
    uint8_t response[BUF_SIZE];
    size_t length = data_length(PACKET(conn));
    data_packet_v2_header header = PACKET(conn).v2_header;
    size_t information_size = header.information;
    size_t data_offs = 4 + information_size;

    /*
     * 0x94 first, because for it the information byte is a sub-type and not an offset into
     * the payload - and the length checks below read it as an offset. An rfid packet carries
     * sub-type 0x1b, so those checks worked out that its nineteen bytes of payload begin at
     * byte thirty one and threw it away. Every 0x94 whose sub-type happens to be a larger
     * number than its payload is long went the same way: rfid, and anything else above 0x13.
     */
    if (header.protocol_number == 0x94) {
        conn->timeout_time = time(0) + JIMI_TIMEOUT;
        process_information_transfer(conn, length);
        return;
    }

    if (length <= data_offs) {
        //Not necessarily malformed - some devices send a long packet whose payload begins
        //straight after the length, with no information field, so the byte read as
        //"information" is really the first byte of an IMEI. Either way the packet is dropped,
        //and it used to be dropped in silence.
        log_line(conn, "v2 packet 0x%02X discarded: %u bytes of data, but its information "
                 "field claims the payload starts at %u\n",
                 header.protocol_number, (unsigned)length, (unsigned)data_offs);
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

        default: {
            //This switch had no default at all, so anything unrecognised left no trace.
            char text[128];
            log_line(conn, "unhandled v2 protocol number 0x%02X, %u bytes: %s\n",
                     header.protocol_number, (unsigned)length,
                     preview_ascii(response, min(length, sizeof(text) - 1), text, sizeof(text)));
            break;
        }
    }
}

/*
 * The answer to a 0x1F synchronisation package: four bytes of UTC seconds and two reserved,
 * as the JI09 document gives it. A terminal sends one every twenty four hours after it
 * registers and goes unanswered without this. Big endian, like the protocol's other multi
 * byte fields.
 */
data_packet create_sync_response(uint16_t serial_number) {
    data_packet response = create_response(0x1F, serial_number);
    uint32_t now = (uint32_t)time(NULL);
    response.data[0] = (uint8_t)(now >> 24);
    response.data[1] = (uint8_t)(now >> 16);
    response.data[2] = (uint8_t)(now >> 8);
    response.data[3] = (uint8_t)(now);
    response.data[4] = 0;
    response.data[5] = 0;
    response.header.length += 6;
    response.footer.crc = crc16(response);
    return response;
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

        /*
         * 0x1F, the daily synchronisation. The terminal sends the time it believes it is and
         * the server answers with the time it actually is. Worth logging the difference: the
         * archive holds readings stamped years in the past by devices whose clock was wrong
         * before they synchronised, and a device that is far out says so here first.
         */
        case 0x1F: {
            size_t len = data_length(PACKET(conn));

            if (len >= 6) {
                const uint8_t * d = PACKET(conn).data;
                time_t claimed = date_to_time(d[0], d[1], d[2], d[3], d[4], d[5]);
                long skew = (long)(time(0) - claimed);

                if (skew > 300 || skew < -300) {
                    log_line(conn, "device clock is %ld seconds out, sending the time\n", skew);

                } else {
                    log_line(conn, "device asked for the time, clock is %ld seconds out\n", skew);
                }
            }

            send_data_packet(conn, create_sync_response(PACKET(conn).footer.serial_number));
            break;
        }

        /*
         * 0x2A is the address request. 0x17 carries one too on the devices seen here - the
         * packet holds a phone number in ASCII - though other builds of this protocol reuse
         * 0x17 for wifi or rfid data, so process_address_request only speaks up when it can
         * actually find a number. 0x97 is deliberately absent: that is the type the *server*
         * replies with, not one a device sends.
         */
        /*
         * 0x1A is the address request: the GT06 spec calls it "GPS, Phone Number Querying
         * Address Information Package", and its layout is a location packet with a 21 byte
         * phone number and a 2 byte language flag appended. 0x2A is the same request in the
         * variant traccar implements.
         *
         * 0x17 used to be handled here as if a device sent it. It does not: the spec has
         * 0x17 as the server's *Chinese* address response and 0x97 as the English one, both
         * server to terminal. A device that sends 0x17 is speaking one of the dialects that
         * reuse it for wifi or rfid data, which is not an address request at all, so it now
         * falls through to the unhandled-protocol logging where it can be identified.
         */
        //carrying a gps fix
        case 0x1A:
        case 0x2A:
            process_address_request(conn, false);
            break;

        /*
         * Carrying the serving cell instead. 0x17 was taken out of this list earlier on the
         * strength of the base GT06 document, which has 0x17 only as the server's Chinese
         * address reply. The JM-LL301 document for this same family has it as the terminal's
         * LBS address request, and the packet this server captured matches that layout field
         * for field - mcc 204, mnc 8, then "+31619036989" in ascii - so both are true: the
         * number is used in each direction. Reading one document was not enough.
         */
        case 0x17:
        case 0xA7:
            process_address_request(conn, true);
            break;

        default: {
            //Same blind spot as the v2 switch above: an unknown protocol number looked
            //exactly like no packet at all, which is what kept 0x17 out of sight.
            char text[128];
            size_t len = data_length(PACKET(conn));
            log_line(conn, "unhandled v1 protocol number 0x%02X, %u bytes: %s\n",
                     PACKET(conn).header.protocol_number, (unsigned)len,
                     preview_ascii(PACKET(conn).data, min(len, sizeof(text) - 1), text, sizeof(text)));
            break;
        }
    }
}

void process_current_packet(connection * conn) {
    //Both framings are checked now - see crc16(). A failure is still only reported rather
    //than acted on: the parser has always carried on regardless, and starting to drop
    //packets on a checksum this server has only just begun computing would be a poor way to
    //find out it had it wrong.
    if (PACKET(conn).footer.stop_bit[0] != 0xD ||
            PACKET(conn).footer.stop_bit[1] != 0xA ||
            PACKET(conn).footer.crc != crc16(PACKET(conn))) {
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

/*
 * The bytes as they arrived, which for a long packet this did not manage.
 *
 * data_packet_header is four bytes - two start, a one byte length, a protocol number - so
 * printing it covers wire bytes 0 to 3. That is right for a 0x78 packet. A 0x79 packet has a
 * two byte length, so its protocol number and information byte are wire bytes 4 and 5, and
 * both were skipped: the dump jumped from the length straight to the payload at byte 6.
 *
 * Which makes the fifth byte printed look exactly like a protocol number without being one.
 * I read four different payload bytes out of these logs as protocol numbers and went looking
 * for message types that do not exist. The packets were ordinary 0x94 information transfers
 * whose payload happens to start with "ALM1=", an IMEI, or an ff ff marker.
 */
void log_current_packet(connection * conn) {
    log_line(conn, "current packet: ");

    if (is_v2(PACKET(conn))) {
        log_array(conn, ((uint8_t *)&PACKET(conn).v2_header), sizeof(data_packet_v2_header));

    } else {
        log_array(conn, ((uint8_t *)&PACKET(conn).header), sizeof(data_packet_header));
    }

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
            size_t declared = declared_data_length(PACKET(conn));
            size_t header_size = (is_v2(PACKET(conn)) ? sizeof(data_packet_v2_header) : sizeof(data_packet_header));
            size_t totalSize = header_size +  declared +  sizeof(data_packet_footer);

            /*
             * A packet larger than data[] can hold has to be stepped over whole. totalSize
             * used to be computed from the clamped length, so an oversized packet had only
             * its first part consumed and the remainder was left at the front of the buffer
             * to be read as if it were the next packet - which it is not. That desynced the
             * stream, and recovery was the one-byte-at-a-time resync in the branch above.
             * Skip it in one piece instead, and say so.
             */
            if (declared > dataLength) {
                log_line(conn, "packet declares %u bytes of payload, more than the %u that fit - skipping it\n",
                         (unsigned)declared, (unsigned)dataLength);

                if (conn->read_count >= totalSize) {
                    memmove(conn->recv_buffer, conn->recv_buffer + totalSize, conn->read_count - totalSize);
                    conn->read_count -= totalSize;
                }

                return;
            }

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
        //TIMER sets the upload interval
        conn->supports_interval = true;
        conn->WARNING_FUNCTION = JIMI_warn;
        conn->AUDIO_WARNING_FUNCTION = JIMI_warn;
        conn->MOTOR_WARNING_FUNCTION = JIMI_do_nothing;
    }
}

#include <memory.h>
#include "jimi_packet.h"
#include "../crc16.h"
#include "../connection.h"
#include "../logfiles.h"
#include <stdlib.h>

bool is_v2(data_packet packet) {
    return packet.header.start_bit[0] == 0x79;
}

//The length the packet declares, before any clamping - what the sender says is on the wire.
size_t declared_data_length(data_packet packet) {
    return is_v2(packet)  ? (size_t)max(0, (int)SWAP_UINT16(packet.v2_header.length) - 6)
           : (size_t)max(0, (int)packet.header.length - 5);
}

size_t data_length(data_packet packet) {
    //Clamped to what data[] can actually hold. The clamp used to be BUF_SIZE/4 - a quarter
    //of the buffer - while a 0x79 packet carries a two byte length and is allowed to be far
    //larger. Callers that need the unclamped figure use declared_data_length().
    size_t declared = declared_data_length(packet);
    return min(sizeof(packet.data), declared);
}


// calculate 16 bits CRC of the given length data.
/*
 * CRC-ITU over everything from the packet length to the serial number inclusive, which is
 * what the GT06 specification says and what both framings use - the only difference is that
 * the long packet's length is two bytes rather than one.
 *
 * The v2 case used to be a comment saying the calculation was unknown, which left two
 * problems. Long packets were never checked at all, so a corrupted one was parsed as though
 * it were sound. And crc was declared without an initial value and only set in the v1 branch,
 * so anything that did reach the loop below through the v2 path was folding bytes into an
 * uninitialised variable. Nothing called it that way, but only by luck of a short circuit in
 * the one caller.
 *
 * Verified against a captured 0x9C packet - 79 79 00 08 9c 04 00 00 00 7c bf 82 0d 0a - whose
 * stored checksum this reproduces exactly.
 */
uint16_t crc16(data_packet packet) {
    uint16_t crc = crc16_init();
    uint16_t length = data_length(packet);

    if (is_v2(packet)) {
        //the length as it arrived, both bytes, in wire order
        const uint8_t * len_bytes = (const uint8_t *) & packet.v2_header.length;
        crc = crc16_addbyte(crc, len_bytes[0]);
        crc = crc16_addbyte(crc, len_bytes[1]);
        crc = crc16_addbyte(crc, packet.v2_header.protocol_number);
        //the information byte belongs to the content, so it is covered too
        crc = crc16_addbyte(crc, packet.v2_header.information);

    } else {
        crc = crc16_addbyte(crc, packet.header.length);
        crc = crc16_addbyte(crc, packet.header.protocol_number);
    }

    //a v1 packet cannot carry more than 250 bytes of payload - its length field is a single
    //byte - so this bound only ever bites on a long packet, where it must not
    size_t covered = is_v2(packet) ? length : min(250, length);

    for (size_t i = 0; i < covered && i < sizeof(packet.data); i++) {
        crc = crc16_addbyte(crc, packet.data[i]);
    }

    crc = crc16_adduint16(crc, packet.footer.serial_number);
    crc = crc16_finish(crc);
    return crc;
}

data_packet get_basic_packet() {
    data_packet ret;
    ret.header.start_bit[0] = 0x78;
    ret.header.start_bit[1] = 0x78;
    ret.footer.stop_bit[0] = 0x0D;
    ret.footer.stop_bit[1] = 0x0A;
    ret.header.length = 5;
    return ret;
}

data_packet create_response(uint8_t protocol_number, uint16_t count) {
    data_packet ret = get_basic_packet();
    ret.header.protocol_number = protocol_number;
    ret.footer.serial_number = count;
    ret.footer.crc = crc16(ret);
    return ret;
}

void send_data_packet( void * c, data_packet packet) {
    connection * conn = (connection *)c;
    unsigned short length = packet.header.length - 5;
    int start = conn->send_count;
    memcpy(conn->send_buffer + start, &packet.header, sizeof(data_packet_header));
    conn->send_count += sizeof(data_packet_header);
    memcpy(conn->send_buffer + conn->send_count, packet.data, length);
    conn->send_count += length;
    memcpy(conn->send_buffer + conn->send_count, &packet.footer, sizeof(data_packet_footer));
    conn->send_count += sizeof(data_packet_footer);
    log_line(conn, "sent response: ");

    for (int i = start; i < conn->send_count; i++) {
        unsigned char curr = ((unsigned char *)conn->send_buffer)[i];

        if (isprint(curr)) {
            //if(false){
            logprintf(conn, "%c ", curr);

        } else {
            logprintf(conn, "\\0x%x ", curr);
        }
    }

    logprintf(conn, "\n");
}

bool JIMI_send_command(void * c, const char * cmd) {
    connection * conn = (connection *)c;
    char translated[64] = {0};

    //adaptive tracking hands every protocol the same UPDATE=<seconds>#. GT06 splits it in
    //two and, awkwardly, uses different units for each: T1 is the upload interval with the
    //ignition on, in seconds, and T2 the interval with it off, in minutes. Both are
    //documented as 5..18000. T2 gets at least a minute so a short active interval cannot
    //round it down to zero.
    if (strlen(cmd) > 7 && memcmp(cmd, "UPDATE=", 7) == 0) {
        unsigned int seconds = atoi(cmd + 7);

        if (seconds < 5) {
            seconds = 5;
        }

        if (seconds > 18000) {
            seconds = 18000;
        }

        unsigned int minutes = seconds / 60;

        if (minutes < 1) {
            minutes = 1;
        }

        snprintf(translated, sizeof(translated) - 1, "TIMER,%u,%u#", seconds, minutes);
        cmd = translated;
    }

    command_packet command;
    data_packet to_send = create_response(0x80, 0);
    memset(&command, 0, sizeof(command));
    memcpy(command.cmd, cmd, min(249, strlen(cmd)));
    command.length = 4 + strlen(command.cmd);
    memcpy(to_send.data, &command, command.length + 1);
    to_send.header.length = 6 + command.length;
    to_send.footer.crc = crc16(to_send);
    send_data_packet(conn, to_send);
    return true;
}

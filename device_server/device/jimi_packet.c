#include <memory.h>
#include "jimi_packet.h"
#include "../crc16.h"
#include "../connection.h"
#include "../logfiles.h"

bool is_v2(data_packet packet) {
    return packet.header.start_bit[0] == 0x79;
}

size_t data_length(data_packet packet) {
    return is_v2(packet)  ? min(BUF_SIZE / 4, max(0, (int)SWAP_UINT16(packet.v2_header.length) - 6)) : min(BUF_SIZE / 4, max(0, (int)packet.header.length - 5));
}


// calculate 16 bits CRC of the given length data.
uint16_t crc16(data_packet packet) {
    uint16_t crc;
    uint16_t length = data_length(packet);

    if (!is_v2(packet)) {
        crc = crc16_addbyte(crc16_init(), packet.header.length);
        crc =  crc16_addbyte(crc, packet.header.protocol_number);

    } else {
        //unknown how the crc for a v2 packet is calculated
    }

    for (int i = 0; i < min(250, length); i++) {
        crc =  crc16_addbyte(crc, packet.data[i]);
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

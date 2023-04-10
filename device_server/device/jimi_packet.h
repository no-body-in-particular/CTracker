#ifndef _JIMI_PACKET_H_
#define _JIMI_PACKET_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../config.h"
#include "../util.h"
#include "../connection.h"
#include "jimi_typedefs.h"

#define PACKET(CUR) (*((data_packet *) CUR -> current_packet))


bool is_v2(data_packet packet);
size_t data_length(data_packet packet) ;
uint16_t crc16(data_packet packet) ;
data_packet get_basic_packet() ;
data_packet create_response(uint8_t protocol_number, uint16_t count) ;

void send_data_packet( void * conn, data_packet packet) ;
bool JIMI_send_command(void * conn, const char * cmd) ;



#endif

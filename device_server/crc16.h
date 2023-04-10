#ifndef _CRC_H_
#define _CRC_H_
#include <stdint.h>


uint16_t crc16_init();
uint16_t crc16_finish(uint16_t crc) ;
uint16_t crc16_addbyte(uint16_t crc, uint8_t bt) ;
uint16_t crc16_adduint16(uint16_t crc, uint16_t bt) ;

#endif

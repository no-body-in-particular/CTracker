#ifndef CRC64_H_INCLUDED
#define CRC64_H_INCLUDED

#include "stdint.h"
#include "stddef.h"
uint64_t crc64(uint64_t seed, const unsigned char * data, size_t len);

#endif // CRC64_H_INCLUDED

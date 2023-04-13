#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#ifndef _UTIL_H_
#define _UTIL_H_

#if __STDC_VERSION__ >= 199901L
# define _XOPEN_SOURCE 600
#else
# define _XOPEN_SOURCE 500
#endif /* __STDC_VERSION__ */
#include <time.h>

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))
#define SWAP_UINT16(x) (((x&0xff00) >> 8) | ((x&0xff) << 8))
#define SWAP_UINT32(x) (((x&0xff000000) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | ((x&0xFF) << 24))
#define SWAP_FLOAT(FLT)  *( (uint32_t *)& FLT) = SWAP_UINT32((* (uint32_t *)& FLT));


#define MAX_TIME ((((time_t) 1 << (sizeof(time_t) * 8 - 2)) - 1) * 2 + 1)

#define CONVERT_HEX(bt, a, b)\
{\
	unsigned char tmp = bt &0x0f;\
	b = (tmp + 48) + (tmp > 9 ? 7 : 0);\
	tmp = bt >> 4;\
	a = (tmp + 48) + (tmp > 9 ? 7 : 0);\
}

#define HEXTOBYTE(H1,H2) (H1>='A'?(H1-'A'+10):(H1 - '0'))<<4 | (H2>='A'?(H2-'A'+10):(H2 - '0'))
#define IMEI_TO_BYTES(IMEI,BYTES)\
BYTES[0]=HEXTOBYTE(IMEI[0],IMEI[1]);\
BYTES[1]=HEXTOBYTE(IMEI[2],IMEI[3]);\
BYTES[2]=HEXTOBYTE(IMEI[4],IMEI[5]);\
BYTES[3]=HEXTOBYTE(IMEI[6],IMEI[7]);\
BYTES[4]=HEXTOBYTE(IMEI[8],IMEI[9]);\
BYTES[5]=HEXTOBYTE(IMEI[10],IMEI[11]);\
BYTES[6]=HEXTOBYTE(IMEI[12],IMEI[13]);\
BYTES[7]=HEXTOBYTE(IMEI[14],IMEI[15]);


void convert_imei(unsigned char * from, char * to);
time_t fileModifiedAgo(char * path) ;
double haversineDistance(double lat1, double lon1, double lat2, double lon2) ;
void strip_unprintable(char * from);
bool file_exists (char * filename) ;
double compute_speed(time_t since, double lat1, double lon1, double lat2, double lon2);
int msleep(long msec);
size_t binary_replace(uint8_t * from, size_t fromlen, uint8_t * to, size_t tolen, uint8_t * in, size_t length, size_t safelength);
time_t parse_date(const char * dt);
time_t date_to_time(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec) ;
void pad_imei(char * imei) ;
float voltage_to_soc(float voltage);
#endif

#ifndef _GPS_STRING_H_
#define _GPS_STRING_H_

#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include <stdlib.h>
#include <string.h>


bool is_special(char c);
char * strip_whitespace(char * cmd);
int parse_int(char * str, size_t count);
float parse_float(char * string);
int idx(char * string, char c);
void rep(unsigned char * in, unsigned char from, unsigned char with, size_t len);
size_t split_to(unsigned char delim, unsigned char * src, size_t len, unsigned char ** dest, size_t dest_count);

#endif

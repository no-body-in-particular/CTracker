
#include <stdlib.h>
#include "string.h"
#include "util.h"

bool is_special(char c) {
    //ctype takes an int that must be representable as unsigned char or EOF. Passing a
    //plain (signed) char hands it a negative value for every byte above 0x7f, which is
    //undefined - and these protocols carry binary payloads full of them.
    unsigned char u = (unsigned char)c;
    return (isspace(u) || u == '\r' || u == '\n');
}

char * strip_whitespace(char * cmd) {
    for (; strlen(cmd) > 0 && is_special(*cmd); cmd++);

    //a string that is empty, or entirely whitespace, leaves strlen at 0 here and the old
    //"strlen - 1" wrapped to SIZE_MAX, so the loop read cmd[SIZE_MAX] on its first test.
    size_t len = strlen(cmd);

    if (len == 0) {
        return cmd;
    }

    for (size_t idx = len - 1; idx > 0 && is_special(cmd[idx]) ; idx--) {
        char c = cmd[idx];

        if (!isprint(c)) {
            cmd[idx] = ' ';
        }

        if (isspace(c)) {
            cmd[idx] = ' ';
        }

        if (c == '\r' || c == '\n') {
            cmd[idx] = 0;
        }
    }

    return cmd;
}

int parse_int(char * str, size_t count) {
    if (count >= 32) {
        return 0;
    }

    if (0 == str) {
        return 0;
    }

    for (size_t i = 0; i < count && is_special(*str); i++) {
        str++;
    }

    unsigned char tmp[32];
    memset(tmp, 0, 32);
    memcpy(tmp, str, min(count, strlen(str)));
    return atoi((const char *)tmp);
}

float parse_float(char * string) {
    float num = 0, divisor = 10;;
    char ch;
    bool neg = false;

    if (0 == string) {
        return num;
    }

    size_t count = strlen(string);

    for (size_t i = 0; i < count && is_special(*string); i++) {
        string++;
    }

    if (*string == '-') {
        neg = true;
        string++;
    }

    while (isdigit((unsigned char)(ch = *string)) && ch != 0) {
        num = 10 * num + ch - '0';
        string++;
    }

    //the integer loop above used to consume the byte that stopped it. When that byte was
    //the terminator the fractional loop started one past the end of the string and read
    //whatever followed it in memory.
    if (*string == '.' || *string == ',') {
        string++;

        while (isdigit((unsigned char)(ch = *string)) && ch != 0) {
            num = num + (ch - '0') / divisor;
            divisor *= 10;
            string++;
        }
    }

    return neg ? -num : num;
}

int idx(char * string, char c) {
    char * e = strchr(string, c);

    if (e == NULL) {
        return 0;
    }

    return (int)(e - string);
}

void rep(unsigned char * in, unsigned char from, unsigned char with, size_t len) {
    for (size_t i = 0; i < len; i++)if (in[i] == from) {
            in[i] = with;
        }
}

size_t split_to(unsigned char delim, unsigned char * src, size_t len, unsigned char ** dest, size_t dest_count) {
    size_t prev_idx = 0;
    size_t str_count = 0;
    len = min(len, strlen(src));

    for (size_t i = 0; i <= len && str_count < dest_count; i++) {
        if (src[i] == delim || src[i] == '\0' || i == len) {
            size_t cur_len = i - prev_idx;
            dest[str_count] = src + prev_idx;

            for (; cur_len > 0 && isspace(dest[str_count][0]) && dest[str_count] < (src + len)  ;)  {
                dest[str_count]++;
                cur_len--;
            }

            str_count++;

            if ( src[i] == '\0' || i == len) {
                return str_count;
            }

            prev_idx = i + 1;
            src[i] = 0;
        }
    }

    return str_count;
}

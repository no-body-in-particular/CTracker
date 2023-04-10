
#include <stdlib.h>
#include "string.h"
#include "util.h"

bool is_special(char c) {
    return (isspace(c) || c == '\r' || c == '\n');
}

char * strip_whitespace(char * cmd) {
    for (; strlen(cmd) > 0 && is_special(*cmd); cmd++);

    for (size_t idx = strlen(cmd) - 1; idx > 0 && is_special(cmd[idx]) ; idx--) {
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
    size_t count = strlen(string);

    for (size_t i = 0; i < count && is_special(*string); i++) {
        string++;
    }

    if (*string == '-') {
        neg = true;
        string++;
    }

    while (isdigit(ch = *(string++))) {
        num = 10 * num + ch - '0';
    }

    while (isdigit(ch = *(string++))) {
        num = num + (ch - '0') / divisor;
        divisor *= 10;
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

    for (size_t i = 0; i < len && str_count < dest_count; i++) {
        if (src[i] == delim || src[i] == 0) {
            size_t cur_len = i - prev_idx;
            dest[str_count] = src + prev_idx;

            for (; cur_len > 0 && isspace(dest[str_count][0]) ;)  {
                dest[str_count]++;
                cur_len--;
            }

            str_count++;
            prev_idx = i + 1;

            if (src[i] == 0) {
                return str_count;
            }

            src[i] = 0;
        }
    }

    return str_count;
}

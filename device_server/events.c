#include "events.h"
#include <string.h>

void read_disabled_alarms(connection * conn) {
    FILE * fp = fopen(conn->disabled_alarms_infile, "r");
    memset( conn->disabled_alarms, 0, BUF_SIZE);

    //if there's no commands file, well there is nothing to do
    if (fp <= 0) {
        return;
    }

    fseek(fp, 0L, SEEK_END);

    if (ftell(fp) < 1) {
        fclose(fp);
        return;
    }

    fseek(fp, 0L, SEEK_SET);
    fgets(conn->disabled_alarms, BUF_SIZE - 1, fp);
    fclose(fp);
}


/*
 * Letters and digits only, folded to lower case. The same alarm reaches this function spelled
 * three different ways depending on which protocol raised it - "low battery" from most of
 * them, "Low battery" from the r18 watches and "LowBattery" straight off a megastek - and the
 * match used to be a plain case sensitive strstr. So a user who switched off "low battery"
 * still got paged by the other two, silently, and there was nothing in the log to say why.
 */
static void fold_alarm_name(const char * in, char * out, size_t out_size) {
    size_t w = 0;

    for (size_t i = 0; in && in[i] && w + 1 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];

        if (c >= 'A' && c <= 'Z') {
            out[w++] = (char)(c - 'A' + 'a');

        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[w++] = (char)c;
        }
    }

    out[w] = 0;
}

bool is_alarm_disabled(connection * conn, const char * evt) {
    char folded_list[BUF_SIZE];
    char folded_evt[BUF_SIZE];

    if (strstr(conn->disabled_alarms, "*") != 0) {
        return true;
    }

    //still honour an exact match, so a rule written against the raw text keeps working
    if (evt && strstr(conn->disabled_alarms, evt) != 0) {
        return true;
    }

    fold_alarm_name((const char *)conn->disabled_alarms, folded_list, sizeof(folded_list));
    fold_alarm_name(evt, folded_evt, sizeof(folded_evt));

    if (folded_evt[0] == 0) {
        return false;
    }

    return strstr(folded_list, folded_evt) != 0;
}

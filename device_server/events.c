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

/*
 * Which fence folders are switched off, read from <imei>.disabled-fences.txt the same way
 * the disabled alarms are read. An absent or unreadable file leaves the list empty, which
 * means every folder is enforced - the safe direction for a curfew, and what every device
 * did before folders existed.
 */
void read_disabled_fences(connection * conn) {
    FILE * fp = fopen(conn->disabled_fences_infile, "r");
    memset( conn->disabled_fences, 0, BUF_SIZE);

    if (fp <= 0) {
        return;
    }

    fseek(fp, 0L, SEEK_END);

    if (ftell(fp) < 1) {
        fclose(fp);
        return;
    }

    fseek(fp, 0L, SEEK_SET);
    fgets(conn->disabled_fences, BUF_SIZE - 1, fp);
    fclose(fp);
}


//letters and digits only, folded to lower case, so a folder matches however it was cased
//and whatever spacing the list was written with
static void fold_folder_name(const char * in, size_t len, char * out, size_t out_size) {
    size_t w = 0;

    for (size_t i = 0; in && i < len && in[i] && w + 1 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];

        if (c >= 'A' && c <= 'Z') {
            out[w++] = (char)(c - 'A' + 'a');

        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[w++] = (char)c;
        }
    }

    out[w] = 0;
}

/*
 * Unlike is_alarm_disabled() this matches whole comma separated entries rather than any
 * substring. Folder names are chosen by the user and collide readily - a folder called
 * "Sommer" must not switch off "SommerFerien" - and switching off the wrong set of fences
 * here means either enforcing a curfew that was lifted or lifting one that was not.
 *
 * A fence with no folder belongs to "default", so that is the name to disable it by.
 */
bool is_fence_folder_disabled(connection * conn, const char * folder) {
    char wanted[64];
    char entry[64];

    if (strstr(conn->disabled_fences, "*") != 0) {
        return true;
    }

    fold_folder_name((folder && folder[0]) ? folder : "default", 64, wanted, sizeof(wanted));

    if (wanted[0] == 0) {
        return false;
    }

    const char * list = (const char *)conn->disabled_fences;

    for (size_t i = 0; list[i];) {
        size_t start = i;

        while (list[i] && list[i] != ',') {
            i++;
        }

        fold_folder_name(list + start, i - start, entry, sizeof(entry));

        if (entry[0] != 0 && strcmp(entry, wanted) == 0) {
            return true;
        }

        if (list[i] == ',') {
            i++;
        }
    }

    return false;
}

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


bool is_alarm_disabled(connection * conn, const char * evt) {
    return strstr(conn->disabled_alarms, "*") != 0 || strstr(conn->disabled_alarms, evt) != 0;
}

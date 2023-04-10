#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <err.h>
#include <sys/select.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../lbs_lookup.c"


int main(int argc, char * argv[]) {
    cell_tower tower;
    init_lbs();
    tower.mcc = 262;
    tower.mnc = 2;
    tower.lac = 801;
    tower.cell_id = 86355;
    location_result result = lbs_lookup(&tower, 52.522202, 13.285512);
    fprintf(stdout, "%u %f %f\n", result.valid, result.lat, result.lng);

    for (size_t i = 0; i < 9999999; i++) {
        tower.mcc = rand() % 300;
        tower.mnc = rand() % 255;
        tower.lac = rand() % 900;
        tower.location.valid = false;
        result.valid = false;
        tower.cell_id = rand() % 6400000;
        result = lbs_lookup(&tower, 52.522202, 13.285512);

        if (!result.valid) {
            fprintf(stdout, "%u %f %f\n", result.valid, result.lat, result.lng);
        }

        lbs_cache_to_database(&lbs_db);
        // lbs_database_to_file(&lbs_db, CELLDB_FILE);
    }

    lbs_cache_to_database(&lbs_db);
    lbs_database_to_file(&lbs_db, CELLDB_FILE);
    lbs_cache_to_database(&lbs_db);
    lbs_database_to_file(&lbs_db, CELLDB_FILE);
    lbs_cache_to_database(&lbs_db);
    lbs_database_to_file(&lbs_db, CELLDB_FILE);
    return 0;
}


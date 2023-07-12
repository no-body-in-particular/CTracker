#include "lbs_lookup.h"
#include "string.h"
#include "util.h"
#include "web_geolocate.h"
#include "sort.h"
#include "search.h"

double compare_lbs(void  * tower_a, void * tower_b) {
    return memcmp(tower_b, tower_a, 9);
}

double compare_lbs_partial(void  * tower_a, void * tower_b) {
    return memcmp(tower_a, tower_b, 5);
}


void lbs_sort(cell_db * database) {
    database->tower_count = quick_sort(database->tower_buffer, database->tower_count, sizeof(cell_tower), 72, compare_lbs);
}

//create mutex and read from file
void lbs_initialise_database(cell_db * database) {
    memset(database, 0, sizeof(cell_db));
    database->tower_count = 0;
    database->tower_buffer = malloc(sizeof(cell_tower) * 1048576);
    database->current_cache = malloc(sizeof(cell_tower) * 10240);
    database->tower_memory_size = 1048576;
    database->cache_memory_size = 10240;
    database->cache_size = 0;
    database->cache_age = time(0);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&database->mutex, &attr);
}

void read_lbs_file(cell_db * database, char * file) {
    char buffer[BUF_SIZE];
    FILE * fp = fopen(file, "r");
    fprintf(stdout, "Reading file with LBS data: %s \n", file);

    //if there's no commands file, well there is nothing to do
    if (fp <= 0) {
        fprintf(stdout, "Failed to open file.");
        return;
    }

    size_t line_count = 0;
    fprintf(stdout, "Computing size...\n");

    while (fgets(buffer, BUF_SIZE - 1, fp) ) {
        if (strlen(buffer) > 2 && memcmp(buffer, "radio", 5) != 0) {
            line_count++;
            //we have a line
        }
    }

    fprintf(stdout, "Number of entries: %u\n", line_count);
    size_t tower_idx =   database->tower_count;
    database->tower_count += line_count;
    database->tower_buffer = realloc(database->tower_buffer, sizeof(cell_tower) * database->tower_count);
    fseek(fp, 0, SEEK_SET);

    while (fgets(buffer, BUF_SIZE - 1, fp) ) {
        if (strlen(buffer) > 2 && memcmp(buffer, "radio", 5) != 0) {
            char * sptrs[11];

            if (split_to(',', buffer, strlen(buffer), &sptrs, 11) >= 10) {
                memset(& database->tower_buffer[tower_idx], 0, sizeof(cell_tower));
                database->tower_buffer[tower_idx].mcc = parse_int(sptrs[1], 5); //64000
                database->tower_buffer[tower_idx].mnc = parse_int(sptrs[2], 3);
                database->tower_buffer[tower_idx].lac = parse_int(sptrs[3], 5);
                database->tower_buffer[tower_idx].cell_id = parse_int(sptrs[4], 9);
                database->tower_buffer[tower_idx].location.lat = parse_float(sptrs[7]);
                database->tower_buffer[tower_idx].location.lng = parse_float(sptrs[6]);
                database->tower_buffer[tower_idx].location.valid = true;
                database->tower_buffer[tower_idx].location.radius = parse_float(sptrs[8]);
                database->tower_buffer[tower_idx].location.last_tried = 0;
                tower_idx++;
            }
        }
    }

    fprintf(stdout, "Read all towers: %u\n", tower_idx);
    lbs_sort(database);
    fprintf(stdout, "size in memory:%lu\n", database->tower_count * sizeof(cell_tower));
    fclose(fp);
}


void lbs_database_to_file(cell_db * database, char * file) {
    FILE * fp = fopen(file, "w+b");

    if (fp <= 0) {
        fprintf(stdout, "Failed to write LBS database: %s\n", file);
        return;
    }

    fwrite(&database->tower_count, sizeof(database->tower_count), 1, fp);
    fwrite(database->tower_buffer, database->tower_count * sizeof(cell_tower), 1, fp);
    fclose(fp);
}

void lbs_cache_to_database(cell_db * database) {
    fprintf(stdout, "Merging cache to LBS database and writing to file.\n");
    database->cache_age = time(0);

    if (database->cache_size == 0) {
        fprintf(stdout, "   Nothing to do.. quitting.\n");
        return;
    }

    size_t tower_idx =   database->tower_count;
    database->tower_count += database->cache_size;
    bool resize = false;

    if (database->tower_memory_size < 5) {
        database->tower_memory_size = 5;
        resize = true;
    }

    while (database->tower_count > database->tower_memory_size) {
        database->tower_memory_size *= 1.2;
        resize = true;
    }

    if (resize) {
        database->tower_buffer = realloc(database->tower_buffer, sizeof(cell_tower) * database->tower_memory_size);
    }

    memcpy(database->tower_buffer + tower_idx, database->current_cache, database->cache_size * sizeof(cell_tower));
    database->cache_size = 0;
    lbs_sort(database);
    lbs_database_to_file(database, CELLDB_FILE);
    fprintf(stdout, "Done writing LBS database.\n");
}

void lbs_to_cache(cell_db * database, cell_tower * tower) {
    if ((database->cache_size + 1) > database->cache_memory_size) {
        database->cache_memory_size *= 2;
        database->current_cache = realloc(database->current_cache, sizeof(cell_tower) *  database->cache_memory_size );
    }

    database->current_cache[database->cache_size] = *tower;
    database->cache_size++;
}

void lbs_database_from_file(cell_db * database, char * file) {
    fprintf(stdout, "Loading LBS cache.\n");
    FILE * fp = fopen(file, "rb");

    if (fp <= 0) {
        fprintf(stdout, "   failed to open cache file.\n");
        return;
    }

    fread(&database->tower_count, sizeof(database->tower_count), 1, fp);
    database->tower_buffer = realloc(database->tower_buffer, database->tower_count * sizeof(cell_tower));
    fread(database->tower_buffer, database->tower_count * sizeof(cell_tower), 1, fp);
    fclose(fp);
    fprintf(stdout, "   done.\n");
}



static cell_db lbs_db;

void init_lbs() {
    lbs_initialise_database(&lbs_db);
    lbs_database_from_file(&lbs_db, CELLDB_FILE);

    if (file_exists(OPENCELLID_FILE)) {
        read_lbs_file(&lbs_db, "opencellid.csv");
        lbs_database_to_file(&lbs_db, CELLDB_FILE);
    }

    if (file_exists(MOZCELLID_FILE)) {
        read_lbs_file(&lbs_db, "mozilla.csv");
        lbs_database_to_file(&lbs_db, CELLDB_FILE);
    }
}

location_result lbs_in_cache(cell_tower * to_find) {
    for (size_t n = 0; n < lbs_db.cache_size; n++) {
        if (memcmp(&lbs_db.current_cache[n], to_find, 9) == 0) {
            return  lbs_db.current_cache[n].location;
        }
    }

    location_result failure = {0, 0, 0, 0};
    failure.valid = false;
    return failure;
}

location_result lbs_in_db(cell_tower * to_find) {
    void * ret = lbs_db.tower_count == 0 ? 0 : binary_search(lbs_db.tower_buffer, (lbs_db.tower_buffer + lbs_db.tower_count),  to_find,  sizeof(cell_tower), compare_lbs) ;

    if (ret <= 0) {
        location_result failure = {0, 0, 0, 0};
        failure.valid = false;
        return failure;;
    }

    return ((cell_tower *)ret)->location;
}


location_result lbs_lookup(cell_tower * to_find, float last_lat, float last_lng) {
    pthread_mutex_lock(&lbs_db.mutex);

    if ((time(0) - lbs_db.cache_age) > CACHE_SAVE_TIME) {
        lbs_cache_to_database(&lbs_db);
    }

    location_result ret =   lbs_in_db(to_find);

    if (!ret.valid) {
        ret = lbs_in_cache(to_find);
    }

    if (!ret.valid || (ret.last_tried > 0 && ( (time(0) - ret.last_tried) > CACHE_ENTRY_RETRY))) {
        ret = geolocate_tower(to_find);

        if (ret.valid )  {
            ret.last_tried = 0;
            to_find->location = ret;
            lbs_to_cache(&lbs_db, to_find);
        }
    }

    double closest_match = UINT32_MAX;

    if (!ret.valid) {
        to_find->location.radius = 0;
        to_find->location.valid = false;
        to_find->location.last_tried = time(0);
        cell_tower * current_tower =   lbs_db.tower_count == 0 ? 0 :  binary_search(lbs_db.tower_buffer, (lbs_db.tower_buffer + lbs_db.tower_count),  to_find,  sizeof(cell_tower), compare_lbs_partial) ;
        bool found_closest_tower = false;

        for (; current_tower != 0 && (current_tower < (lbs_db.tower_buffer + lbs_db.tower_count)); current_tower++) {
            if (memcmp(current_tower, to_find, 5) == 0) {
                uint8_t diff = haversineDistance(current_tower->location.lat, current_tower->location.lng, last_lat, last_lng);

                if (diff < closest_match) {
                    closest_match = diff;
                    //get the nearest tower to the current location.
                    to_find->location = current_tower->location;
                    to_find->location.last_tried = time(0);
                    found_closest_tower = true;
                }

            } else {
                break;
            }
        }

        //add tower closest to the last point - usually cell towers are clustered.
        //since this is not an accurate location - we need to re-check it.
        if (found_closest_tower) {
            lbs_to_cache(&lbs_db, to_find);
            ret = to_find->location;
            ret.valid = true;
        }
    }

    pthread_mutex_unlock(&lbs_db.mutex);
    return ret;
}

//locking
//automatic saving/merging

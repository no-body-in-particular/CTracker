#include <pthread.h>
#include "string.h"
#include "util.h"
#include "wifi_lookup.h"
#include "sort.h"
#include "search.h"
#include "crc64.h"

uint64_t wifi_db_entry_hash(wifi_db_entry * val) {
    uint64_t ret = 0;

    for (size_t i = 0; i < val->network_count; i++) {
        ret = crc64(ret, val->network_buffer[i].mac_addr, 6);
    }

    return ret;
}

int8_t wifi_db_entry_bit(void * db_entry, size_t bit) {
    wifi_db_entry * value = (wifi_db_entry *)db_entry;
    uint64_t hash = wifi_db_entry_hash(db_entry);
    return bit >= 64 ? -1 : (hash & (1LLU << (64 - bit))) > 0LLU ? 1LLU : 0;
}


double wifi_network_compare(void * a, void * b) {
    return memcmp(b, a, 6);
}

bool is_subset(wifi_db_entry in, wifi_db_entry  find) {
    for (size_t n = 0; n < find.network_count; n++) {
        bool found = false;

        for (size_t i = 0; i < in.network_count; i++) {
            if (memcmp(in.network_buffer[i].mac_addr, find.network_buffer[n].mac_addr, 6) == 0) {
                found = true;
            }
        }

        if (!found) {
            return false;
        }
    }

    return true;
}

double is_same(void * a, void * b) {
    wifi_db_entry * of = a;
    wifi_db_entry  * in = b;

    if (of->network_count < in->network_count) {
        return -1;
    }

    if (of->network_count > in->network_count) {
        return 1;
    }

    for (size_t n = 0; n < in->network_count; n++) {
        bool found = false;

        for (size_t i = 0; i < of->network_count; i++) {
            if (memcmp(of->network_buffer[i].mac_addr, in->network_buffer[n].mac_addr, 6) == 0) {
                found = true;
            }
        }

        if (!found) {
            return -1;
        }
    }

    return 0;
}


void wifi_sort(wifi_db * db) {
    for (size_t i = 0; i < db->network_count; i++) {
        quick_sort(db->network_buffer[i].network_buffer, db->network_buffer[i].network_count, sizeof(wifi_network), 48, wifi_network_compare);
    }

    db->network_count = quick_sort(db->network_buffer, db->network_count, sizeof(wifi_db_entry), 64, is_same );
}

double wifi_hash_compare(void * db_entry, void * db_entry_b) {
    wifi_db_entry * valueA = (wifi_db_entry *)db_entry;
    wifi_db_entry * valueB = (wifi_db_entry *)db_entry_b;
    double hashA = wifi_db_entry_hash(valueA);
    double hashB = wifi_db_entry_hash(valueB);
    return (hashB - hashA);
}

//create mutex and read from file
void wifi_initialise_database(wifi_db * database) {
    memset(database, 0, sizeof(wifi_db));
    database->network_count = 0;
    database->network_buffer = malloc(sizeof(wifi_network) * 10240);
    database->network_buffer_size = 10240;
    database->cache_count = 0;
    database->network_cache = malloc(sizeof(wifi_network) * 10240);
    database->network_cache_size = 10240;
    database->cache_age = time(0);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&database->mutex, &attr);
}


void wifi_database_to_file(wifi_db * database, char * file) {
    FILE * fp = fopen(file, "w+b");

    if (fp <= 0) {
        fprintf(stdout, "Failed to open wifi database file for writing.\n");
        return;
    }

    fwrite(&database->network_count, sizeof(database->network_count), 1, fp);
    fwrite(database->network_buffer, database->network_count * sizeof(wifi_db_entry), 1, fp);
    fclose(fp);
}

void wifi_database_from_file(wifi_db * database, char * file) {
    FILE * fp = fopen(file, "rb");
    fprintf(stdout, "Loading WiFi cache.\n");

    if (fp <= 0) {
        fprintf(stdout, "   Failed to open file: %s\n", file);
        return;
    }

    fread(&database->network_count, sizeof(database->network_count), 1, fp);
    database->network_buffer = realloc(database->network_buffer, database->network_count * sizeof(wifi_db_entry));
    database->network_buffer_size = database->network_count;
    fread(database->network_buffer, database->network_count * sizeof(wifi_db_entry), 1, fp);
    fclose(fp);
    wifi_sort(database);
    fprintf(stdout, "   done.\n");
}

void wifi_cache_to_database(wifi_db * database) {
    fprintf(stdout, "Merging cache to WiFi database and writing to file. Current cache size: %u\n", database->cache_count);
    database->cache_age = time(0);

    if (database->cache_count == 0) {
        fprintf(stdout, "   Nothing to do.. quitting.\n");
        return;
    }

    //
    size_t tower_idx =   database->network_count;
    database->network_count += database->cache_count;
    bool resize = false;

    if (database->network_buffer_size < 5) {
        database->network_buffer_size = 5;
        resize = true;
    }

    while (database->network_count > database->network_buffer_size) {
        database->network_buffer_size *= 1.2;
        resize = true;
    }

    if (resize) {
        database->network_buffer = realloc(database->network_buffer, sizeof(wifi_db_entry) * database->network_buffer_size);
    }

    memcpy(database->network_buffer + tower_idx, database->network_cache, database->cache_count * sizeof(wifi_db_entry));
    database->cache_count = 0;
    wifi_sort(database);
    wifi_database_to_file(database, WIFIDB_FILE);
    fprintf(stdout, "Done writing WiFi database. Old count: %u New count: %u\n", tower_idx, database->network_count);
}

static wifi_db wifi_database;
void init_wifi() {
    wifi_initialise_database(&wifi_database);
    wifi_database_from_file(&wifi_database, WIFIDB_FILE);
}


location_result wifi_to_cache( wifi_db_entry  networks) {
    pthread_mutex_unlock(&wifi_database.mutex);
    //sort our networks first
    quick_sort(networks.network_buffer, networks.network_count, sizeof(wifi_network), 48, wifi_network_compare);

    for (size_t network_idx = 0; network_idx < wifi_database.cache_count; network_idx++) {
        if (is_same(&wifi_database.network_cache[network_idx], &networks) == 0) {
            return  wifi_database.network_cache[network_idx].result;
        }
    }

    if ( (wifi_database.cache_count + 1) >= wifi_database.network_cache_size) {
        wifi_database.network_cache_size *= 2;
        wifi_database.network_cache = realloc(wifi_database.network_cache, sizeof(wifi_db_entry) *  wifi_database.network_cache_size);
    }

    wifi_database.network_cache[wifi_database.cache_count] = networks;
    wifi_database.cache_count++;
    pthread_mutex_unlock(&wifi_database.mutex);
    return networks.result;
}

//first must always point to a list of at least 2 wifi networks.
location_result wifi_lookup(wifi_network * first, size_t network_count) {
    //create a sorted network entry
    wifi_db_entry entry;
    memset(&entry, 0, sizeof(wifi_db_entry));
    memcpy(entry.network_buffer, first, network_count * sizeof(wifi_network));
    entry.network_count = network_count;
    quick_sort(entry.network_buffer, entry.network_count, sizeof(wifi_network), 48, wifi_network_compare);
    entry.result.valid = false;
    pthread_mutex_unlock(&wifi_database.mutex);
    wifi_db_entry * network_ptr = wifi_database.network_count == 0 ? 0 : (wifi_db_entry *) binary_search(wifi_database.network_buffer, (wifi_database.network_buffer + wifi_database.network_count),  &entry,  sizeof(wifi_db_entry), wifi_hash_compare) ;

    for (; network_ptr != 0 && (network_ptr <= (wifi_database.network_buffer + wifi_database.network_count))
            && wifi_db_entry_hash( network_ptr) == wifi_db_entry_hash( &entry)
            ; network_ptr++) {
        if (is_same(network_ptr, &entry) == 0) {
            entry.result = network_ptr->result;
            break;
        }
    }

    for (size_t network_idx = 0; !entry.result.valid && (network_idx < wifi_database.cache_count); network_idx++) {
        if (is_same(&wifi_database.network_cache[network_idx], &entry) == 0) {
            entry.result = wifi_database.network_cache[network_idx].result;
        }
    }

    if (!entry.result.valid) {
        for (size_t network_idx = 0; !entry.result.valid && (network_idx < wifi_database.network_count); network_idx++) {
            if (is_subset(wifi_database.network_buffer[network_idx], entry )) {
                entry.result = wifi_database.network_buffer[network_idx].result;
            }
        }
    }

    if ((time(0) - wifi_database.cache_age) > CACHE_SAVE_TIME) {
        wifi_cache_to_database(&wifi_database);
    }

    pthread_mutex_unlock(&wifi_database.mutex);

    if (!entry.result.valid) {
        entry.result = geolocate_wifi(entry.network_buffer, entry.network_count);

        if (entry.result.valid) {
            wifi_to_cache( entry);
        }
    }

    return entry.result;
}



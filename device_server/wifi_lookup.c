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



/*
 * Three way, rather than the difference of the two hashes.
 *
 * The hash is a crc64, so it uses the whole 64 bit range, and subtracting one from another
 * broke in two separate ways. A double cannot hold a 64 bit integer exactly past 2^53, so two
 * hashes differing only in their low bits came out equal. And the difference itself does not
 * fit in the int64_t that binary_search stored it in: any pair where one hash has the top bit
 * set and the other does not differs by more than INT64_MAX, the conversion is undefined, and
 * in practice it comes out as INT64_MIN - negative - whichever way round the two actually
 * were. For random 64 bit hashes that is about half of all comparisons, so the search took
 * the wrong branch about half the time and missed entries that were sitting in the table.
 *
 * A miss here is not a wrong answer - the caller checks the hashes exactly and then compares
 * the networks themselves - but it does mean paying a positioning service for a lookup
 * already in the cache, and writing a second copy of an entry already held.
 *
 * The sign convention matches compare_lbs: the arguments are the other way round from what
 * the names suggest, because partition() and binary_search() both want it that way.
 */
double wifi_hash_compare(void * db_entry, void * db_entry_b) {
    uint64_t hashA = wifi_db_entry_hash((wifi_db_entry *)db_entry);
    uint64_t hashB = wifi_db_entry_hash((wifi_db_entry *)db_entry_b);

    if (hashB > hashA) {
        return 1;
    }

    if (hashB < hashA) {
        return -1;
    }

    return 0;
}

double wifi_network_compare(void * a, void * b) {
    return memcmp(b, a, 6);
}

/*
 * One radio, several BSSIDs.
 *
 * A router carrying a guest network and a couple of bands answers on a block of consecutive
 * addresses - 1c:28:af:cc:6c:20 through :25 is one device, not six - and virtual or
 * randomised interfaces derive an address from the real one by setting the locally
 * administered bit. Counted separately they make two scans of the same room look like two
 * different places; counted together they are the landmark they actually are.
 *
 * Masking the low three bits of the last octet covers a block of eight, which is what
 * multi-BSSID hardware allocates. The bits above that are left alone: they are still the
 * vendor's address, and merging further would start joining genuinely different devices.
 */
void normalise_mac(uint8_t * out, const uint8_t * in) {
    memcpy(out, in, 6);
    out[0] &= (uint8_t) ~0x02u;
    out[5] &= (uint8_t) ~0x07u;
}

//Normalise every address in an entry in place, then sort so comparisons stay cheap. Used on
//what a device reports and on what is already stored, so old entries written before any of
//this match new scans without the file needing converting.
void normalise_entry(wifi_db_entry * entry) {
    if (entry->network_count > 16) {
        entry->network_count = 16;
    }

    for (size_t i = 0; i < entry->network_count; i++) {
        normalise_mac(entry->network_buffer[i].mac_addr, entry->network_buffer[i].mac_addr);
    }

    //the comparator is passed twice on purpose: the second argument is what quick_sort uses
    //to drop duplicates, and after normalisation a block of BSSIDs from one radio really is
    //one entry repeated. Leaving them in would count that radio six times over when weighing
    //how much two scans have in common.
    entry->network_count = quick_sort(entry->network_buffer, entry->network_count,
                                      sizeof(wifi_network), wifi_network_compare,
                                      wifi_network_compare);
}

//How many access points the two have in common, once normalised. Both are sorted, but the
//counts are at most sixteen so a straight walk is clearer than merging and no slower.
size_t shared_networks(wifi_db_entry * a, wifi_db_entry * b) {
    size_t shared = 0;

    for (size_t i = 0; i < a->network_count && i < 16; i++) {
        for (size_t j = 0; j < b->network_count && j < 16; j++) {
            if (memcmp(a->network_buffer[i].mac_addr, b->network_buffer[j].mac_addr, 6) == 0) {
                shared++;
                break;
            }
        }
    }

    return shared;
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
        db->network_buffer[i].network_count = quick_sort(db->network_buffer[i].network_buffer, db->network_buffer[i].network_count, sizeof(wifi_network), wifi_network_compare, 0);
    }

    db->network_count = quick_sort(db->network_buffer, db->network_count, sizeof(wifi_db_entry), wifi_hash_compare, is_same);
}


//create mutex and read from file
void wifi_initialise_database(wifi_db * database) {
    memset(database, 0, sizeof(wifi_db));
    /*
     * sizeof(wifi_db_entry), not sizeof(wifi_network). Both buffers hold entries; a
     * wifi_network is one of the sixteen access points inside an entry. Nine bytes against a
     * hundred and seventy three, so asking for ten thousand entries bought room for five
     * hundred and thirty two of them - and the growth check compares against the ten thousand
     * it thinks it has, so it would not have resized until long after running off the end.
     * Every entry past the 532nd went into the heap beyond the block.
     *
     * The database buffer is resized correctly the first time it is loaded or merged, which
     * is what has kept this from being noticed; the cache is never resized at all, and it is
     * the one that fills up with every new set of networks a device reports.
     */
    database->network_count = 0;
    database->network_buffer_size = 10240;
    database->network_buffer = malloc(sizeof(wifi_db_entry) * database->network_buffer_size);
    database->cache_count = 0;
    database->network_cache_size = 10240;
    database->network_cache = malloc(sizeof(wifi_db_entry) * database->network_cache_size);

    //without a buffer there is nothing to cache into, and pretending otherwise writes to null
    if (database->network_buffer == 0 || database->network_cache == 0) {
        fprintf(stdout, "   could not allocate the WiFi database, continuing without it\n");
        free(database->network_buffer);
        free(database->network_cache);
        database->network_buffer = 0;
        database->network_cache = 0;
        database->network_buffer_size = 0;
        database->network_cache_size = 0;
    }

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

    if (fread(&database->network_count, sizeof(database->network_count), 1, fp) != 1) {
        fprintf(stdout, "   cache file has no count in it, starting empty.\n");
        database->network_count = 0;
        fclose(fp);
        return;
    }

    //same reasoning as the LBS cache above - bound the count by what the file can hold, and
    //do not hand an unchecked allocation to fread()
    long here = ftell(fp);

    if (fseek(fp, 0, SEEK_END) == 0) {
        long size = ftell(fp);
        fseek(fp, here, SEEK_SET);

        if (size > here) {
            size_t fits = ((size_t)(size - here)) / sizeof(wifi_db_entry);

            if (database->network_count > fits) {
                database->network_count = fits;
            }
        }
    }

    wifi_db_entry * grown = realloc(database->network_buffer, database->network_count * sizeof(wifi_db_entry));

    if (grown == NULL) {
        fprintf(stdout, "   could not allocate %lu bytes for the WiFi cache, continuing without it\n",
                (unsigned long)(database->network_count * sizeof(wifi_db_entry)));
        database->network_count = 0;
        database->network_buffer_size = 0;
        fclose(fp);
        return;
    }

    database->network_buffer = grown;
    database->network_buffer_size = database->network_count;

    if (database->network_count &&
            fread(database->network_buffer, database->network_count * sizeof(wifi_db_entry), 1, fp) != 1) {
        fprintf(stdout, "   cache file is shorter than it claims, starting empty.\n");
        database->network_count = 0;
    }

    fclose(fp);

    /*
     * Normalise what came off disk before anything is compared against it. Entries written
     * before BSSIDs were normalised hold the addresses exactly as the devices reported them,
     * and a scan normalised on the way in would never match one of those - the whole stored
     * history would look empty and every lookup would go to the positioning service. Doing it
     * here converts the file as it is read instead, so nothing needs migrating and the next
     * save writes it back in the new form.
     */
    for (size_t idx = 0; idx < database->network_count; idx++) {
        normalise_entry(&database->network_buffer[idx]);
    }

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

void test() {
    wifi_sort(&wifi_database);
}


location_result wifi_to_cache( wifi_db_entry  networks) {
    /*
     * Locked, not unlocked. Every mutex call in this file was an unlock and there was not one
     * lock anywhere, so the mutex was initialised and then never held by anybody - while the
     * two functions below it grow the cache and the database with realloc, and the search in
     * wifi_lookup() walks the buffer that grows. Two devices reporting wifi at the same moment
     * is all it takes for one thread to be reading the buffer another has just moved.
     *
     * The equivalent code in lbs_lookup.c locks and unlocks correctly, which says this was
     * meant to and simply came out the wrong way round.
     */
    pthread_mutex_lock(&wifi_database.mutex);
    //Normalise here rather than at each caller: this is the one place entries are written,
    //and a self-learned entry stored with raw addresses would never match a scan that had
    //been normalised on the way in - it would sit in the cache answering nothing.
    normalise_entry(&networks);

    for (size_t network_idx = 0; network_idx < wifi_database.cache_count; network_idx++) {
        if (is_same(&wifi_database.network_cache[network_idx], &networks) == 0) {
            //this returned while still holding the lock, so the first cache hit after the
            //locking is fixed would have been the last thing this server ever did
            location_result found = wifi_database.network_cache[network_idx].result;
            pthread_mutex_unlock(&wifi_database.mutex);
            return found;
        }
    }

    if ((wifi_database.cache_count + 1) >= wifi_database.network_cache_size) {
        size_t grown_size = wifi_database.network_cache_size ? wifi_database.network_cache_size * 2 : 1024;
        wifi_db_entry * grown = realloc(wifi_database.network_cache, sizeof(wifi_db_entry) * grown_size);

        if (grown == 0) {
            //the reading is lost, which costs one lookup; writing anyway costs the process
            pthread_mutex_unlock(&wifi_database.mutex);
            return networks.result;
        }

        wifi_database.network_cache = grown;
        wifi_database.network_cache_size = grown_size;
    }

    wifi_database.network_cache[wifi_database.cache_count] = networks;
    wifi_database.cache_count++;
    pthread_mutex_unlock(&wifi_database.mutex);
    return networks.result;
}

//first must always point to a list of at least 2 wifi networks.
/*
 * A position from everything that knows about these access points.
 *
 * Every stored entry sharing at least one access point with the scan votes for its own
 * position, weighted by how many it shares - an entry overlapping six of them says more
 * about where the device is than one overlapping a single neighbour's router. Votes within
 * WIFI_CONSENSUS_RADIUS of each other are the same place, and the heaviest such cluster
 * wins. The answer is that cluster's weighted centre, so the wrong entries are outvoted
 * rather than averaged in.
 *
 * Called with the database mutex held.
 */
location_result wifi_consensus(wifi_db_entry * key) {
    static __thread float vote_lat[WIFI_CONSENSUS_MAX_VOTES];
    static __thread float vote_lng[WIFI_CONSENSUS_MAX_VOTES];
    static __thread double vote_weight[WIFI_CONSENSUS_MAX_VOTES];
    size_t votes = 0;

    location_result out;
    memset(&out, 0, sizeof(out));
    out.valid = false;

    wifi_db_entry * pools[2] = { wifi_database.network_buffer, wifi_database.network_cache };
    size_t counts[2] = { wifi_database.network_count, wifi_database.cache_count };

    for (size_t pool = 0; pool < 2; pool++) {
        if (pools[pool] == 0) {
            continue;
        }

        for (size_t idx = 0; idx < counts[pool] && votes < WIFI_CONSENSUS_MAX_VOTES; idx++) {
            wifi_db_entry * stored = &pools[pool][idx];

            if (!stored->result.valid) {
                continue;
            }

            size_t shared = shared_networks(key, stored);

            if (shared == 0) {
                continue;
            }

            vote_lat[votes] = stored->result.lat;
            vote_lng[votes] = stored->result.lng;
            vote_weight[votes] = (double) shared;
            votes++;
        }
    }

    if (votes == 0) {
        return out;
    }

    size_t best = 0;
    double best_weight = -1.0;

    for (size_t i = 0; i < votes; i++) {
        double weight = 0.0;

        for (size_t j = 0; j < votes; j++) {
            if (haversineDistance(vote_lat[i], vote_lng[i], vote_lat[j], vote_lng[j]) * 1000.0
                    <= WIFI_CONSENSUS_RADIUS) {
                weight += vote_weight[j];
            }
        }

        if (weight > best_weight) {
            best_weight = weight;
            best = i;
        }
    }

    if (best_weight < WIFI_CONSENSUS_MIN_VOTES) {
        return out;
    }

    //the centre of the winning cluster only - everything outside it lost the vote and must
    //not be allowed to drag the answer toward itself
    double sum_lat = 0.0, sum_lng = 0.0, sum_weight = 0.0;
    double spread = 0.0;

    for (size_t j = 0; j < votes; j++) {
        double away = haversineDistance(vote_lat[best], vote_lng[best], vote_lat[j], vote_lng[j]) * 1000.0;

        if (away > WIFI_CONSENSUS_RADIUS) {
            continue;
        }

        sum_lat += vote_lat[j] * vote_weight[j];
        sum_lng += vote_lng[j] * vote_weight[j];
        sum_weight += vote_weight[j];

        if (away > spread) {
            spread = away;
        }
    }

    if (sum_weight <= 0.0) {
        return out;
    }

    out.lat = (float) (sum_lat / sum_weight);
    out.lng = (float) (sum_lng / sum_weight);
    //how far the agreeing entries are spread, which is a better statement of how well this is
    //known than any single stored entry's own radius
    out.radius = (float) (spread > 10.0 ? spread : 10.0);
    out.last_tried = (uint64_t) time(0);
    out.valid = true;
    return out;
}

/*
 * Record where these access points were seen, from the device's own fix.
 *
 * Refused while the device is moving, and refused when it contradicts a position the stored
 * entries already agree on - see the constants in config.h for why. Returns whether anything
 * was learned, which callers are free to ignore; nothing downstream depends on it.
 */
bool wifi_learn_position(wifi_db_entry * entry, double lat, double lon, double speed_kmh) {
    if (entry == 0 || entry->network_count < WIFI_LOOKUP_MIN) {
        return false;
    }

    //A fix at 0,0 is the absence of one. Learning from it would put every access point the
    //device can see in the Gulf of Guinea.
    if (lat == 0.0 && lon == 0.0) {
        return false;
    }

    if (speed_kmh > WIFI_LEARN_MAX_SPEED) {
        return false;
    }

    wifi_db_entry key = *entry;
    normalise_entry(&key);

    pthread_mutex_lock(&wifi_database.mutex);
    location_result known = wifi_consensus(&key);
    pthread_mutex_unlock(&wifi_database.mutex);

    if (known.valid) {
        double away = haversineDistance(known.lat, known.lng, lat, lon) * 1000.0;

        if (away > WIFI_LEARN_MAX_DISAGREE) {
            fprintf(stdout, "wifi: not learning a position %.0f m from where the database already puts these %u access points\n",
                    away, (unsigned) key.network_count);
            return false;
        }
    }

    key.result.lat = (float) lat;
    key.result.lng = (float) lon;
    key.result.radius = (float) WIFI_LEARN_RADIUS;
    key.result.last_tried = (uint64_t) time(0);
    key.result.valid = true;
    wifi_to_cache(key);
    return true;
}

location_result wifi_lookup(wifi_network * first, size_t network_count) {
    //create a sorted network entry
    wifi_db_entry entry;
    memset(&entry, 0, sizeof(wifi_db_entry));
    memcpy(entry.network_buffer, first, network_count * sizeof(wifi_network));
    entry.network_count = network_count;
    quick_sort(entry.network_buffer, entry.network_count, sizeof(wifi_network), wifi_network_compare, 0);
    entry.result.valid = false;

    /*
     * The address the positioning service is asked about has to be the one the device
     * actually saw, so the raw scan is kept for that. Everything this server matches on uses
     * the normalised form, in its own copy.
     */
    wifi_db_entry key = entry;
    normalise_entry(&key);

    //held across every read of the shared database below, and dropped again before the
    //network lookup at the end - that one can take ten seconds and must not hold anybody up
    pthread_mutex_lock(&wifi_database.mutex);

    /*
     * Consensus rather than a lookup of this exact set. Matching the set was what let a
     * single wrong entry answer for a scan that happened to reproduce it, and made an
     * otherwise identical scan with one BSSID more or less into a different question with
     * its own unrelated answer.
     */
    entry.result = wifi_consensus(&key);

    if ((time(0) - wifi_database.cache_age) > CACHE_SAVE_TIME) {
        wifi_cache_to_database(&wifi_database);
    }

    pthread_mutex_unlock(&wifi_database.mutex);

    if (!entry.result.valid) {
        entry.result = geolocate_wifi(entry.network_buffer, entry.network_count);

        if (entry.result.valid) {
            //stored normalised, because that is what every later comparison uses
            key.result = entry.result;
            wifi_to_cache(key);
        }
    }

    return entry.result;
}



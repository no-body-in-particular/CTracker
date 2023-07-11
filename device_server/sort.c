#include "sort.h"
#include <memory.h>
#include <stdlib.h>
#include <pthread.h>


//static buffer to avoid memory fragmentation
static uint8_t * radix_sort_buffer = 0;
static size_t  radix_sort_buffer_size = 0;
static  pthread_mutex_t radix_sort_mutex;

size_t radix_sort(void * entities, size_t count, size_t entity_size, size_t sort_size,  int8_t (*BIT_FUNCTION)(void *, size_t bit), double (*COMPARE_FUNCTION)(void *, void *) ) {
    pthread_mutex_lock(&radix_sort_mutex);

    if (radix_sort_buffer == 0) {
        radix_sort_buffer_size = entity_size * count * 2;
        radix_sort_buffer_size = radix_sort_buffer_size < 5 ? 5 : radix_sort_buffer_size;
        radix_sort_buffer = malloc(radix_sort_buffer_size);
    }

    bool resize = false;

    while ((entity_size * count * 2) > radix_sort_buffer_size) {
        radix_sort_buffer_size *= 1.2f;
        resize = true;
    }

    if (resize) {
        radix_sort_buffer = realloc(radix_sort_buffer, radix_sort_buffer_size);
    }

    uint8_t * bytes = (uint8_t *)entities;
    uint8_t * bucket_a =  radix_sort_buffer;
    size_t bucket_a_size = 0;
    uint8_t * bucket_b =  radix_sort_buffer + (entity_size * count);
    size_t bucket_b_size = 0;

    for (int bt = sort_size - 1; bt >= 0; bt--) {
        for (size_t n = 0; n < count; n++) {
            int8_t btValue = BIT_FUNCTION(bytes + (n * entity_size), bt);

            if (btValue) {
                memcpy(bucket_b + (bucket_b_size * entity_size), bytes + (n * entity_size), entity_size);
                bucket_b_size++;

            } else {
                memcpy(bucket_a + (bucket_a_size * entity_size), bytes + (n * entity_size), entity_size);
                bucket_a_size++;
            }
        }

        memcpy(bytes, bucket_a, bucket_a_size * entity_size);
        memcpy(bytes + (bucket_a_size * entity_size), bucket_b, bucket_b_size * entity_size);
        bucket_a_size = 0;
        bucket_b_size = 0;
    }

    bucket_a_size = 0;

    for (size_t n = 0; n < count ; n++) {
        if (bucket_a_size == 0 || (COMPARE_FUNCTION(bucket_a + ((bucket_a_size - 1)  * entity_size), bytes + (n * entity_size) ) != 0)) {
            memcpy(bucket_a + (bucket_a_size * entity_size), bytes + (n * entity_size), entity_size);
            bucket_a_size++;
        }
    }

    memcpy(bytes, bucket_a, bucket_a_size * entity_size);
    memset(radix_sort_buffer, 0, radix_sort_buffer_size);
    pthread_mutex_unlock(&radix_sort_mutex);
    return bucket_a_size;
}







void memswap(void * a, void * b, size_t size) {
    char * a_swap = (char *)a, *b_swap = (char *)b;
    char * a_end = a + size;

    while (a_swap < a_end) {
        char temp = *a_swap;
        *a_swap = *b_swap;
        *b_swap = temp;
        a_swap++, b_swap++;
    }
}



int fast_rand() {
    static unsigned int g_seed = 0;
    g_seed = (214013 * g_seed + 2531011);
    return (g_seed >> 16) & 0x7FFF;
}

size_t partition(void * entities, int count, size_t entity_size, size_t sort_size,  int8_t (*BIT_FUNCTION)(void *, size_t bit), double (*COMPARE_FUNCTION)(void *, void *) ) {
    uint8_t * bytes = (uint8_t *)entities;
    uint8_t * pivot = (((uint8_t *)entities) + ((count - 1) * entity_size));
    memswap(bytes + (entity_size * ( fast_rand() % count)), pivot, entity_size);
    size_t store_index = 0;

    if (count < 2) {
        return 0;
    }

    for (size_t n = 0; n < count; n++) {
        for (int bt = 0; bt < sort_size; bt++) {
            int8_t btValue = BIT_FUNCTION(bytes + (n * entity_size), bt);
            int8_t btValue_pivot = BIT_FUNCTION(pivot, bt);

            if (btValue < btValue_pivot )  {
                memswap(bytes + (entity_size * store_index), bytes + (n * entity_size), entity_size);
                store_index++;
                n = store_index - 1;
                break;

            } else  if (btValue > btValue_pivot) {
                break;
            }
        }
    }

    memswap(pivot, bytes + (entity_size * (store_index)), entity_size);
    return store_index ;
}


size_t quick_sort(void * entities, size_t count, size_t entity_size, size_t sort_size,  int8_t (*BIT_FUNCTION)(void *, size_t bit), double (*COMPARE_FUNCTION)(void *, void *) ) {
    if (count <= 1) {
        return count;
    }

    uint8_t * bytes = (uint8_t *)entities;
    size_t store_index = partition(entities, count, entity_size, sort_size, BIT_FUNCTION, COMPARE_FUNCTION) ;
    quick_sort(entities, store_index, entity_size, sort_size, BIT_FUNCTION, COMPARE_FUNCTION);
    quick_sort(entities + (entity_size * (store_index + 1)), count - store_index - 1, entity_size, sort_size, BIT_FUNCTION, COMPARE_FUNCTION);

    for (size_t n = 0; n < (count - 1) ; n++) {
        if (count == 0 || (COMPARE_FUNCTION((bytes +  (n * entity_size)), bytes + ((n + 1) * entity_size) ) == 0)) {
            memmove(bytes +  (n * entity_size), bytes +  ((n + 1)* entity_size), (count - n - 1));
            count--;
        }
    }

    return count ;
}

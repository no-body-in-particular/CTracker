#include "sort.h"
#include <memory.h>
#include <stdlib.h>
#include <pthread.h>


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

size_t partition(void * entities, int count, size_t entity_size, size_t sort_size, double (*COMPARE_FUNCTION)(void *, void *) ) {
    uint8_t * bytes = (uint8_t *)entities;
    uint8_t * pivot = (((uint8_t *)entities) + ((count - 1) * entity_size));
    memswap(bytes + (entity_size * ( fast_rand() % count)), pivot, entity_size);
    size_t store_index = 0;

    if (count < 2) {
        return 0;
    }

    for (size_t n = 0; n < count; n++) {
        if (COMPARE_FUNCTION(pivot, bytes + (n * entity_size)) < 0) {
            memswap(bytes + (entity_size * store_index), bytes + (n * entity_size), entity_size);
            store_index++;
            n = store_index - 1;
        }
    }

    memswap(pivot, bytes + (entity_size * (store_index)), entity_size);
    return store_index ;
}


size_t quick_sort(void * entities, size_t count, size_t entity_size, size_t sort_size, double (*COMPARE_FUNCTION)(void *, void *), double (*EXACT_COMPARE_FUNCTION)(void *, void *)) {
    if (count <= 1) {
        return count;
    }

    uint8_t * bytes = (uint8_t *)entities;
    size_t store_index = partition(entities, count, entity_size, sort_size, COMPARE_FUNCTION) ;
    quick_sort(entities, store_index, entity_size, sort_size, COMPARE_FUNCTION,EXACT_COMPARE_FUNCTION);
    quick_sort(entities + (entity_size * (store_index + 1)), count - store_index - 1, entity_size, sort_size, COMPARE_FUNCTION,EXACT_COMPARE_FUNCTION);

    for (size_t n = 0; EXACT_COMPARE_FUNCTION!= 0 && n < (count - 1) ; n++) {
        if ( EXACT_COMPARE_FUNCTION((bytes +  (n * entity_size)), (bytes + ((n + 1) * entity_size)) ) == 0) {
            memmove(bytes +  (n * entity_size), bytes +  ((n + 1)* entity_size), (count - n - 1)*entity_size);
            count--;
            n--;
        }
    }

    return count ;
}

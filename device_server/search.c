#include "search.h"

void * binary_search(void * begin, void * end,  void * to_find, size_t entity_size, double (*COMPARE_FUNCTION)(void *, void * )) {
    uint8_t * find_bytes = (uint8_t *)to_find;
    uint8_t * entity_bytes = (uint8_t *)begin;
    size_t mid_entity = (end - begin) / (2 * entity_size);
    uint8_t * mid_bytes = entity_bytes + (mid_entity * entity_size);
    int64_t compare_result = COMPARE_FUNCTION(mid_bytes, to_find);

    if (compare_result == 0) {
        return mid_bytes;
    }

    if ((end - begin) == entity_size) {
        return begin;
    }

    if (compare_result < 0) {
        return binary_search(begin, mid_bytes, to_find, entity_size, COMPARE_FUNCTION);

    } else {
        return binary_search(mid_bytes, end, to_find, entity_size, COMPARE_FUNCTION);
    }

    return 0;
}


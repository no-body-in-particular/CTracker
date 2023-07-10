#ifndef SORT_H_INCLUDED
#define SORT_H_INCLUDED

#include <stdint.h>
#include <memory.h>
#include <stdio.h>
#include <stdbool.h>

size_t radix_sort(void * entities, size_t count, size_t entity_size, size_t sort_size,    int8_t (*BIT_FUNCTION)(void *, size_t bit), double (*COMPARE_FUNCTION)(void *, void *) );

#endif // SORT_H_INCLUDED

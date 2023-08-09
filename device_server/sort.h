#ifndef SORT_H_INCLUDED
#define SORT_H_INCLUDED

#include <stdint.h>
#include <memory.h>
#include <stdio.h>
#include <stdbool.h>

size_t quick_sort(void * entities, size_t count, size_t entity_size, double (*COMPARE_FUNCTION)(void *, void *), double (*EXACT_COMPARE_FUNCTION)(void *, void *) );

#endif // SORT_H_INCLUDED

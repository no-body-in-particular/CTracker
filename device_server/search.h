#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <stdint.h>
#include <memory.h>
#include <stdio.h>
void * binary_search(void * begin, void * end,  void * to_find, size_t entity_size, double (*COMPARE_FUNCTION)(void *, void * )) ;
#endif // SEARCH_H_INCLUDED

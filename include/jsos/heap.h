#ifndef JSOS_HEAP_H
#define JSOS_HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t capacity;
    size_t used;
    size_t free;
    size_t largest_free;
    size_t allocations;
} HeapStats;

int heap_init(void *memory, size_t size);
void *heap_allocate(size_t size);
void *heap_callocate(size_t count, size_t size);
void *heap_reallocate(void *pointer, size_t size);
void heap_release(void *pointer);
size_t heap_usable_size(const void *pointer);
HeapStats heap_stats(void);

#endif

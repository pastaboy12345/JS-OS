#ifndef JSOS_STDLIB_H
#define JSOS_STDLIB_H

#include <stddef.h>

#define alloca(size) __builtin_alloca(size)

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
size_t malloc_usable_size(void *pointer);

int abs(int value);
int atoi(const char *text);
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));

_Noreturn void abort(void);
_Noreturn void exit(int status);

#endif

#ifndef JSOS_STDIO_H
#define JSOS_STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct JSOS_FILE {
    int descriptor;
} FILE;

extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int fputc(int character, FILE *stream);
int putchar(int character);

#endif

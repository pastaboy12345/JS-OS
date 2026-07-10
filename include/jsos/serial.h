#ifndef JSOS_SERIAL_H
#define JSOS_SERIAL_H

#include <stddef.h>

void serial_init(void);
void serial_putc(char character);
void serial_write(const char *text);
void serial_write_n(const char *text, size_t length);

#endif

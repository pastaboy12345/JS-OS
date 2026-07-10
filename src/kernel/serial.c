#include <jsos/platform.h>
#include <jsos/serial.h>

#define COM1_PORT 0x3f8

void serial_init(void) {
    platform_out8(COM1_PORT + 1, 0x00);
    platform_out8(COM1_PORT + 3, 0x80);
    platform_out8(COM1_PORT + 0, 0x01);
    platform_out8(COM1_PORT + 1, 0x00);
    platform_out8(COM1_PORT + 3, 0x03);
    platform_out8(COM1_PORT + 2, 0xc7);
    platform_out8(COM1_PORT + 4, 0x0b);
}

static void serial_putc_raw(char character) {
    for (unsigned int spin = 0; spin < 1000000; spin++) {
        if ((platform_in8(COM1_PORT + 5) & 0x20) != 0) {
            platform_out8(COM1_PORT, (uint8_t)character);
            return;
        }
    }
}

void serial_putc(char character) {
    if (character == '\n') {
        serial_putc_raw('\r');
    }
    serial_putc_raw(character);
}

void serial_write_n(const char *text, size_t length) {
    if (text == NULL) {
        return;
    }
    for (size_t i = 0; i < length; i++) {
        serial_putc(text[i]);
    }
}

void serial_write(const char *text) {
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        serial_putc(*text++);
    }
}

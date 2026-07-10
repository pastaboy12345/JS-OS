#ifndef JSOS_FRAMEBUFFER_H
#define JSOS_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct limine_framebuffer;

typedef struct {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
    uint8_t red_size;
    uint8_t green_size;
    uint8_t blue_size;
} FramebufferInfo;

bool framebuffer_init(const struct limine_framebuffer *limine_framebuffer);
bool framebuffer_ready(void);
FramebufferInfo framebuffer_info(void);
uint32_t framebuffer_rgb(uint8_t red, uint8_t green, uint8_t blue);
void framebuffer_clear(uint32_t color);
bool framebuffer_get_pixel(int64_t x, int64_t y, uint32_t *color);
bool framebuffer_set_pixel(int64_t x, int64_t y, uint32_t color);
bool framebuffer_blend_pixel(int64_t x, int64_t y, uint32_t color, uint8_t alpha);
void framebuffer_fill_rect(int64_t x, int64_t y, int64_t width, int64_t height,
                           uint32_t color);
void framebuffer_stroke_rect(int64_t x, int64_t y, int64_t width, int64_t height,
                             uint32_t color, uint32_t thickness);
void framebuffer_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                      uint32_t color);
void framebuffer_circle(int64_t center_x, int64_t center_y, int64_t radius,
                        uint32_t color, bool filled);
void framebuffer_fill_rounded_rect(int64_t x, int64_t y, int64_t width,
                                   int64_t height, int64_t radius,
                                   uint32_t color);
void framebuffer_fill_triangle(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                               int64_t x2, int64_t y2, uint32_t color);
void framebuffer_put_rgba(int64_t x, int64_t y, uint32_t width, uint32_t height,
                          const uint8_t *pixels, size_t length);
bool framebuffer_copy_rect(int64_t source_x, int64_t source_y,
                           int64_t destination_x, int64_t destination_y,
                           uint32_t width, uint32_t height);
void framebuffer_present(void);
void framebuffer_text(int64_t x, int64_t y, const char *text, uint32_t color,
                      uint32_t scale);
void framebuffer_measure_text(const char *text, uint32_t scale,
                              uint64_t *width, uint64_t *height);

void console_clear(uint32_t background);
void console_set_colors(uint32_t foreground, uint32_t background);
void console_set_cursor(uint64_t column, uint64_t row);
void console_get_cursor(uint64_t *column, uint64_t *row);
void console_set_cursor_visible(bool visible);
void console_write_n(const char *text, size_t length);
void console_write(const char *text);

#endif

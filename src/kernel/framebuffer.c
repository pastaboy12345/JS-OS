#include <jsos/framebuffer.h>

#include <jsos/font8x8_basic.h>
#include <jsos/serial.h>
#include <limine.h>
#include <string.h>

typedef struct {
    volatile uint8_t *address;
    FramebufferInfo info;
    uint64_t cursor_column;
    uint64_t cursor_row;
    uint32_t foreground;
    uint32_t background;
    bool initialized;
} FramebufferState;

static FramebufferState state;

bool framebuffer_init(const struct limine_framebuffer *framebuffer) {
    if (framebuffer == NULL || framebuffer->address == NULL ||
            framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
            framebuffer->bpp != 32 || framebuffer->width == 0 ||
            framebuffer->height == 0 || framebuffer->pitch < framebuffer->width * 4) {
        return false;
    }
    state.address = framebuffer->address;
    state.info.width = framebuffer->width;
    state.info.height = framebuffer->height;
    state.info.pitch = framebuffer->pitch;
    state.info.bpp = framebuffer->bpp;
    state.info.red_shift = framebuffer->red_mask_shift;
    state.info.green_shift = framebuffer->green_mask_shift;
    state.info.blue_shift = framebuffer->blue_mask_shift;
    state.info.red_size = framebuffer->red_mask_size;
    state.info.green_size = framebuffer->green_mask_size;
    state.info.blue_size = framebuffer->blue_mask_size;
    state.foreground = framebuffer_rgb(235, 239, 244);
    state.background = framebuffer_rgb(16, 18, 20);
    state.cursor_column = 1;
    state.cursor_row = 1;
    state.initialized = true;
    return true;
}

bool framebuffer_ready(void) {
    return state.initialized;
}

FramebufferInfo framebuffer_info(void) {
    return state.info;
}

static uint32_t channel(uint8_t value, uint8_t size, uint8_t shift) {
    if (size == 0) {
        return 0;
    }
    uint32_t scaled = size >= 8 ? value : value >> (8 - size);
    return scaled << shift;
}

uint32_t framebuffer_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return channel(red, state.info.red_size, state.info.red_shift) |
           channel(green, state.info.green_size, state.info.green_shift) |
           channel(blue, state.info.blue_size, state.info.blue_shift);
}

static volatile uint32_t *pixel_address(uint64_t x, uint64_t y) {
    return (volatile uint32_t *)(state.address + y * state.info.pitch + x * 4);
}

bool framebuffer_get_pixel(int64_t x, int64_t y, uint32_t *color) {
    if (!state.initialized || color == NULL || x < 0 || y < 0 ||
            (uint64_t)x >= state.info.width || (uint64_t)y >= state.info.height) {
        return false;
    }
    *color = *pixel_address((uint64_t)x, (uint64_t)y);
    return true;
}

bool framebuffer_set_pixel(int64_t x, int64_t y, uint32_t color) {
    if (!state.initialized || x < 0 || y < 0 ||
            (uint64_t)x >= state.info.width || (uint64_t)y >= state.info.height) {
        return false;
    }
    *pixel_address((uint64_t)x, (uint64_t)y) = color;
    return true;
}

void framebuffer_clear(uint32_t color) {
    if (!state.initialized) {
        return;
    }
    for (uint64_t y = 0; y < state.info.height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(state.address + y * state.info.pitch);
        for (uint64_t x = 0; x < state.info.width; x++) {
            row[x] = color;
        }
    }
}

static bool clip_axis(int64_t start, int64_t length, uint64_t maximum,
                      uint64_t *clipped_start, uint64_t *clipped_end) {
    if (length <= 0 || start >= (int64_t)maximum) {
        return false;
    }
    int64_t end = length > INT64_MAX - start ? INT64_MAX : start + length;
    if (end <= 0) {
        return false;
    }
    *clipped_start = start < 0 ? 0 : (uint64_t)start;
    *clipped_end = end > (int64_t)maximum ? maximum : (uint64_t)end;
    return *clipped_start < *clipped_end;
}

void framebuffer_fill_rect(int64_t x, int64_t y, int64_t width, int64_t height,
                           uint32_t color) {
    uint64_t start_x;
    uint64_t end_x;
    uint64_t start_y;
    uint64_t end_y;
    if (!state.initialized || !clip_axis(x, width, state.info.width, &start_x, &end_x) ||
            !clip_axis(y, height, state.info.height, &start_y, &end_y)) {
        return;
    }
    for (uint64_t row_index = start_y; row_index < end_y; row_index++) {
        volatile uint32_t *row = (volatile uint32_t *)(state.address + row_index * state.info.pitch);
        for (uint64_t column = start_x; column < end_x; column++) {
            row[column] = color;
        }
    }
}

void framebuffer_stroke_rect(int64_t x, int64_t y, int64_t width, int64_t height,
                             uint32_t color, uint32_t thickness) {
    if (thickness == 0 || width <= 0 || height <= 0) {
        return;
    }
    int64_t t = thickness;
    framebuffer_fill_rect(x, y, width, t, color);
    framebuffer_fill_rect(x, y + height - t, width, t, color);
    framebuffer_fill_rect(x, y, t, height, color);
    framebuffer_fill_rect(x + width - t, y, t, height, color);
}

static int64_t absolute(int64_t value) {
    return value < 0 ? -value : value;
}

void framebuffer_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                      uint32_t color) {
    int64_t dx = absolute(x1 - x0);
    int64_t sx = x0 < x1 ? 1 : -1;
    int64_t dy = -absolute(y1 - y0);
    int64_t sy = y0 < y1 ? 1 : -1;
    int64_t error = dx + dy;
    for (;;) {
        framebuffer_set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        int64_t doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void circle_points(int64_t cx, int64_t cy, int64_t x, int64_t y,
                          uint32_t color, bool filled) {
    if (filled) {
        framebuffer_line(cx - x, cy + y, cx + x, cy + y, color);
        framebuffer_line(cx - x, cy - y, cx + x, cy - y, color);
        framebuffer_line(cx - y, cy + x, cx + y, cy + x, color);
        framebuffer_line(cx - y, cy - x, cx + y, cy - x, color);
    } else {
        framebuffer_set_pixel(cx + x, cy + y, color);
        framebuffer_set_pixel(cx - x, cy + y, color);
        framebuffer_set_pixel(cx + x, cy - y, color);
        framebuffer_set_pixel(cx - x, cy - y, color);
        framebuffer_set_pixel(cx + y, cy + x, color);
        framebuffer_set_pixel(cx - y, cy + x, color);
        framebuffer_set_pixel(cx + y, cy - x, color);
        framebuffer_set_pixel(cx - y, cy - x, color);
    }
}

void framebuffer_circle(int64_t center_x, int64_t center_y, int64_t radius,
                        uint32_t color, bool filled) {
    if (radius < 0) {
        return;
    }
    int64_t x = radius;
    int64_t y = 0;
    int64_t error = 1 - radius;
    while (x >= y) {
        circle_points(center_x, center_y, x, y, color, filled);
        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

static void framebuffer_character(int64_t x, int64_t y, unsigned char character,
                                  uint32_t color, uint32_t scale) {
    if (character >= 128) {
        character = '?';
    }
    for (uint32_t row = 0; row < 8; row++) {
        unsigned char bits = font8x8_basic[character][row];
        for (uint32_t column = 0; column < 8; column++) {
            if ((bits & (1U << column)) == 0) {
                continue;
            }
            framebuffer_fill_rect(x + (int64_t)column * scale,
                                  y + (int64_t)row * scale,
                                  scale, scale, color);
        }
    }
}

void framebuffer_text(int64_t x, int64_t y, const char *text, uint32_t color,
                      uint32_t scale) {
    if (!state.initialized || text == NULL || scale == 0 || scale > 32) {
        return;
    }
    int64_t origin_x = x;
    while (*text != '\0') {
        unsigned char character = (unsigned char)*text++;
        if (character == '\n') {
            x = origin_x;
            y += (int64_t)8 * scale;
            continue;
        }
        framebuffer_character(x, y, character, color, scale);
        x += (int64_t)8 * scale;
    }
}

void framebuffer_measure_text(const char *text, uint32_t scale,
                              uint64_t *width, uint64_t *height) {
    uint64_t current = 0;
    uint64_t maximum = 0;
    uint64_t lines = 1;
    if (text != NULL) {
        while (*text != '\0') {
            if (*text++ == '\n') {
                if (current > maximum) {
                    maximum = current;
                }
                current = 0;
                lines++;
            } else {
                current++;
            }
        }
    }
    if (current > maximum) {
        maximum = current;
    }
    if (width != NULL) {
        *width = maximum * 8 * scale;
    }
    if (height != NULL) {
        *height = lines * 8 * scale;
    }
}

void console_clear(uint32_t background) {
    state.background = background;
    framebuffer_clear(background);
    state.cursor_column = 1;
    state.cursor_row = 1;
}

void console_set_colors(uint32_t foreground, uint32_t background) {
    state.foreground = foreground;
    state.background = background;
}

void console_set_cursor(uint64_t column, uint64_t row) {
    state.cursor_column = column;
    state.cursor_row = row;
}

void console_get_cursor(uint64_t *column, uint64_t *row) {
    if (column != NULL) {
        *column = state.cursor_column;
    }
    if (row != NULL) {
        *row = state.cursor_row;
    }
}

static void console_scroll(void) {
    const uint64_t pixels = 8;
    if (!state.initialized || state.info.height <= pixels) {
        return;
    }
    for (uint64_t y = 0; y < state.info.height - pixels; y++) {
        volatile uint32_t *destination = (volatile uint32_t *)(state.address + y * state.info.pitch);
        volatile uint32_t *source = (volatile uint32_t *)(state.address + (y + pixels) * state.info.pitch);
        for (uint64_t x = 0; x < state.info.width; x++) {
            destination[x] = source[x];
        }
    }
    framebuffer_fill_rect(0, state.info.height - pixels, state.info.width, pixels,
                          state.background);
    if (state.cursor_row > 0) {
        state.cursor_row--;
    }
}

void console_write_n(const char *text, size_t length) {
    if (text == NULL) {
        return;
    }
    serial_write_n(text, length);
    if (!state.initialized) {
        return;
    }
    uint64_t columns = state.info.width / 8;
    uint64_t rows = state.info.height / 8;
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)text[i];
        if (character == '\r') {
            state.cursor_column = 0;
            continue;
        }
        if (character == '\n') {
            state.cursor_column = 0;
            state.cursor_row++;
        } else {
            framebuffer_fill_rect(state.cursor_column * 8, state.cursor_row * 8,
                                  8, 8, state.background);
            framebuffer_character(state.cursor_column * 8, state.cursor_row * 8,
                                  character, state.foreground, 1);
            state.cursor_column++;
            if (state.cursor_column >= columns) {
                state.cursor_column = 0;
                state.cursor_row++;
            }
        }
        if (state.cursor_row >= rows) {
            console_scroll();
        }
    }
}

void console_write(const char *text) {
    console_write_n(text, text == NULL ? 0 : strlen(text));
}

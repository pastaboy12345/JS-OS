#include <jsos/framebuffer.h>

#include <jsos/font8x8_basic.h>
#include <jsos/font.h>
#include <jsos/serial.h>
#include <jsos/vmware_svga.h>
#include <limine.h>
#include <string.h>

#define CONSOLE_CELL_WIDTH 8ULL
#define CONSOLE_CELL_HEIGHT 16ULL

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

static bool framebuffer_accelerated(void) {
    SvgaInfo info = svga_info();
    return info.available && info.width == state.info.width &&
           info.height == state.info.height && info.pitch == state.info.pitch &&
           info.bits_per_pixel == state.info.bpp;
}

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

static uint8_t color_channel(uint32_t color, uint8_t size, uint8_t shift) {
    if (size == 0) {
        return 0;
    }
    uint32_t mask = size >= 32 ? UINT32_MAX : ((1U << size) - 1U);
    uint32_t value = (color >> shift) & mask;
    return size >= 8 ? (uint8_t)value : (uint8_t)((value * 255U) / mask);
}

bool framebuffer_blend_pixel(int64_t x, int64_t y, uint32_t color, uint8_t alpha) {
    uint32_t destination;
    if (!framebuffer_get_pixel(x, y, &destination)) {
        return false;
    }
    if (alpha == 0) {
        return true;
    }
    if (alpha == 255) {
        return framebuffer_set_pixel(x, y, color);
    }
    uint32_t inverse = 255U - alpha;
    uint8_t red = (uint8_t)((color_channel(color, state.info.red_size,
        state.info.red_shift) * alpha + color_channel(destination,
        state.info.red_size, state.info.red_shift) * inverse + 127U) / 255U);
    uint8_t green = (uint8_t)((color_channel(color, state.info.green_size,
        state.info.green_shift) * alpha + color_channel(destination,
        state.info.green_size, state.info.green_shift) * inverse + 127U) / 255U);
    uint8_t blue = (uint8_t)((color_channel(color, state.info.blue_size,
        state.info.blue_shift) * alpha + color_channel(destination,
        state.info.blue_size, state.info.blue_shift) * inverse + 127U) / 255U);
    return framebuffer_set_pixel(x, y, framebuffer_rgb(red, green, blue));
}

void framebuffer_clear(uint32_t color) {
    if (!state.initialized) {
        return;
    }
    if (framebuffer_accelerated() &&
        svga_rect_fill(0, 0, (uint32_t)state.info.width,
                       (uint32_t)state.info.height, color)) {
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
    if ((end_x - start_x) * (end_y - start_y) >= 256 &&
        framebuffer_accelerated() &&
        svga_rect_fill((uint32_t)start_x, (uint32_t)start_y,
                       (uint32_t)(end_x - start_x), (uint32_t)(end_y - start_y),
                       color)) {
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

void framebuffer_fill_rounded_rect(int64_t x, int64_t y, int64_t width,
                                   int64_t height, int64_t radius,
                                   uint32_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    int64_t maximum_radius = (width < height ? width : height) / 2;
    if (radius < 0) radius = 0;
    if (radius > maximum_radius) radius = maximum_radius;
    if (radius == 0) {
        framebuffer_fill_rect(x, y, width, height, color);
        return;
    }
    framebuffer_fill_rect(x + radius, y, width - radius * 2, height, color);
    framebuffer_fill_rect(x, y + radius, width, height - radius * 2, color);
    framebuffer_circle(x + radius, y + radius, radius, color, true);
    framebuffer_circle(x + width - radius - 1, y + radius, radius, color, true);
    framebuffer_circle(x + radius, y + height - radius - 1, radius, color, true);
    framebuffer_circle(x + width - radius - 1, y + height - radius - 1,
                       radius, color, true);
}

static int64_t edge(int64_t ax, int64_t ay, int64_t bx, int64_t by,
                    int64_t px, int64_t py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void framebuffer_fill_triangle(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                               int64_t x2, int64_t y2, uint32_t color) {
    int64_t min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int64_t max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int64_t min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int64_t max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int64_t)state.info.width) max_x = (int64_t)state.info.width - 1;
    if (max_y >= (int64_t)state.info.height) max_y = (int64_t)state.info.height - 1;
    int64_t area = edge(x0, y0, x1, y1, x2, y2);
    for (int64_t y = min_y; y <= max_y; y++) {
        for (int64_t x = min_x; x <= max_x; x++) {
            int64_t a = edge(x0, y0, x1, y1, x, y);
            int64_t b = edge(x1, y1, x2, y2, x, y);
            int64_t c = edge(x2, y2, x0, y0, x, y);
            if ((area >= 0 && a >= 0 && b >= 0 && c >= 0) ||
                    (area < 0 && a <= 0 && b <= 0 && c <= 0)) {
                framebuffer_set_pixel(x, y, color);
            }
        }
    }
}

void framebuffer_put_rgba(int64_t x, int64_t y, uint32_t width, uint32_t height,
                          const uint8_t *pixels, size_t length) {
    if (pixels == NULL || width == 0 || height == 0 ||
            (uint64_t)width * height > length / 4) {
        return;
    }
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t column = 0; column < width; column++) {
            size_t offset = ((size_t)row * width + column) * 4;
            uint32_t color = framebuffer_rgb(pixels[offset], pixels[offset + 1],
                                             pixels[offset + 2]);
            framebuffer_blend_pixel(x + column, y + row, color, pixels[offset + 3]);
        }
    }
}

bool framebuffer_copy_rect(int64_t source_x, int64_t source_y,
                           int64_t destination_x, int64_t destination_y,
                           uint32_t width, uint32_t height) {
    if (!state.initialized || source_x < 0 || source_y < 0 ||
        destination_x < 0 || destination_y < 0 || !framebuffer_accelerated()) {
        return false;
    }
    return svga_rect_copy((uint32_t)source_x, (uint32_t)source_y,
                          (uint32_t)destination_x, (uint32_t)destination_y,
                          width, height);
}

void framebuffer_present(void) {
    if (svga_ready()) {
        svga_update(0, 0, (uint32_t)state.info.width, (uint32_t)state.info.height);
        svga_sync();
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
    if (font_draw_text(x, y, text, color, scale * 8)) {
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
    if (font_measure_text(text, scale * 8, width, height)) {
        return;
    }
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

void console_set_cursor_visible(bool visible) {
    if (!state.initialized) {
        return;
    }
    framebuffer_fill_rect(state.cursor_column * CONSOLE_CELL_WIDTH,
                          state.cursor_row * CONSOLE_CELL_HEIGHT,
                          CONSOLE_CELL_WIDTH, CONSOLE_CELL_HEIGHT,
                          visible ? state.foreground : state.background);
}

static void console_scroll(void) {
    const uint64_t pixels = CONSOLE_CELL_HEIGHT;
    if (!state.initialized || state.info.height <= pixels) {
        return;
    }
    if (!framebuffer_copy_rect(0, pixels, 0, 0, (uint32_t)state.info.width,
                               (uint32_t)(state.info.height - pixels))) {
        for (uint64_t y = 0; y < state.info.height - pixels; y++) {
            volatile uint32_t *destination = (volatile uint32_t *)(state.address + y * state.info.pitch);
            volatile uint32_t *source = (volatile uint32_t *)(state.address + (y + pixels) * state.info.pitch);
            for (uint64_t x = 0; x < state.info.width; x++) {
                destination[x] = source[x];
            }
        }
    }
    framebuffer_fill_rect(0, state.info.height - pixels, state.info.width, pixels,
                          state.background);
    if (state.cursor_row > 0) {
        state.cursor_row--;
    }
}

static void console_character(uint64_t column, uint64_t row,
                              unsigned char character) {
    if (character >= 128) {
        character = '?';
    }
    int64_t x = (int64_t)(column * CONSOLE_CELL_WIDTH);
    int64_t y = (int64_t)(row * CONSOLE_CELL_HEIGHT);
    framebuffer_fill_rect(x, y, CONSOLE_CELL_WIDTH, CONSOLE_CELL_HEIGHT,
                          state.background);
    for (uint32_t glyph_row = 0; glyph_row < 8; glyph_row++) {
        unsigned char bits = font8x8_basic[character][glyph_row];
        for (uint32_t glyph_column = 0; glyph_column < 8; glyph_column++) {
            if ((bits & (1U << glyph_column)) != 0) {
                framebuffer_fill_rect(x + glyph_column, y + glyph_row * 2,
                                      1, 2, state.foreground);
            }
        }
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
    uint64_t columns = state.info.width / CONSOLE_CELL_WIDTH;
    uint64_t rows = state.info.height / CONSOLE_CELL_HEIGHT;
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)text[i];
        if (character == '\b') {
            if (state.cursor_column > 0) {
                state.cursor_column--;
            } else if (state.cursor_row > 0 && columns > 0) {
                state.cursor_row--;
                state.cursor_column = columns - 1;
            }
            framebuffer_fill_rect(state.cursor_column * CONSOLE_CELL_WIDTH,
                                  state.cursor_row * CONSOLE_CELL_HEIGHT,
                                  CONSOLE_CELL_WIDTH, CONSOLE_CELL_HEIGHT,
                                  state.background);
            continue;
        }
        if (character == '\r') {
            state.cursor_column = 0;
            continue;
        }
        if (character == '\n') {
            state.cursor_column = 0;
            state.cursor_row++;
        } else {
            console_character(state.cursor_column, state.cursor_row, character);
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

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static const uint8_t *glyph(char c) {
    static const uint8_t blank[7] = { 0 };
    static const uint8_t comma[7] = { 0, 0, 0, 0, 0, 4, 8 };
    static const uint8_t bang[7] = { 4, 4, 4, 4, 4, 0, 4 };
    static const uint8_t b[7] = { 30, 17, 17, 30, 17, 17, 30 };
    static const uint8_t d[7] = { 30, 17, 17, 17, 17, 17, 30 };
    static const uint8_t e[7] = { 31, 16, 16, 30, 16, 16, 31 };
    static const uint8_t g[7] = { 14, 16, 16, 23, 17, 17, 14 };
    static const uint8_t h[7] = { 17, 17, 17, 31, 17, 17, 17 };
    static const uint8_t i[7] = { 14, 4, 4, 4, 4, 4, 14 };
    static const uint8_t m[7] = { 17, 27, 21, 21, 17, 17, 17 };
    static const uint8_t n[7] = { 17, 25, 21, 19, 17, 17, 17 };
    static const uint8_t o[7] = { 14, 17, 17, 17, 17, 17, 14 };
    static const uint8_t t[7] = { 31, 4, 4, 4, 4, 4, 4 };
    static const uint8_t y[7] = { 17, 17, 10, 4, 4, 4, 4 };

    switch (c) {
        case ',': return comma;
        case '!': return bang;
        case 'B': return b;
        case 'D': return d;
        case 'E': return e;
        case 'G': return g;
        case 'H': return h;
        case 'I': return i;
        case 'M': return m;
        case 'N': return n;
        case 'O': return o;
        case 'T': return t;
        case 'Y': return y;
        default: return blank;
    }
}

static uint32_t rgb(const struct limine_framebuffer *fb,
                    uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)red << fb->red_mask_shift)
         | ((uint32_t)green << fb->green_mask_shift)
         | ((uint32_t)blue << fb->blue_mask_shift);
}

static void put_pixel(const struct limine_framebuffer *fb, uint64_t x,
                      uint64_t y, uint32_t color) {
    volatile uint32_t *row = (volatile uint32_t *)
        ((uintptr_t)fb->address + y * fb->pitch);
    row[x] = color;
}

static void draw_message(const struct limine_framebuffer *fb) {
    static const char message[] = "GOODBYE, NIGHTTIME!";
    const uint64_t length = sizeof(message) - 1;
    const uint64_t scale = fb->width >= 800 ? 4 : 2;
    const uint64_t text_width = length * 6 * scale - scale;
    const uint64_t text_height = 7 * scale;
    const uint64_t start_x = fb->width > text_width
        ? (fb->width - text_width) / 2 : 0;
    const uint64_t start_y = fb->height > text_height
        ? (fb->height - text_height) / 2 : 0;
    const uint32_t background = rgb(fb, 16, 18, 20);
    const uint32_t foreground = rgb(fb, 235, 239, 244);

    for (uint64_t y = 0; y < fb->height; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            put_pixel(fb, x, y, background);
        }
    }

    if (text_width > fb->width || text_height > fb->height) {
        return;
    }

    for (uint64_t character = 0; character < length; character++) {
        const uint8_t *rows = glyph(message[character]);
        for (uint64_t row = 0; row < 7; row++) {
            for (uint64_t column = 0; column < 5; column++) {
                if ((rows[row] & (1u << (4 - column))) == 0) {
                    continue;
                }
                for (uint64_t dy = 0; dy < scale; dy++) {
                    for (uint64_t dx = 0; dx < scale; dx++) {
                        put_pixel(fb,
                            start_x + (character * 6 + column) * scale + dx,
                            start_y + row * scale + dy,
                            foreground);
                    }
                }
            }
        }
    }
}

void kmain(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED
            || framebuffer_request.response == NULL
            || framebuffer_request.response->framebuffer_count == 0) {
        hcf();
    }

    struct limine_framebuffer *fb =
        framebuffer_request.response->framebuffers[0];
    if (fb->memory_model == LIMINE_FRAMEBUFFER_RGB && fb->bpp == 32) {
        draw_message(fb);
    }

    hcf();
}

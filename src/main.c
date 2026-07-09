#include <stdint.h>
#include <stddef.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start_marker[] = { LIMINE_REQUESTS_START_MARKER };

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end_marker[] = { LIMINE_REQUESTS_END_MARKER };

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kmain(void) {
    volatile uint16_t *vga = (uint16_t *)0xB8000;
    const char *msg = "I'm not even going to say 'Hello, World!', because everyone does that, what about 'Goodbye, Nighttime'? Nah, too weird, ... whatever. goodbye.";

    for (size_t i = 0; msg[i]; i++) {
        vga[i] = (uint16_t)msg[i] | 0x0F00;
    }

    hcf();
}
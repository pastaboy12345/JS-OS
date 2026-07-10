#include <jsos/boot_script.h>
#include <jsos/framebuffer.h>
#include <jsos/font.h>
#include <jsos/heap.h>
#include <jsos/platform.h>
#include <jsos/runtime.h>
#include <jsos/serial.h>
#include <jsos/vmware_svga.h>

#include <limine.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_HEAP_SIZE (32ULL * 1024ULL * 1024ULL)

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_boot_time_request boot_time_request = {
    .id = LIMINE_BOOT_TIME_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request executable_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER

static uint8_t kernel_heap[KERNEL_HEAP_SIZE] __attribute__((aligned(16)));

void kmain(void) {
    serial_init();
    serial_write("JS-OS: booting\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        serial_write("JS-OS: unsupported Limine base revision\n");
        platform_halt();
    }

    uint64_t boot_epoch = 0;
    if (boot_time_request.response != NULL &&
            boot_time_request.response->boot_time > 0) {
        boot_epoch = (uint64_t)boot_time_request.response->boot_time;
    }
    platform_init(boot_epoch);
    serial_write("JS-OS: platform ready\n");

    if (hhdm_request.response != NULL && executable_address_request.response != NULL) {
        platform_configure_address_space(
            hhdm_request.response->offset,
            executable_address_request.response->physical_base,
            executable_address_request.response->virtual_base);
        svga_init(hhdm_request.response->offset);
    }
    serial_write("JS-OS: display probe complete\n");

    if (heap_init(kernel_heap, sizeof(kernel_heap)) < 0) {
        serial_write("JS-OS: unable to initialize kernel heap\n");
        platform_halt();
    }
    serial_write("JS-OS: heap ready\n");

    platform_run_constructors();
    serial_write("JS-OS: C++ runtime ready\n");

    if (framebuffer_request.response != NULL &&
            framebuffer_request.response->framebuffer_count != 0) {
        framebuffer_init(framebuffer_request.response->framebuffers[0]);
    }
    serial_write("JS-OS: framebuffer ready\n");

    if (!font_init()) {
        console_write("JS-OS: FreeType initialization failed; using bitmap text\n");
    }
    serial_write("JS-OS: FreeType ready\n");

    if (!js_runtime_start((const char *)jsos_colors_script_start,
                          jsos_colors_script_length(), "colors.js") ||
            !js_runtime_run((const char *)jsos_graphics_script_start,
                            jsos_graphics_script_length(), "graphics.js") ||
            !js_runtime_run((const char *)jsos_dom_script_start,
                            jsos_dom_script_length(), "dom.js") ||
            !js_runtime_run((const char *)jsos_react_script_start,
                            jsos_react_script_length(), "react.js") ||
            !js_runtime_run((const char *)jsos_boot_script_start,
                            jsos_boot_script_length(), "boot.js")) {
        console_write("JS-OS: JavaScript boot failed\n");
        platform_halt();
    }
    serial_write("JS-OS: JavaScript environment ready\n");

    js_runtime_shell();
}

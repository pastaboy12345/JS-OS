#include <jsos/platform.h>

#include <stddef.h>
#include <string.h>

static uint64_t boot_tsc;
static uint64_t tsc_frequency = 1000000000ULL;
static uint64_t boot_epoch;

uint8_t platform_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint16_t platform_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t platform_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void platform_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void platform_out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

void platform_out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

CpuidResult platform_cpuid(uint32_t leaf, uint32_t subleaf) {
    CpuidResult result;
    __asm__ volatile ("cpuid"
                      : "=a"(result.eax), "=b"(result.ebx),
                        "=c"(result.ecx), "=d"(result.edx)
                      : "a"(leaf), "c"(subleaf));
    return result;
}

uint64_t platform_rdtsc(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static uint64_t detect_tsc_frequency(void) {
    CpuidResult maximum = platform_cpuid(0, 0);
    if (maximum.eax >= 0x15) {
        CpuidResult ratio = platform_cpuid(0x15, 0);
        if (ratio.eax != 0 && ratio.ebx != 0 && ratio.ecx != 0) {
            return ((uint64_t)ratio.ecx * ratio.ebx) / ratio.eax;
        }
    }
    if (maximum.eax >= 0x16) {
        CpuidResult frequency = platform_cpuid(0x16, 0);
        if (frequency.eax != 0) {
            return (uint64_t)frequency.eax * 1000000ULL;
        }
    }
    return 1000000000ULL;
}

static void initialize_floating_point(void) {
    uint64_t cr0;
    uint64_t cr4;
    uint32_t mxcsr = 0x1f80;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= 1ULL << 1;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));
    __asm__ volatile ("fninit");
    __asm__ volatile ("ldmxcsr %0" : : "m"(mxcsr));
}

void platform_init(uint64_t boot_epoch_seconds) {
    initialize_floating_point();
    tsc_frequency = detect_tsc_frequency();
    if (tsc_frequency == 0) {
        tsc_frequency = 1000000000ULL;
    }
    boot_epoch = boot_epoch_seconds;
    boot_tsc = platform_rdtsc();
}

uint64_t platform_tsc_hz(void) {
    return tsc_frequency;
}

uint64_t platform_uptime_us(void) {
    uint64_t elapsed = platform_rdtsc() - boot_tsc;
    uint64_t seconds = elapsed / tsc_frequency;
    uint64_t remainder = elapsed % tsc_frequency;
    return seconds * 1000000ULL + (remainder * 1000000ULL) / tsc_frequency;
}

uint64_t platform_unix_us(void) {
    return boot_epoch * 1000000ULL + platform_uptime_us();
}

void platform_sleep_ms(uint64_t milliseconds) {
    uint64_t deadline = platform_uptime_us() + milliseconds * 1000ULL;
    while (platform_uptime_us() < deadline) {
        __asm__ volatile ("pause");
    }
}

void platform_cpu_vendor(char output[13]) {
    CpuidResult result = platform_cpuid(0, 0);
    memcpy(output + 0, &result.ebx, sizeof(result.ebx));
    memcpy(output + 4, &result.edx, sizeof(result.edx));
    memcpy(output + 8, &result.ecx, sizeof(result.ecx));
    output[12] = '\0';
}

void platform_cpu_brand(char output[49]) {
    CpuidResult maximum = platform_cpuid(0x80000000, 0);
    memset(output, 0, 49);
    if (maximum.eax < 0x80000004) {
        return;
    }
    for (uint32_t leaf = 0; leaf < 3; leaf++) {
        CpuidResult part = platform_cpuid(0x80000002 + leaf, 0);
        memcpy(output + leaf * 16 + 0, &part.eax, 4);
        memcpy(output + leaf * 16 + 4, &part.ebx, 4);
        memcpy(output + leaf * 16 + 8, &part.ecx, 4);
        memcpy(output + leaf * 16 + 12, &part.edx, 4);
    }
    output[48] = '\0';
}

_Noreturn void platform_halt(void) {
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

_Noreturn void platform_reboot(void) {
    for (unsigned int spin = 0; spin < 100000; spin++) {
        if ((platform_in8(0x64) & 0x02) == 0) {
            break;
        }
    }
    platform_out8(0x64, 0xfe);
    platform_halt();
}

_Noreturn void platform_shutdown(void) {
    platform_out16(0x604, 0x2000);
    platform_out16(0xb004, 0x2000);
    platform_out16(0x4004, 0x3400);
    platform_halt();
}

_Noreturn void platform_debug_exit(uint8_t code) {
    platform_out32(0xf4, code);
    platform_halt();
}

#include <jsos/platform.h>

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define PAGE_SIZE 4096ULL

static uint64_t boot_tsc;
static uint64_t tsc_frequency = 1000000000ULL;
static uint64_t boot_epoch;
static uint64_t direct_map_offset;
static uint64_t executable_physical_base;
static uint64_t executable_virtual_base;
static uint8_t bootstrap_tls[PAGE_SIZE] __attribute__((aligned(16)));

#define PAGE_ADDRESS_MASK 0x000ffffffffff000ULL
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITE 0x002ULL
#define PAGE_WRITE_THROUGH 0x008ULL
#define PAGE_CACHE_DISABLE 0x010ULL
#define PAGE_HUGE 0x080ULL
#define MMIO_TABLE_COUNT 64

static uint8_t mmio_page_tables[MMIO_TABLE_COUNT][PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static size_t mmio_page_table_count;

typedef void (*Initializer)(void);
extern Initializer __init_array_start[];
extern Initializer __init_array_end[];

void platform_run_constructors(void) {
    for (Initializer *initializer = __init_array_start;
         initializer < __init_array_end; initializer++) {
        (*initializer)();
    }
}

void platform_configure_address_space(uint64_t hhdm_offset,
                                      uint64_t kernel_physical_base,
                                      uint64_t kernel_virtual_base) {
    direct_map_offset = hhdm_offset;
    executable_physical_base = kernel_physical_base;
    executable_virtual_base = kernel_virtual_base;
}

static uint64_t table_physical_address(const void *table) {
    return (uint64_t)(uintptr_t)table - executable_virtual_base +
           executable_physical_base;
}

static volatile uint64_t *allocate_page_table(void) {
    if (mmio_page_table_count >= MMIO_TABLE_COUNT) return NULL;
    void *table = mmio_page_tables[mmio_page_table_count++];
    memset(table, 0, PAGE_SIZE);
    return table;
}

static bool map_mmio_page(uint64_t virtual_address, uint64_t physical_address) {
    uint64_t cr3;
    uint64_t cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    unsigned int levels = (cr4 & (1ULL << 12)) != 0 ? 5 : 4;
    volatile uint64_t *table = (volatile uint64_t *)(uintptr_t)
        (direct_map_offset + (cr3 & PAGE_ADDRESS_MASK));

    for (unsigned int level = levels; level > 1; level--) {
        unsigned int shift = 12 + (level - 1) * 9;
        size_t index = (virtual_address >> shift) & 0x1ff;
        uint64_t entry = table[index];
        if ((entry & PAGE_PRESENT) != 0) {
            if ((entry & PAGE_HUGE) != 0) {
                uint64_t page_size = 1ULL << shift;
                uint64_t mapped = (entry & ~(page_size - 1)) |
                                  (virtual_address & (page_size - 1));
                return (mapped & ~(PAGE_SIZE - 1)) == physical_address;
            }
            table = (volatile uint64_t *)(uintptr_t)
                (direct_map_offset + (entry & PAGE_ADDRESS_MASK));
            continue;
        }
        volatile uint64_t *next = allocate_page_table();
        if (next == NULL) return false;
        table[index] = table_physical_address((const void *)next) |
                       PAGE_PRESENT | PAGE_WRITE;
        table = next;
    }

    size_t index = (virtual_address >> 12) & 0x1ff;
    table[index] = physical_address | PAGE_PRESENT | PAGE_WRITE |
                   PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return true;
}

void *platform_map_mmio(uint64_t physical_address, uint64_t length) {
    if (direct_map_offset == 0 || executable_virtual_base == 0 || length == 0 ||
        physical_address > UINT64_MAX - length) return NULL;
    uint64_t start = physical_address & ~(PAGE_SIZE - 1);
    uint64_t end = (physical_address + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
        if (!map_mmio_page(direct_map_offset + page, page)) return NULL;
    }
    return (void *)(uintptr_t)(direct_map_offset + physical_address);
}

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
    uint64_t tls_base = (uint64_t)(uintptr_t)bootstrap_tls;
    uint32_t tls_low = (uint32_t)tls_base;
    uint32_t tls_high = (uint32_t)(tls_base >> 32);
    __asm__ volatile ("wrmsr" : : "c"(0xc0000100U), "a"(tls_low), "d"(tls_high));
    *(uint64_t *)(void *)(bootstrap_tls + 0x28) = platform_rdtsc() ^ 0x9e3779b97f4a7c15ULL;
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

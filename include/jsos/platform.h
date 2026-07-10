#ifndef JSOS_PLATFORM_H
#define JSOS_PLATFORM_H

#include <stdint.h>

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} CpuidResult;

void platform_init(uint64_t boot_epoch_seconds);
void platform_run_constructors(void);
void platform_configure_address_space(uint64_t hhdm_offset,
                                      uint64_t kernel_physical_base,
                                      uint64_t kernel_virtual_base);
void *platform_map_mmio(uint64_t physical_address, uint64_t length);
CpuidResult platform_cpuid(uint32_t leaf, uint32_t subleaf);
uint64_t platform_rdtsc(void);
uint64_t platform_tsc_hz(void);
uint64_t platform_uptime_us(void);
uint64_t platform_unix_us(void);
void platform_sleep_ms(uint64_t milliseconds);
void platform_cpu_vendor(char output[13]);
void platform_cpu_brand(char output[49]);

uint8_t platform_in8(uint16_t port);
uint16_t platform_in16(uint16_t port);
uint32_t platform_in32(uint16_t port);
void platform_out8(uint16_t port, uint8_t value);
void platform_out16(uint16_t port, uint16_t value);
void platform_out32(uint16_t port, uint32_t value);

_Noreturn void platform_halt(void);
_Noreturn void platform_reboot(void);
_Noreturn void platform_shutdown(void);
_Noreturn void platform_debug_exit(uint8_t code);

#endif

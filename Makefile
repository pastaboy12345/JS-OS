CC := x86_64-elf-gcc
CXX := x86_64-elf-g++
HOST_CXX := g++
BUILD_DIR := build

HOST_CXX_VERSION := $(shell $(HOST_CXX) -dumpfullversion)
HOST_CXX_MACHINE := $(shell $(HOST_CXX) -dumpmachine)
HOST_CXX_INCLUDE := /usr/include/c++/$(HOST_CXX_VERSION)
HOST_LIBSTDCXX := $(shell $(HOST_CXX) -print-file-name=libstdc++.a)

CPPFLAGS := \
	-Iinclude/jsos/libc \
	-Iinclude \
	-Ilimine \
	-Ivendor/quickjs \
	-Ivendor/freetype/include \
	-Ivendor/litehtml/src/gumbo/include \
	-Ivendor/litehtml/src/gumbo/include/gumbo \
	-Ivendor/openlibm \
	-Ivendor/openlibm/include \
	-Ivendor/openlibm/src \
	-Ivendor/openlibm/amd64 \
	-DJSOS_FREESTANDING \
	-DLITEHTML_NO_THREADS \
	-DFT2_BUILD_LIBRARY \
	-DCONFIG_VERSION=\"2025-09-13\"
CXX_CPPFLAGS := \
	-Iinclude \
	-Ilimine \
	-Ivendor/freetype/include \
	-DJSOS_FREESTANDING \
	-DLITEHTML_NO_THREADS \
	-DCONFIG_VERSION=\"2025-09-13\"
CFLAGS := \
	-std=gnu11 \
	-O2 \
	-g \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-fno-omit-frame-pointer \
	-mno-red-zone \
	-mcmodel=kernel \
	-Wall \
	-Wextra \
	-Wno-unused-parameter \
	-Wno-sign-compare
CXXFLAGS := \
	-std=gnu++17 \
	-O2 \
	-g \
	-ffreestanding \
	-U__STDC_HOSTED__ \
	-D__STDC_HOSTED__=1 \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-fno-omit-frame-pointer \
	-mno-red-zone \
	-mcmodel=kernel \
	-nostdinc++ \
	-I$(HOST_CXX_INCLUDE) \
	-I$(HOST_CXX_INCLUDE)/$(HOST_CXX_MACHINE) \
	-isystem /usr/include/$(HOST_CXX_MACHINE) \
	-isystem /usr/include \
	-Wall \
	-Wno-extra \
	-Wno-builtin-macro-redefined
ASFLAGS := $(CFLAGS)
LDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 -T linker.ld

KERNEL_C_SOURCES := $(wildcard src/*.c src/kernel/*.c)
KERNEL_S_SOURCES := $(wildcard src/kernel/*.S)
KERNEL_CXX_SOURCES := $(wildcard src/kernel/*.cpp)
QUICKJS_SOURCES := \
	vendor/quickjs/quickjs.c \
	vendor/quickjs/cutils.c \
	vendor/quickjs/dtoa.c \
	vendor/quickjs/libregexp.c \
	vendor/quickjs/libunicode.c
OPENLIBM_C_SOURCES := $(wildcard vendor/openlibm/src/*.c)
OPENLIBM_S_SOURCES := vendor/openlibm/amd64/s_lrint.S
FREETYPE_SOURCES := \
	vendor/freetype/src/base/ftsystem.c \
	vendor/freetype/src/base/ftdebug.c \
	vendor/freetype/src/base/ftinit.c \
	vendor/freetype/src/base/ftbase.c \
	vendor/freetype/src/base/ftbitmap.c \
	vendor/freetype/src/base/ftmm.c \
	vendor/freetype/src/autofit/autofit.c \
	vendor/freetype/src/truetype/truetype.c \
	vendor/freetype/src/sfnt/sfnt.c \
	vendor/freetype/src/smooth/smooth.c \
	vendor/freetype/src/psnames/psnames.c
LITEHTML_CXX_SOURCES := $(wildcard vendor/litehtml/src/*.cpp)
GUMBO_C_SOURCES := $(wildcard vendor/litehtml/src/gumbo/*.c)

C_SOURCES := $(KERNEL_C_SOURCES) $(QUICKJS_SOURCES) $(OPENLIBM_C_SOURCES) $(FREETYPE_SOURCES) $(GUMBO_C_SOURCES)
CXX_SOURCES := $(KERNEL_CXX_SOURCES) $(LITEHTML_CXX_SOURCES)
S_SOURCES := $(KERNEL_S_SOURCES) $(OPENLIBM_S_SOURCES)
OBJECTS := \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) \
	$(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CXX_SOURCES)) \
	$(patsubst %.S,$(BUILD_DIR)/%.o,$(S_SOURCES))
DEPENDENCIES := $(OBJECTS:.o=.d)
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

.PHONY: all clean run run-headless

all: quickboot.iso

kernel.elf: $(OBJECTS) linker.ld
	$(CXX) $(LDFLAGS) $(OBJECTS) $(HOST_LIBSTDCXX) $(LIBGCC) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXX_CPPFLAGS) $(CXXFLAGS) \
		-Ivendor/litehtml/include -Ivendor/litehtml/include/litehtml \
		-Ivendor/litehtml/src -Ivendor/litehtml/src/gumbo/include \
		-MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.S $(wildcard src/js/*.js) vendor/font/DejaVuSans.ttf
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

quickboot.iso: kernel.elf limine.conf
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp kernel.elf iso_root/kernel.elf
	cp limine.conf iso_root/boot/limine/limine.conf
	cp limine/limine-bios.sys iso_root/boot/limine/
	cp limine/limine-bios-cd.bin iso_root/boot/limine/
	cp limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o quickboot.iso
	./limine/limine bios-install quickboot.iso

run: quickboot.iso
	qemu-system-x86_64 -M q35 -m 256M -vga vmware -cdrom quickboot.iso \
		-serial none -monitor none

run-headless: quickboot.iso
	qemu-system-x86_64 -M q35 -m 256M -vga vmware -cdrom quickboot.iso \
		-display none -serial stdio -monitor none

clean:
	rm -rf $(BUILD_DIR) iso_root kernel.elf quickboot.iso

-include $(DEPENDENCIES)

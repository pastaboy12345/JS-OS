CC = x86_64-elf-gcc
CFLAGS = -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel -Wall -Wextra -Ilimine
LDFLAGS = -nostdlib -z max-page-size=0x1000 -T linker.ld

all: quickboot.iso

kernel.elf: src/main.c linker.ld
	$(CC) $(CFLAGS) -c src/main.c -o main.o
	$(CC) $(LDFLAGS) main.o -o kernel.elf

quickboot.iso: kernel.elf limine.conf
	rm -rf iso_root
	mkdir -p iso_root/boot/limine
	cp kernel.elf iso_root/kernel.elf
	cp limine.conf iso_root/boot/limine/limine.conf
	cp limine/limine-bios.sys iso_root/boot/limine/
	cp limine/limine-bios-cd.bin iso_root/boot/limine/
	cp limine/limine-uefi-cd.bin iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o quickboot.iso
	./limine/limine bios-install quickboot.iso

run: quickboot.iso
	qemu-system-x86_64 -cdrom quickboot.iso

clean:
	rm -rf iso_root *.o kernel.elf quickboot.iso

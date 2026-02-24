ARCH := i686
CC := i686-elf-gcc
LD := i686-elf-ld
AS := nasm
PY := python3

CFLAGS := -std=c11 -O2 -Wall -Wextra \
          -ffreestanding -fno-stack-protector -fno-pic -m32

LDFLAGS := -T linker.ld -nostdlib

ISO_DIR := iso
BUILD_DIR := build

KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ISO_IMAGE := $(BUILD_DIR)/DiellOS.iso
INITRD_IMG := $(BUILD_DIR)/initrd.img

OBJS := $(BUILD_DIR)/boot.o \
        $(BUILD_DIR)/kernel.o \
        $(BUILD_DIR)/vga.o \
        $(BUILD_DIR)/gdt.o \
        $(BUILD_DIR)/gdt_flush.o \
        $(BUILD_DIR)/idt.o \
        $(BUILD_DIR)/idt_load.o \
        $(BUILD_DIR)/isr.o \
        $(BUILD_DIR)/isr_stubs.o \
        $(BUILD_DIR)/io.o \
        $(BUILD_DIR)/pic.o \
        $(BUILD_DIR)/irq.o \
        $(BUILD_DIR)/irq_stubs.o \
        $(BUILD_DIR)/timer.o \
        $(BUILD_DIR)/keyboard.o \
        $(BUILD_DIR)/print.o \
        $(BUILD_DIR)/panic.o \
        $(BUILD_DIR)/console.o \
        $(BUILD_DIR)/shell.o \
        $(BUILD_DIR)/string.o \
        $(BUILD_DIR)/vfs.o \
        $(BUILD_DIR)/initrd.o \
        $(BUILD_DIR)/ata.o \
        $(BUILD_DIR)/mbr.o \
        $(BUILD_DIR)/donut.o \
        $(BUILD_DIR)/partition.o \
        $(BUILD_DIR)/fat16.o \
        $(BUILD_DIR)/paging.o

all: $(ISO_IMAGE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.o: src/boot.s | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/kernel.o: src/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: src/vga.c src/vga.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: src/arch/i386/gdt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt_flush.o: src/arch/i386/gdt_flush.s | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/idt.o: src/arch/i386/idt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt_load.o: src/arch/i386/idt_load.s | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/isr.o: src/arch/i386/isr.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/isr_stubs.o: src/arch/i386/isr_stubs.s | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/io.o: src/arch/i386/io.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: src/arch/i386/pic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/irq.o: src/arch/i386/irq.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/irq_stubs.o: src/arch/i386/irq_stubs.s | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/timer.o: src/drivers/timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: src/drivers/keyboard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/print.o: src/debug/print.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/panic.o: src/debug/panic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: src/console.c src/console.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: src/shell.c src/shell.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/string.o: src/lib/string.c src/lib/string.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: src/fs/vfs.c src/fs/vfs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/initrd.o: src/fs/initrd.c src/fs/initrd.h src/boot/multiboot.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ata.o: src/drivers/ata.c src/drivers/ata.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mbr.o: src/disk/mbr.c src/disk/mbr.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/donut.o: src/apps/donut.c src/apps/donut.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/partition.o: src/disk/partition.c src/disk/partition.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fat16.o: src/fs/fat16.c src/fs/fat16.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: src/memory/paging.c src/memory/paging.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(INITRD_IMG): tools/mkinitrd.py initrd_root/README.TXT | $(BUILD_DIR)
	$(PY) tools/mkinitrd.py initrd_root $(INITRD_IMG)

$(ISO_IMAGE): $(KERNEL_ELF) $(INITRD_IMG)
	mkdir -p $(ISO_DIR)/boot
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	cp $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR)

check: $(KERNEL_ELF)
	@echo "Checking multiboot header..."
	grub-file --is-x86-multiboot $(KERNEL_ELF) && echo "OK: multiboot detected" || echo "FAIL: no multiboot header"

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/initrd.img

.PHONY: all clean check
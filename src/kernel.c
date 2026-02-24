#include <stdint.h>
#include "vga.h"
#include "console.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/irq.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "shell.h"
#include "memory/paging.h"

#include "boot/multiboot.h"
#include "fs/initrd.h"

#include "drivers/ata.h"



void kmain(uint32_t mb_magic, uint32_t mb_info_addr)
{
    gdt_init();
    idt_init();
    irq_init();

paging_init();

    vga_clear();
    vga_puts("DiellOS v0.5 Console\n");

    console_init();

    if (mb_magic == MB_BOOTLOADER_MAGIC) {
        const multiboot_info_t* mb = (const multiboot_info_t*)(uintptr_t)mb_info_addr;
        initrd_mount_from_multiboot(mb);
    } else {
        vga_puts("[boot] invalid multiboot magic; initrd skipped\n");
    }

    timer_init(100);
    keyboard_init();

    ata_init();
    if (ata_present()) {
        vga_puts("[ata] primary master: ");
        vga_puts(ata_model());
        vga_putc('\n');
    } else {
        vga_puts("[ata] no primary master detected\n");
    }

    __asm__ volatile("sti");

    for (;;)
    {
        char line[128];

        vga_puts("\nDiellOS> ");
        console_begin_input();
        console_readline(line, sizeof(line));

        console_clear_cancel();
        shell_execute(line);
        console_clear_cancel();
    }
}
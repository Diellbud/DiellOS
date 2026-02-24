#pragma once
#include <stdint.h>

#define MB_BOOTLOADER_MAGIC 0x2BADB002u
#define MB_INFO_MODS        (1u << 3)

typedef struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

typedef struct multiboot_info {
    uint32_t flags;

    uint32_t mem_lower;
    uint32_t mem_upper;

    uint32_t boot_device;
    uint32_t cmdline;

    uint32_t mods_count;
    uint32_t mods_addr;
} __attribute__((packed)) multiboot_info_t;

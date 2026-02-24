#include "initrd.h"
#include "vfs.h"
#include "../vga.h"
#include "../debug/print.h"
#include "../lib/string.h"

#define INITRD_MAGIC 0x44495244u

typedef struct initrd_header {
    uint32_t magic;
    uint32_t nfiles;
} __attribute__((packed)) initrd_header_t;

typedef struct initrd_file {
    char     name[32];
    uint32_t offset;
    uint32_t length;
} __attribute__((packed)) initrd_file_t;

typedef struct initrd_ctx {
    const uint8_t* base;
    uint32_t size;
    const initrd_header_t* hdr;
    const initrd_file_t* files;
} initrd_ctx_t;

static initrd_ctx_t g_ctx;
static vfs_node_t g_nodes[VFS_MAX_NODES];

static size_t initrd_read(vfs_node_t* node, size_t offset, size_t size, uint8_t* out) {
    if (!node || !node->impl) return 0;
    const initrd_file_t* f = (const initrd_file_t*)node->impl;

    if (offset >= f->length) return 0;
    uint32_t remaining = f->length - (uint32_t)offset;
    if (size > remaining) size = remaining;

    const uint8_t* src = g_ctx.base + f->offset + (uint32_t)offset;
    kmemcpy(out, src, size);
    return size;
}

static int initrd_validate(const uint8_t* base, uint32_t size) {
    if (!base || size < sizeof(initrd_header_t)) return 0;
    const initrd_header_t* h = (const initrd_header_t*)base;
    if (h->magic != INITRD_MAGIC) return 0;

    uint32_t dir_size = sizeof(initrd_header_t) + h->nfiles * sizeof(initrd_file_t);
    if (dir_size > size) return 0;

    const initrd_file_t* files = (const initrd_file_t*)(base + sizeof(initrd_header_t));
    for (uint32_t i = 0; i < h->nfiles; i++) {
        uint32_t end = files[i].offset + files[i].length;
        if (files[i].offset < dir_size) return 0;
        if (end > size) return 0;
        if (files[i].name[31] != '\0') return 0;
    }
    return 1;
}

int initrd_mount_from_multiboot(const multiboot_info_t* mb) {
    if (!mb) return -1;

    if ((mb->flags & MB_INFO_MODS) == 0 || mb->mods_count == 0) {
        vga_puts("[fs] no multiboot modules; initrd not mounted\n");
        return -1;
    }

    const multiboot_module_t* mods = (const multiboot_module_t*)(uintptr_t)mb->mods_addr;
    const multiboot_module_t* m0 = &mods[0];

    const uint8_t* base = (const uint8_t*)(uintptr_t)m0->mod_start;
    uint32_t size = m0->mod_end - m0->mod_start;

    if (!initrd_validate(base, size)) {
        vga_puts("[fs] initrd invalid\n");
        return -1;
    }

    const initrd_header_t* hdr = (const initrd_header_t*)base;
    const initrd_file_t* files = (const initrd_file_t*)(base + sizeof(initrd_header_t));

    g_ctx.base = base;
    g_ctx.size = size;
    g_ctx.hdr = hdr;
    g_ctx.files = files;

    vfs_init();

    uint32_t n = hdr->nfiles;
    if (n > VFS_MAX_NODES) n = VFS_MAX_NODES;

    for (uint32_t i = 0; i < n; i++) {
        vfs_node_t* node = &g_nodes[i];
        kmemset(node, 0, sizeof(*node));
        kstrncpy(node->name, files[i].name, sizeof(node->name));
        node->type = VFS_NODE_FILE;
        node->size = files[i].length;
        node->read = initrd_read;
        node->impl = (void*)&files[i];
        vfs_register(node);
    }

    vga_puts("[fs] initrd mounted: files=");
    kprint_dec(hdr->nfiles);
    vga_putc('\n');
    return 0;
}

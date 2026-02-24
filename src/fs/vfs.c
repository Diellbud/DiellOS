#include "vfs.h"
#include "../vga.h"
#include "../debug/print.h"
#include "../lib/string.h"

static vfs_node_t* g_nodes[VFS_MAX_NODES];
static unsigned g_node_count = 0;

void vfs_init(void) {
    g_node_count = 0;
    for (unsigned i = 0; i < VFS_MAX_NODES; i++) g_nodes[i] = 0;
}

int vfs_register(vfs_node_t* node) {
    if (!node) return -1;
    if (g_node_count >= VFS_MAX_NODES) return -1;
    g_nodes[g_node_count++] = node;
    return 0;
}

vfs_node_t* vfs_open(const char* name) {
    if (!name || !*name) return 0;
    for (unsigned i = 0; i < g_node_count; i++) {
        if (g_nodes[i] && kstrcmp(g_nodes[i]->name, name) == 0) return g_nodes[i];
    }
    return 0;
}

size_t vfs_read(vfs_node_t* node, size_t offset, size_t size, uint8_t* out) {
    if (!node || node->type != VFS_NODE_FILE || !node->read) return 0;
    return node->read(node, offset, size, out);
}

void vfs_list(void) {
    if (g_node_count == 0) {
        vga_puts("(empty)\n");
        return;
    }
    for (unsigned i = 0; i < g_node_count; i++) {
        vfs_node_t* n = g_nodes[i];
        if (!n) continue;
        vga_puts(n->name);
        vga_puts("  ");
        kprint_dec(n->size);
        vga_puts(" bytes\n");
    }
}

#pragma once
#include <stdint.h>
#include <stddef.h>

#define VFS_MAX_NODES 64

typedef enum {
    VFS_NODE_FILE = 1,
} vfs_node_type_t;

struct vfs_node;
typedef size_t (*vfs_read_fn)(struct vfs_node* node, size_t offset, size_t size, uint8_t* out);

typedef struct vfs_node {
    char name[32];
    vfs_node_type_t type;
    uint32_t size;
    vfs_read_fn read;
    void* impl;
} vfs_node_t;

void vfs_init(void);
int  vfs_register(vfs_node_t* node);
vfs_node_t* vfs_open(const char* name);
void vfs_list(void);
size_t vfs_read(vfs_node_t* node, size_t offset, size_t size, uint8_t* out);

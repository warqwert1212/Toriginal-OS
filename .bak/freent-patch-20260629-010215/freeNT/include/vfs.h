#pragma once
#include <stdint.h>
#include <stddef.h>

#define FS_FILE      1
#define FS_DIRECTORY 2

struct vfs_node {
    char name[128];
    uint32_t type;
    uint32_t size;
    uint32_t (*read)(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    uint32_t (*write)(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    struct vfs_node* next;
};

void vfs_init(void);
void vfs_register_root(struct vfs_node* root_node);
uint32_t vfs_read(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);

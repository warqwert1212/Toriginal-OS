// ==============================================================================
// VFS.C - System Abstract Mount Node Dispatcher
// ==============================================================================
#include "vfs.h"

void print_serial(const char* str);

static struct vfs_node* fs_root = NULL;

void vfs_init(void) {
    print_serial("[VFS] Unified Virtual File System System Layer active.\n");
}

void vfs_register_root(struct vfs_node* root_node) {
    fs_root = root_node;
    print_serial("[VFS] Core Mount Root point registered successfully.\n");
}

uint32_t vfs_read(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node && node->read) {
        return node->read(node, offset, size, buffer);
    }
    return 0;
}
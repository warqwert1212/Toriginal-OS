#include "fs.h"
#include "mm.h"
#include "string.h"
#include "io.h"

#define MAX_VFS_ENTRIES 128
#define MAX_VFS_PATH_LEN 256

/* VFS entry mapping from full path to inode */
typedef struct {
    int in_use;
    char path[MAX_VFS_PATH_LEN];
    inode_t *inode;
    int is_dir;
} vfs_entry_t;

/* VFS state */
static inode_t *root_inode = NULL;
static inode_t *current_dir = NULL;
static superblock_t superblock = {0};
static vfs_entry_t vfs_entries[MAX_VFS_ENTRIES];

/* File descriptor table */
typedef struct {
    inode_t *inode;
    uint64_t position;
    int flags;
} file_descriptor_t;

static file_descriptor_t *fd_table = NULL;
static uint32_t fd_count = 0;

static inode_t *fs_create_inode(int mode) {
    inode_t *inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(inode_t));
    inode->ino = superblock.total_inodes++;
    inode->mode = mode;
    inode->uid = 0;
    inode->gid = 0;
    inode->size = 0;
    inode->accessed_time = 0;
    inode->modified_time = 0;
    inode->created_time = 0;
    inode->block_count = 0;
    inode->link_count = 1;
    inode->data = NULL;
    inode->indirect_block = 0;
    inode->double_indirect = 0;
    inode->triple_indirect = 0;
    return inode;
}

static int fs_register_path(const char *path, inode_t *inode, int is_dir) {
    if (!path || !inode)
        return -1;

    for (int i = 0; i < MAX_VFS_ENTRIES; ++i) {
        if (!vfs_entries[i].in_use) {
            vfs_entries[i].in_use = 1;
            strncpy(vfs_entries[i].path, path, MAX_VFS_PATH_LEN - 1);
            vfs_entries[i].path[MAX_VFS_PATH_LEN - 1] = '\0';
            vfs_entries[i].inode = inode;
            vfs_entries[i].is_dir = is_dir;
            return 0;
        }
    }

    return -1;
}

inode_t *fs_resolve_path(const char *path) {
    if (!path || !path[0])
        return current_dir;

    if (path[0] == '/') {
        if (path[1] == '\0')
            return root_inode;
    }

    for (int i = 0; i < MAX_VFS_ENTRIES; ++i) {
        if (!vfs_entries[i].in_use)
            continue;
        if (strcmp(vfs_entries[i].path, path) == 0)
            return vfs_entries[i].inode;
    }

    return NULL;
}

void fs_init(void) {
    fd_table = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t) * MAX_FD_PER_PROCESS);
    memset(fd_table, 0, sizeof(file_descriptor_t) * MAX_FD_PER_PROCESS);
    fd_count = 0;
    memset(vfs_entries, 0, sizeof(vfs_entries));

    superblock.magic = 0x12345678;
    superblock.version = 1;
    superblock.total_blocks = 1000000;
    superblock.free_blocks = 1000000;
    superblock.total_inodes = 1;
    superblock.free_inodes = 100000;
    superblock.block_size = 4096;
    superblock.creation_time = 0;
    superblock.mount_count = 1;

    root_inode = fs_create_inode(FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X | FILE_TYPE_DIR);
    current_dir = root_inode;
    fs_register_path("/", root_inode, 1);
}

fd_t fs_open(const char *path, int flags, int mode) {
    if (!path || fd_count >= MAX_FD_PER_PROCESS)
        return -1;

    inode_t *inode = fs_resolve_path(path);
    if (!inode && (flags & O_CREAT)) {
        inode = fs_create_inode(mode);
        if (!inode)
            return -1;
        if (fs_register_path(path, inode, 0) != 0) {
            kfree(inode);
            return -1;
        }
    }

    if (!inode)
        return -1;

    if ((flags & O_TRUNC) && inode->data) {
        kfree(inode->data);
        inode->data = NULL;
        inode->size = 0;
        inode->block_count = 0;
    }

    fd_t fd = fd_count;
    fd_table[fd].inode = inode;
    fd_table[fd].position = 0;
    fd_table[fd].flags = flags;
    fd_count++;

    return fd;
}

int fs_close(fd_t fd) {
    if (fd < 0 || fd >= (fd_t)fd_count)
        return -1;

    fd_table[fd].inode = NULL;
    fd_table[fd].position = 0;
    fd_table[fd].flags = 0;
    return 0;
}

ssize_t fs_read(fd_t fd, void *buf, size_t count) {
    if (fd < 0 || fd >= (fd_t)fd_count)
        return -1;

    file_descriptor_t *file = &fd_table[fd];
    if (!file->inode)
        return -1;

    uint64_t available = 0;
    if (file->position < file->inode->size)
        available = file->inode->size - file->position;

    size_t to_read = count;
    if ((uint64_t)to_read > available)
        to_read = (size_t)available;

    if (file->inode->data && to_read > 0) {
        memcpy(buf, (uint8_t *)file->inode->data + file->position, to_read);
    } else {
        memset(buf, 0, to_read);
    }

    file->position += to_read;
    return (ssize_t)to_read;
}

ssize_t fs_write(fd_t fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= (fd_t)fd_count)
        return -1;

    file_descriptor_t *file = &fd_table[fd];
    if (!file->inode)
        return -1;

    uint64_t end_pos = file->position + count;
    if (end_pos > file->inode->size) {
        void *new_data = kmalloc(end_pos);
        if (!new_data)
            return -1;
        if (file->inode->data && file->inode->size > 0) {
            memcpy(new_data, file->inode->data, file->inode->size);
            kfree(file->inode->data);
        }
        file->inode->data = new_data;
        file->inode->size = end_pos;
        file->inode->block_count = (end_pos + superblock.block_size - 1) / superblock.block_size;
    }

    memcpy((uint8_t *)file->inode->data + file->position, buf, count);
    file->position = end_pos;
    if (file->position > file->inode->size)
        file->inode->size = file->position;

    return (ssize_t)count;
}

int fs_seek(fd_t fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= (fd_t)fd_count)
        return -1;

    file_descriptor_t *file = &fd_table[fd];
    if (!file->inode)
        return -1;

    int64_t new_pos = 0;
    switch (whence) {
        case 0:
            new_pos = offset;
            break;
        case 1:
            new_pos = (int64_t)file->position + offset;
            break;
        case 2:
            new_pos = (int64_t)file->inode->size + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0)
        return -1;

    file->position = (uint64_t)new_pos;
    return 0;
}

int fs_stat(const char *path, inode_t *stat) {
    inode_t *inode = fs_resolve_path(path);
    if (!inode)
        return -1;
    if (stat)
        *stat = *inode;
    return 0;
}

int fs_mkdir(const char *path, int mode) {
    if (!path)
        return -1;

    if (fs_resolve_path(path))
        return 0;

    inode_t *dir = fs_create_inode(mode | FILE_TYPE_DIR);
    if (!dir)
        return -1;
    if (fs_register_path(path, dir, 1) != 0) {
        kfree(dir);
        return -1;
    }
    return 0;
}

int fs_rmdir(const char *path) {
    inode_t *inode = fs_resolve_path(path);
    if (!inode)
        return -1;
    if (inode->data)
        kfree(inode->data);
    kfree(inode);
    return 0;
}

int fs_readdir(fd_t fd, dir_entry_t *entry) {
    (void)fd;
    (void)entry;
    return -1;
}

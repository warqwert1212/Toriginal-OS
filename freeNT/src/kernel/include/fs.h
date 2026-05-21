#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include "types.h"

/* Inode structure */
typedef struct inode {
    uint64_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t accessed_time;
    uint64_t modified_time;
    uint64_t created_time;
    uint32_t block_count;
    uint32_t link_count;
    
    /* Direct blocks and indirect blocks pointers */
    uint64_t blocks[12];  /* Direct blocks */
    uint64_t indirect_block;     /* Single indirect */
    uint64_t double_indirect;    /* Double indirect */
    uint64_t triple_indirect;    /* Triple indirect */
} inode_t;

/* Directory entry */
typedef struct {
    uint64_t ino;
    uint32_t name_len;
    uint8_t type;
    char name[256];
} dir_entry_t;

/* File types */
#define FILE_TYPE_REGULAR 1
#define FILE_TYPE_DIR 2
#define FILE_TYPE_SYMLINK 3
#define FILE_TYPE_DEVICE 4

/* File permissions */
#define FILE_PERM_OWNER_R  0400
#define FILE_PERM_OWNER_W  0200
#define FILE_PERM_OWNER_X  0100
#define FILE_PERM_GROUP_R  0040
#define FILE_PERM_GROUP_W  0020
#define FILE_PERM_GROUP_X  0010
#define FILE_PERM_OTHER_R  0004
#define FILE_PERM_OTHER_W  0002
#define FILE_PERM_OTHER_X  0001

/* Filesystem superblock */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint32_t block_size;
    uint64_t creation_time;
    uint32_t mount_count;
} superblock_t;

/* File operations */
typedef struct {
    int (*open)(const char *path, int flags, int mode);
    ssize_t (*read)(fd_t fd, void *buf, size_t count);
    ssize_t (*write)(fd_t fd, const void *buf, size_t count);
    int (*close)(fd_t fd);
    int (*seek)(fd_t fd, int64_t offset, int whence);
} file_ops_t;

/* Initialize filesystem */
void fs_init(void);

/* File operations */
fd_t fs_open(const char *path, int flags, int mode);
ssize_t fs_read(fd_t fd, void *buf, size_t count);
ssize_t fs_write(fd_t fd, const void *buf, size_t count);
int fs_close(fd_t fd);
int fs_seek(fd_t fd, int64_t offset, int whence);

/* Directory operations */
int fs_mkdir(const char *path, int mode);
int fs_rmdir(const char *path);
int fs_readdir(fd_t fd, dir_entry_t *entry);

/* File status */
int fs_stat(const char *path, inode_t *stat);

/* Path resolution */
inode_t* fs_resolve_path(const char *path);

#endif /* _KERNEL_FS_H */

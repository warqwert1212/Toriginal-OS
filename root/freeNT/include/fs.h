#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include "types.h"

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
    uint32_t link_count;    void *data;
    uint64_t blocks[12];
    uint64_t indirect_block;
    uint64_t double_indirect;
    uint64_t triple_indirect;
} inode_t;

typedef struct {
    uint64_t ino;
    uint32_t name_len;
    uint8_t type;
    char name[256];
} dir_entry_t;

#define FILE_TYPE_REGULAR 1
#define FILE_TYPE_DIR 2

#define FILE_PERM_OWNER_R  0400
#define FILE_PERM_OWNER_W  0200
#define FILE_PERM_OWNER_X  0100

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x100
#define O_TRUNC  0x200

typedef int fd_t;

void fs_init(void);
fd_t fs_open(const char *path, int flags, int mode);
ssize_t fs_read(fd_t fd, void *buf, size_t count);
ssize_t fs_write(fd_t fd, const void *buf, size_t count);
int fs_close(fd_t fd);
int fs_seek(fd_t fd, int64_t offset, int whence);
int fs_mkdir(const char *path, int mode);
int fs_stat(const char *path, inode_t *stat);


#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#endif

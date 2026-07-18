#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>
#include "types.h"

typedef struct {
    uint64_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t accessed_time;
    uint64_t modified_time;
    uint64_t created_time;
    uint32_t link_count;
    uint64_t blocks[12];
    uint64_t indirect_block;
    uint64_t double_indirect;
    uint64_t triple_indirect;
    uint32_t block_count;
    void    *data;
} inode_t;

/* File type — lives in bits [23:16] of inode_t.mode.
 * ALWAYS use FS_INODE_TYPE() to extract — never test .mode directly. */
#define FILE_TYPE_REGULAR  1u
#define FILE_TYPE_DIR      2u
#define FILE_TYPE_SYMLINK  3u

#define FS_INODE_TYPE(mode)  (((uint32_t)(mode) >> 16) & 0xFFu)
#define FS_IS_DIR(mode)      (FS_INODE_TYPE(mode) == FILE_TYPE_DIR)
#define FS_IS_FILE(mode)     (FS_INODE_TYPE(mode) == FILE_TYPE_REGULAR)

/* Open flags */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x100
#define O_TRUNC   0x200

/* Permission bits */
#define FILE_PERM_OWNER_R  0x100
#define FILE_PERM_OWNER_W  0x080
#define FILE_PERM_OWNER_X  0x040
#define FILE_PERM_GROUP_R  0x020
#define FILE_PERM_GROUP_W  0x010
#define FILE_PERM_GROUP_X  0x008

/* Seek whence values */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2
typedef int64_t ssize_t;

void    fs_init(void);
fd_t    fs_open(const char *path, int flags, int mode);
ssize_t fs_read(fd_t fd, void *buf, size_t count);
ssize_t fs_write(fd_t fd, const void *buf, size_t count);
int     fs_close(fd_t fd);
int     fs_seek(fd_t fd, int64_t offset, int whence);
int     fs_unlink(const char *path);
int     fs_mkdir(const char *path, int mode);
int     fs_stat(const char *path, inode_t *stat);
int     fs_readdir(const char *path,
                   int (*cb)(const char *name, uint8_t name_len,
                             uint8_t type, void *ctx),
                   void *ctx);

/* Added for SYS_POLL (syscall.c) - see the implementation comment in
 * trpfs.c for exactly what "available" means for a plain file fd. */
int fs_data_available(fd_t fd);

#endif /* FS_H */

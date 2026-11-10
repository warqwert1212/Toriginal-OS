#ifndef TRPLIBC_H
#define TRPLIBC_H

#include <stdarg.h>
#include "app_abi.h"

typedef unsigned long      size_t;
typedef long               ssize_t;
typedef unsigned long long u64;
typedef long long          i64;
typedef unsigned int       u32;
typedef unsigned short     u16;
typedef unsigned char      u8;

#define NULL ((void *)0)

#define SYS_EXIT_N   0
#define SYS_OPEN_N   4
#define SYS_CLOSE_N  5
#define SYS_READ_N   6
#define SYS_WRITE_N  7
#define SYS_SEEK_N   8
#define SYS_MKDIR_N  10
#define SYS_RMDIR_N  11
#define SYS_UNLINK_N 12
#define SYS_BRK_N    19
#define SYS_YIELD_N  22

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x100
#define O_TRUNC  0x200

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

long syscall6(long n, long a, long b, long c, long d, long e, long f);

void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int v, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
size_t strlcpy(char *d, const char *s, size_t n);
size_t strlcat(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
char  *strdup(const char *s);

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int isxdigit(int c);
int toupper(int c);
int tolower(int c);

void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void  free(void *p);

int   atoi(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int   abs(int v);
void  qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));

int snprintf(char *buf, size_t max, const char *fmt, ...);
int vsnprintf(char *buf, size_t max, const char *fmt, va_list ap);

int  open(const char *path, int flags);
long read(int fd, void *buf, size_t n);
long write(int fd, const void *buf, size_t n);
int  close(int fd);
long lseek(int fd, long off, int whence);
int  unlink(const char *path);
int  mkdir(const char *path);
int  rmdir(const char *path);

int  fstat_path(const char *path, app_stat_t *st);
int  listdir(const char *path, char *buf, size_t max);
int  path_exists(const char *path);
int  path_is_dir(const char *path);
long file_read_all(const char *path, char *buf, size_t max);
int  file_write_all(const char *path, const void *buf, size_t n, int append);
int  path_resolve(const char *cwd, const char *in, char *out, size_t max);
int  glob_match(const char *pat, const char *s);

int  cfg_get(const char *key, char *out, size_t max);
int  cfg_set(const char *key, const char *val);
u64  uptime_ms(void);
int  sysinfo(app_sysinfo_t *si);
void app_yield(void);
long shell_run(char *cwd256, const char *cmd, char *out, size_t max);
int  app_spawn(const char *path);
void exit(int code) __attribute__((noreturn));

int main(void);

#endif

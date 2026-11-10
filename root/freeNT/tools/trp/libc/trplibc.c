#include "trplibc.h"

long syscall6(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile("syscall"
                     : "=a"(r), "+D"(a), "+S"(b), "+d"(c), "+r"(r10), "+r"(r8), "+r"(r9)
                     : "0"(n)
                     : "rcx", "r11", "memory");
    return r;
}

void *memcpy(void *d, const void *s, size_t n) {
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}

void *memmove(void *d, const void *s, size_t n) {
    u8 *p = d;
    const u8 *q = s;
    if (p == q || !n) return d;
    if (p < q) { while (n--) *p++ = *q++; }
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}

void *memset(void *d, int v, size_t n) {
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(v) : "memory");
    return r;
}

int memcmp(const void *a, const void *b, size_t n) {
    const u8 *p = a, *q = b;
    for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return p[i] - q[i];
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const u8 *p = s;
    for (size_t i = 0; i < n; i++) if (p[i] == (u8)c) return (void *)(p + i);
    return NULL;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower(*a) == tolower(*b)) { a++; b++; }
    return tolower((u8)*a) - tolower((u8)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && tolower(*a) == tolower(*b)) { a++; b++; n--; }
    return n ? tolower((u8)*a) - tolower((u8)*b) : 0;
}

char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

size_t strlcpy(char *d, const char *s, size_t n) {
    size_t l = strlen(s);
    if (n) {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}

size_t strlcat(char *d, const char *s, size_t n) {
    size_t dl = strlen(d);
    if (dl >= n) return n + strlen(s);
    return dl + strlcpy(d + dl, s, n - dl);
}

char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c) {
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

char *strstr(const char *h, const char *n) {
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
    return NULL;
}

char *strdup(const char *s) {
    size_t l = strlen(s) + 1;
    char *p = malloc(l);
    if (p) memcpy(p, s, l);
    return p;
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isprint(int c) { return c >= 32 && c < 127; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

typedef struct blk {
    size_t size;
    struct blk *next;
    int free;
} blk_t;

#define BLK_HDR ((sizeof(blk_t) + 15) & ~(size_t)15)

static blk_t *g_head;
static blk_t *g_tail;

static blk_t *grow(size_t need) {
    long cur = syscall6(SYS_BRK_N, 0, 0, 0, 0, 0, 0);
    if (cur <= 0) return NULL;
    size_t total = (BLK_HDR + need + 4095) & ~(size_t)4095;
    long got = syscall6(SYS_BRK_N, cur + (long)total, 0, 0, 0, 0, 0);
    if (got != cur + (long)total) return NULL;
    blk_t *b = (blk_t *)cur;
    b->size = total - BLK_HDR;
    b->next = NULL;
    b->free = 1;
    if (g_tail) g_tail->next = b; else g_head = b;
    g_tail = b;
    return b;
}

void *malloc(size_t n) {
    if (!n) n = 1;
    n = (n + 15) & ~(size_t)15;
    for (;;) {
        for (blk_t *b = g_head; b; b = b->next) {
            if (!b->free || b->size < n) continue;
            if (b->size >= n + BLK_HDR + 32) {
                blk_t *r = (blk_t *)((u8 *)b + BLK_HDR + n);
                r->size = b->size - n - BLK_HDR;
                r->free = 1;
                r->next = b->next;
                b->next = r;
                b->size = n;
                if (g_tail == b) g_tail = r;
            }
            b->free = 0;
            return (u8 *)b + BLK_HDR;
        }
        if (!grow(n)) return NULL;
    }
}

void free(void *p) {
    if (!p) return;
    blk_t *b = (blk_t *)((u8 *)p - BLK_HDR);
    b->free = 1;
    for (blk_t *c = g_head; c; c = c->next) {
        while (c->free && c->next && c->next->free && (u8 *)c + BLK_HDR + c->size == (u8 *)c->next) {
            if (g_tail == c->next) g_tail = c;
            c->size += BLK_HDR + c->next->size;
            c->next = c->next->next;
        }
    }
}

void *calloc(size_t n, size_t m) {
    void *p = malloc(n * m);
    if (p) memset(p, 0, n * m);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    blk_t *b = (blk_t *)((u8 *)p - BLK_HDR);
    if (b->size >= n) return p;
    void *q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    free(p);
    return q;
}

unsigned long strtoul(const char *s, char **end, int base) {
    while (isspace(*s)) s++;
    if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    unsigned long v = 0;
    for (;; s++) {
        int d;
        if (isdigit(*s)) d = *s - '0';
        else if (isalpha(*s)) d = tolower(*s) - 'a' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (end) *end = (char *)s;
    return v;
}

long strtol(const char *s, char **end, int base) {
    while (isspace(*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    long v = (long)strtoul(s, end, base);
    return neg ? -v : v;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
int abs(int v) { return v < 0 ? -v : v; }

static void swap_bytes(u8 *a, u8 *b, size_t n) {
    while (n--) { u8 t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    u8 *a = base;
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && cmp(a + (j - 1) * sz, a + j * sz) > 0; j--)
            swap_bytes(a + (j - 1) * sz, a + j * sz, sz);
}

typedef struct { char *buf; size_t max; size_t len; } out_t;

static void out_c(out_t *o, char c) {
    if (o->len + 1 < o->max) o->buf[o->len] = c;
    o->len++;
}

static void out_num(out_t *o, u64 v, int base, int upper, int neg, int width, int left, int zero) {
    char tmp[24];
    int n = 0;
    const char *dg = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (!v) tmp[n++] = '0';
    while (v) { tmp[n++] = dg[v % (u64)base]; v /= (u64)base; }
    int total = n + (neg ? 1 : 0);
    if (!left && !zero) while (total++ < width) out_c(o, ' ');
    if (neg) out_c(o, '-');
    if (!left && zero) while (total++ < width) out_c(o, '0');
    while (n) out_c(o, tmp[--n]);
    if (left) while (total++ < width) out_c(o, ' ');
}

int vsnprintf(char *buf, size_t max, const char *fmt, va_list ap) {
    out_t o = { buf, max, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out_c(&o, *fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++; prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (isdigit(*fmt)) prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') { if (*fmt != 'h') lng++; fmt++; }
        char c = *fmt;
        if (!c) break;
        if (c == 'd' || c == 'i') {
            i64 v = lng ? va_arg(ap, long) : va_arg(ap, int);
            out_num(&o, v < 0 ? (u64)(-v) : (u64)v, 10, 0, v < 0, width, left, zero);
        } else if (c == 'u' || c == 'x' || c == 'X') {
            u64 v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            out_num(&o, v, c == 'u' ? 10 : 16, c == 'X', 0, width, left, zero);
        } else if (c == 'p') {
            out_c(&o, '0'); out_c(&o, 'x');
            out_num(&o, (u64)va_arg(ap, void *), 16, 0, 0, 0, 0, 0);
        } else if (c == 'c') {
            out_c(&o, (char)va_arg(ap, int));
        } else if (c == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int l = (int)strlen(s);
            if (prec >= 0 && l > prec) l = prec;
            if (!left) for (int i = l; i < width; i++) out_c(&o, ' ');
            for (int i = 0; i < l; i++) out_c(&o, s[i]);
            if (left) for (int i = l; i < width; i++) out_c(&o, ' ');
        } else if (c == '%') {
            out_c(&o, '%');
        } else {
            out_c(&o, '%'); out_c(&o, c);
        }
    }
    if (max) buf[o.len < max ? o.len : max - 1] = 0;
    return (int)o.len;
}

int snprintf(char *buf, size_t max, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, max, fmt, ap);
    va_end(ap);
    return r;
}

int open(const char *path, int flags) { return (int)syscall6(SYS_OPEN_N, (long)path, flags, 0644, 0, 0, 0); }
long read(int fd, void *buf, size_t n) { return syscall6(SYS_READ_N, fd, (long)buf, (long)n, 0, 0, 0); }
long write(int fd, const void *buf, size_t n) { return syscall6(SYS_WRITE_N, fd, (long)buf, (long)n, 0, 0, 0); }
int close(int fd) { return (int)syscall6(SYS_CLOSE_N, fd, 0, 0, 0, 0, 0); }
long lseek(int fd, long off, int whence) { return syscall6(SYS_SEEK_N, fd, off, whence, 0, 0, 0); }
int unlink(const char *path) { return (int)syscall6(SYS_UNLINK_N, (long)path, 0, 0, 0, 0, 0); }
int mkdir(const char *path) { return (int)syscall6(SYS_MKDIR_N, (long)path, 0755, 0, 0, 0, 0); }
int rmdir(const char *path) { return (int)syscall6(SYS_RMDIR_N, (long)path, 0, 0, 0, 0, 0); }

int fstat_path(const char *path, app_stat_t *st) { return (int)syscall6(SYS_APPSVC, SVC_STAT, (long)path, (long)st, 0, 0, 0); }

int listdir(const char *path, char *buf, size_t max) {
    return (int)syscall6(SYS_APPSVC, SVC_READDIR, (long)path, (long)buf, (long)max, 0, 0);
}

int path_exists(const char *path) { app_stat_t st; return fstat_path(path, &st) == 0; }

int path_is_dir(const char *path) {
    app_stat_t st;
    if (!strcmp(path, "/")) return 1;
    return fstat_path(path, &st) == 0 && st.type == DIRENT_DIR;
}

long file_read_all(const char *path, char *buf, size_t max) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < max) {
        long n = read(fd, buf + got, max - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    return (long)got;
}

int file_write_all(const char *path, const void *buf, size_t n, int append) {
    int fd = open(path, O_RDWR | O_CREAT | (append ? 0 : O_TRUNC));
    if (fd < 0) return -1;
    if (append) lseek(fd, 0, SEEK_END);
    size_t put = 0;
    while (put < n) {
        long w = write(fd, (const u8 *)buf + put, n - put);
        if (w <= 0) { close(fd); return -1; }
        put += (size_t)w;
    }
    close(fd);
    return 0;
}

int path_resolve(const char *cwd, const char *in, char *out, size_t max) {
    if (in[0] == 'r' && in[1] == 'o' && in[2] == 'o' && in[3] == 't' && (in[4] == 0 || in[4] == '/')) in = in[4] ? in + 4 : "/";
    char tmp[512];
    if (in[0] == '/') strlcpy(tmp, in, sizeof(tmp));
    else {
        strlcpy(tmp, cwd, sizeof(tmp));
        if (strcmp(cwd, "/")) strlcat(tmp, "/", sizeof(tmp));
        strlcat(tmp, in, sizeof(tmp));
    }
    size_t len = 1;
    char res[512];
    res[0] = '/';
    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        char *e = p;
        while (*e && *e != '/') e++;
        size_t l = (size_t)(e - p);
        if (l == 0) break;
        if (l == 1 && p[0] == '.') {
        } else if (l == 2 && p[0] == '.' && p[1] == '.') {
            if (len > 1) {
                len--;
                while (len > 1 && res[len - 1] != '/') len--;
                if (len > 1) len--;
            }
        } else {
            if (len + l + 1 >= sizeof(res)) return -1;
            if (len > 1) res[len++] = '/';
            memcpy(res + len, p, l);
            len += l;
        }
        p = e;
    }
    res[len] = 0;
    if (len + 1 > max) return -1;
    memcpy(out, res, len + 1);
    return 0;
}

int glob_match(const char *p, const char *s) {
    while (*p) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;
            for (; *s; s++) if (glob_match(p, s)) return 1;
            return glob_match(p, s);
        }
        if (!*s) return 0;
        if (*p == '\\' && p[1]) { p++; if (*p != *s) return 0; p++; s++; continue; }
        if (*p == '?') { p++; s++; continue; }
        if (*p == '[') {
            const char *q = p + 1;
            int neg = 0, ok = 0;
            if (*q == '!' || *q == '^') { neg = 1; q++; }
            while (*q && *q != ']') {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*s >= q[0] && *s <= q[2]) ok = 1;
                    q += 3;
                } else {
                    if (*s == *q) ok = 1;
                    q++;
                }
            }
            if (ok == neg) return 0;
            p = *q ? q + 1 : q;
            s++;
            continue;
        }
        if (*p != *s) return 0;
        p++; s++;
    }
    return !*s;
}

int cfg_get(const char *key, char *out, size_t max) { return (int)syscall6(SYS_APPSVC, SVC_CFG_GET, (long)key, (long)out, (long)max, 0, 0); }
int cfg_set(const char *key, const char *val) { return (int)syscall6(SYS_APPSVC, SVC_CFG_SET, (long)key, (long)val, 0, 0, 0); }
u64 uptime_ms(void) { return (u64)syscall6(SYS_APPSVC, SVC_UPTIME_MS, 0, 0, 0, 0, 0); }
int sysinfo(app_sysinfo_t *si) { return (int)syscall6(SYS_APPSVC, SVC_SYSINFO, (long)si, 0, 0, 0, 0); }
void app_yield(void) { syscall6(SYS_APPCALL, CALL_YIELD, 0, 0, 0, 0, 0); }

long shell_run(char *cwd256, const char *cmd, char *out, size_t max) {
    shell_req_t r;
    r.cwd = cwd256;
    r.cmd = cmd;
    r.out = out;
    r.outmax = max;
    r.len = 0;
    long n = syscall6(SYS_APPCALL, CALL_SHELL, (long)&r, 0, 0, 0, 0);
    if (n < 0) { if (max) out[0] = 0; return -1; }
    if ((size_t)n < max) out[n] = 0;
    return n;
}

int app_spawn(const char *path) { return (int)syscall6(SYS_APPCALL, CALL_SPAWN, (long)path, 0, 0, 0, 0); }

void exit(int code) {
    syscall6(SYS_EXIT_N, code, 0, 0, 0, 0, 0);
    for (;;) app_yield();
}

__attribute__((section(".text.start"), noreturn, used))
void _start(void) {
    __asm__ volatile("andq $-16, %rsp");
    exit(main());
    for (;;) { }
}

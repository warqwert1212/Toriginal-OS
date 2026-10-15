#ifndef TRP_RT_H
#define TRP_RT_H
/* Minimal freestanding runtime for Toriginal OS .trp programs.
 * (The full trplibc lives in a separate repo; the install-time programs
 * only need a handful of syscalls, so they carry this tiny shim instead
 * of depending on it.) */
#include "../../include/install_abi.h"

typedef unsigned long long u64;
typedef unsigned int       u32;
typedef unsigned char      u8;

#define SYS_EXIT_N   0
#define SYS_READ_N   6
#define SYS_WRITE_N  7
#define SYS_YIELD_N  22

static inline long trp_sys(long n, long a, long b, long c) {
    long r;
    register long r10 __asm__("r10") = 0;
    register long r8  __asm__("r8")  = 0;
    register long r9  __asm__("r9")  = 0;
    /* This kernel's syscall return path does not preserve the usual
     * argument registers (rdi/rsi/rdx/r8-r10), unlike Linux - so tell the
     * compiler every one of them may be clobbered. */
    __asm__ volatile("syscall"
                     : "=a"(r), "+D"(a), "+S"(b), "+d"(c), "+r"(r10), "+r"(r8), "+r"(r9)
                     : "0"(n)
                     : "rcx", "r11", "memory");
    return r;
}

/* ── libc-ish bits ──────────────────────────────────────────────────────── */
__attribute__((unused)) static int  t_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
__attribute__((unused)) static int  t_streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
__attribute__((unused)) static void t_memset(void *d, int v, u64 n) { u8 *p = d; while (n--) *p++ = (u8)v; }
__attribute__((unused)) static void t_memcpy(void *d, const void *s, u64 n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; }
__attribute__((unused)) static void t_strcpy(char *d, const char *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
__attribute__((unused)) static char t_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* ── console ────────────────────────────────────────────────────────────── */
__attribute__((unused)) static void putc_(char c) { trp_sys(SYS_WRITE_N, 1, (long)&c, 1); }
__attribute__((unused)) static void puts_(const char *s) { trp_sys(SYS_WRITE_N, 1, (long)s, t_strlen(s)); }
__attribute__((unused)) static void putu(u64 v) {
    char b[24]; int i = 23; b[i] = 0;
    if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10); v /= 10; }
    puts_(b + i);
}
__attribute__((unused)) static void put_pad(const char *s, int w) {
    static const char sp[] = "                                ";
    int l = t_strlen(s); puts_(s);
    if (l < w && w - l < (int)sizeof(sp)) trp_sys(SYS_WRITE_N, 1, (long)sp, w - l);
}
__attribute__((unused)) static void put_upad(u64 v, int w) {
    char b[24]; int i = 23; b[i] = 0; u64 x = v;
    if (!x) b[--i] = '0';
    while (x) { b[--i] = (char)('0' + x % 10); x /= 10; }
    put_pad(b + i, w);
}
__attribute__((unused)) static void clear_(void) { trp_sys(SYS_CONSOLE_CTL, CONSOLE_CTL_CLEAR, 0, 0); }
__attribute__((unused)) static void color_(int fg, int bg) { trp_sys(SYS_CONSOLE_CTL, CONSOLE_CTL_SETCOLOR, fg, bg); }
__attribute__((unused)) static void yield_(void) { trp_sys(SYS_YIELD_N, 0, 0, 0); }

/* Blocking single-key read (PS/2, USB HID or serial - the kernel merges them). */
__attribute__((unused)) static int getch_(void) {
    char c;
    for (;;) {
        long n = trp_sys(SYS_READ_N, 0, (long)&c, 1);
        if (n > 0) return (unsigned char)c;
        yield_();
    }
}

/* Line input with echo (mask != 0 echoes '*'). Returns length. */
__attribute__((unused)) static int readline_(char *buf, int max, int mask) {
    int i = 0;
    for (;;) {
        int c = getch_();
        if (c == '\r' || c == '\n') break;
        if ((c == '\b' || c == 127) && i > 0) { i--; putc_('\b'); putc_(' '); putc_('\b'); continue; }
        if (c < 0x20 || c > 126) continue;
        if (i + 1 < max) { buf[i++] = (char)c; putc_(mask ? '*' : (char)c); }
    }
    buf[i] = 0;
    putc_('\n');
    return i;
}

__attribute__((unused)) static int parse_u64(const char *s, u64 *out) {
    if (!*s) return 0;
    u64 v = 0;
    while (*s) { if (*s < '0' || *s > '9') return 0; v = v * 10 + (u64)(*s - '0'); s++; }
    *out = v; return 1;
}

__attribute__((unused)) static void trp_exit(int code) {
    trp_sys(SYS_EXIT_N, code, 0, 0);
    for (;;) yield_();
}

int trp_main(void);
__attribute__((section(".text.start"), noreturn, used))
void _start(void) {
    __asm__ volatile("andq $-16, %rsp");
    trp_exit(trp_main());
    for (;;) { }
}
#endif

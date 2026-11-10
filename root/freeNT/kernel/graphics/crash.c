#include "crash.h"
#include "graphics_core.h"
#include "font8x16.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include "pit.h"
#include "wm.h"

#define CRASH_QUIP1 "Ohhhh fuck, you messed up bad. How the fuck did you kernel panic?"
#define CRASH_QUIP2 "This is a critical error and the system will now halt. PLEASE BE CAREFUL."

static const char *exception_name(uint64_t n) {
    static const char *names[32] = {
        "Divide error", "Debug", "Non-maskable interrupt", "Breakpoint", "Overflow",
        "Bound range exceeded", "Invalid opcode", "Device not available", "Double fault",
        "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
        "Stack-segment fault", "General protection fault", "Page fault", "Reserved",
        "x87 floating-point error", "Alignment check", "Machine check",
        "SIMD floating-point error", "Virtualization", "Control protection",
        "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor injection", "VMM communication", "Security exception", "Reserved"
    };
    return n < 32 ? names[n] : "Unknown exception";
}

static void put_hex(char *out, uint64_t v) {
    static const char d[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; i++) out[2 + i] = d[(v >> (60 - i * 4)) & 0xF];
    out[18] = 0;
}

static void text(uint32_t x, uint32_t y, const char *s, color_t fg, color_t bg, uint32_t scale) {
    for (; *s; s++) {
        font_draw_glyph(x, y, *s, fg, bg, 1, scale);
        x += FONT8X16_WIDTH * scale;
    }
}

static uint32_t text_w(const char *s, uint32_t scale) {
    return (uint32_t)strlen(s) * FONT8X16_WIDTH * scale;
}

static void reg_line(uint32_t x, uint32_t y, const char *name, uint64_t v, color_t fg, color_t bg) {
    char line[40];
    size_t n = 0;
    while (name[n] && n < 6) { line[n] = name[n]; n++; }
    while (n < 6) line[n++] = ' ';
    put_hex(line + n, v);
    text(x, y, line, fg, bg, 1);
}

void rsod_show(const char *title, const char *reason, const interrupt_frame_t *f) {
    __asm__ volatile("cli");

    serial_puts("\n[RSOD] ");
    serial_puts(title);
    serial_puts(": ");
    serial_puts(reason ? reason : "");
    serial_puts("\n");

    if (graphics_is_available()) {
        uint32_t w = g_framebuffer.width, h = g_framebuffer.height;
        color_t bg = graphics_rgb(170, 20, 24);
        color_t fg = graphics_rgb(255, 255, 255);
        color_t dim = graphics_rgb(255, 205, 205);
        graphics_clear_screen(bg);

        uint32_t face_scale = w >= 1024 ? 10 : 6;
        uint32_t fw = text_w(CRASH_FACE_FATAL, face_scale);
        uint32_t y = h / 12;
        text(w > fw ? (w - fw) / 2 : 0, y, CRASH_FACE_FATAL, fg, bg, face_scale);
        y += FONT8X16_HEIGHT * face_scale + h / 20;

        uint32_t ts = 3;
        uint32_t tw = text_w(title, ts);
        text(w > tw ? (w - tw) / 2 : 0, y, title, fg, bg, ts);
        y += FONT8X16_HEIGHT * ts + 12;

        uint32_t q1 = text_w(CRASH_QUIP1, 2), q2 = text_w(CRASH_QUIP2, 1);
        text(w > q1 ? (w - q1) / 2 : 0, y, CRASH_QUIP1, fg, bg, 2);
        y += FONT8X16_HEIGHT * 2 + 6;
        text(w > q2 ? (w - q2) / 2 : 0, y, CRASH_QUIP2, dim, bg, 1);
        y += FONT8X16_HEIGHT + 20;

        uint32_t left = w / 8;
        if (reason) {
            char row[160];
            size_t n = 0;
            uint32_t max_cols = (w - 2 * left) / (FONT8X16_WIDTH * 2);
            if (max_cols > sizeof(row) - 1) max_cols = sizeof(row) - 1;
            const char *p = reason;
            while (*p) {
                n = 0;
                while (*p && *p != '\n' && n < max_cols) row[n++] = *p++;
                if (*p == '\n') p++;
                row[n] = 0;
                text(left, y, row, fg, bg, 2);
                y += FONT8X16_HEIGHT * 2 + 4;
            }
        }
        y += 12;

        if (f) {
            char tmp[80];
            const char *nm = exception_name(f->interrupt_number);
            size_t k = 0;
            const char *pre = "Exception ";
            while (*pre) tmp[k++] = *pre++;
            if (f->interrupt_number >= 10) tmp[k++] = (char)('0' + (f->interrupt_number / 10) % 10);
            tmp[k++] = (char)('0' + f->interrupt_number % 10);
            tmp[k++] = ':'; tmp[k++] = ' ';
            while (*nm && k < sizeof(tmp) - 1) tmp[k++] = *nm++;
            tmp[k] = 0;
            text(left, y, tmp, dim, bg, 1);
            y += FONT8X16_HEIGHT + 8;

            uint64_t cr2 = 0;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            uint32_t colw = FONT8X16_WIDTH * 28;
            uint32_t rx = left, ry = y;
            struct { const char *n; uint64_t v; } regs[] = {
                {"RIP", f->rip}, {"RSP", f->rsp}, {"RFLAGS", f->rflags}, {"CS", f->cs},
                {"ERR", f->error_code}, {"CR2", cr2}, {"RAX", f->rax}, {"RBX", f->rbx},
                {"RCX", f->rcx}, {"RDX", f->rdx}, {"RSI", f->rsi}, {"RDI", f->rdi},
                {"RBP", f->rbp}, {"R8", f->r8}, {"R9", f->r9}, {"R10", f->r10},
                {"R11", f->r11}, {"R12", f->r12}, {"R13", f->r13}, {"R14", f->r14},
                {"R15", f->r15},
            };
            int per_col = 7;
            for (int i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); i++) {
                reg_line(rx + (uint32_t)(i / per_col) * colw, ry + (uint32_t)(i % per_col) * (FONT8X16_HEIGHT + 2),
                         regs[i].n, regs[i].v, fg, bg);
            }
            y += per_col * (FONT8X16_HEIGHT + 2) + 12;
        }

        const char *foot = "The system has been halted. Restart your machine. Details were sent to the serial port.";
        uint32_t fwid = text_w(foot, 1);
        text(w > fwid ? (w - fwid) / 2 : 0, h - FONT8X16_HEIGHT * 3, foot, dim, bg, 1);
        graphics_present();
    } else {
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_clear();
        vga_write("\n\n          " CRASH_FACE_FATAL "\n\n  ");
        vga_write(title);
        vga_write("\n  " CRASH_QUIP1 "\n  " CRASH_QUIP2 "\n\n  ");
        if (reason) vga_write(reason);
        if (f) {
            vga_write("\n\n  Exception ");
            vga_write_dec(f->interrupt_number);
            vga_write(": ");
            vga_write(exception_name(f->interrupt_number));
        }
        vga_write("\n\n  The system has been halted. Restart your machine.\n");
    }

    for (;;) __asm__ volatile("cli; hlt");
}


#define ERR_QUEUE 4

typedef struct { char what[96]; char detail[128]; } err_item_t;

static err_item_t g_err_q[ERR_QUEUE];
static int g_err_n = 0;

void gui_error_report(const char *what, const char *detail) {
    serial_puts("[GUI ERROR] ");
    serial_puts(what ? what : "");
    serial_puts(" ");
    serial_puts(detail ? detail : "");
    serial_puts("\n");
    static char last[224];
    static uint64_t last_ms = 0;
    char key[224];
    strncpy(key, what ? what : "", 95); key[95] = 0;
    size_t kl = strlen(key);
    strncpy(key + kl, detail ? detail : "", sizeof(key) - kl - 1); key[sizeof(key) - 1] = 0;
    uint64_t now = pit_get_milliseconds();
    if (strcmp(key, last) == 0 && now - last_ms < 3000) return;
    strncpy(last, key, sizeof(last) - 1); last[sizeof(last) - 1] = 0;
    last_ms = now;
    if (g_err_n >= ERR_QUEUE) return;
    err_item_t *e = &g_err_q[g_err_n++];
    strncpy(e->what, what ? what : "", sizeof(e->what) - 1);
    e->what[sizeof(e->what) - 1] = 0;
    strncpy(e->detail, detail ? detail : "", sizeof(e->detail) - 1);
    e->detail[sizeof(e->detail) - 1] = 0;
}

static void px_fill(wm_window_t *w, int x, int y, int rw, int rh, uint32_t c) {
    for (int yy = y; yy < y + rh && yy < w->client_h; yy++) {
        if (yy < 0) continue;
        uint32_t *row = (uint32_t *)((uint8_t *)w->pixels + (size_t)yy * (size_t)w->pitch);
        for (int xx = x; xx < x + rw && xx < w->client_w; xx++) if (xx >= 0) row[xx] = c;
    }
}

static void px_text(wm_window_t *w, int x, int y, const char *s, uint32_t fg, int scale) {
    for (; *s; s++, x += FONT8X16_WIDTH * scale) {
        const uint8_t *rows = font8x16_data[(uint8_t)*s];
        for (int r = 0; r < FONT8X16_HEIGHT; r++) {
            for (int c = 0; c < FONT8X16_WIDTH; c++) {
                if (!((rows[r] >> (7 - c)) & 1)) continue;
                px_fill(w, x + c * scale, y + r * scale, scale, scale, fg);
            }
        }
    }
}

static void open_error_window(const err_item_t *e, uint32_t sw, uint32_t sh) {
    int ww = 480, wh = 210;
    if ((uint32_t)ww > sw) ww = (int)sw;
    int wx = ((int)sw - ww) / 2, wy = ((int)sh - wh) / 2 - 40;
    if (wy < 0) wy = 0;
    uint32_t id = wm_create_window(wx, wy, ww, wh, "Error");
    if (!id) return;
    wm_window_t *w = wm_get_window(id);
    if (!w || !w->pixels) return;

    const uint32_t bg = 0xFFF3F3F3, fg = 0xFF1B1F27, dim = 0xFF6A7280, bad = 0xFFC7362F;
    px_fill(w, 0, 0, w->client_w, w->client_h, bg);
    px_fill(w, 0, 0, 6, w->client_h, bad);
    px_text(w, 24, 22, CRASH_FACE_SOFT, bad, 3);
    px_text(w, 24, 90, e->what, fg, 1);

    int cols = (w->client_w - 48) / FONT8X16_WIDTH;
    if (cols < 8) cols = 8;
    char line[96];
    const char *p = e->detail;
    int y = 118;
    while (*p && y + FONT8X16_HEIGHT < w->client_h) {
        int n = 0;
        while (p[n] && n < cols && n < (int)sizeof(line) - 1) n++;
        for (int i = 0; i < n; i++) line[i] = p[i];
        line[n] = 0;
        px_text(w, 24, y, line, dim, 1);
        p += n;
        y += FONT8X16_HEIGHT + 4;
    }
    wm_raise_window(id);
    wm_set_focus(id);
}

void gui_error_draw(uint32_t sw, uint32_t sh, uint32_t taskbar_h) {
    (void)taskbar_h;
    while (g_err_n > 0) {
        err_item_t e = g_err_q[0];
        for (int i = 1; i < g_err_n; i++) g_err_q[i - 1] = g_err_q[i];
        g_err_n--;
        open_error_window(&e, sw, sh);
    }
}

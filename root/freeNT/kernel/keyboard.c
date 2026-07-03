/* keyboard.c — PS/2 keyboard driver (renamed from keybord.c — typo fix)
 * FIX: keyboard_getc() was accidentally nested inside keyboard_handler()
 *      due to a missing closing brace.  Corrected structure below.
 */

#include <stdint.h>
#include "keyboard.h"

#define KEYBOARD_BUFFER_SIZE 256

static char     keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buffer_head = 0;
static volatile uint32_t buffer_tail = 0;

/* ── modifier state ──────────────────────────────────────────────────────── */
static volatile int mod_shift   = 0;
static volatile int mod_ctrl    = 0;
static volatile int mod_alt     = 0;
static volatile int mod_caps    = 0;
static volatile int mod_num     = 0;
static volatile int pending_e0  = 0;  /* saw 0xE0 prefix, next byte is extended */
static volatile int pending_f0  = 0;  /* saw 0xF0 prefix, next byte is a release */
static int          keyboard_code_set = 1; /* 1 or 2 */
/* Special key codes pushed into the ring buffer (values > 0x7F, won't
 * collide with ASCII). Consumers (shell.c) check for these explicitly. */
#define KEY_LEFT   0x81
#define KEY_RIGHT  0x82
#define KEY_UP     0x83
#define KEY_DOWN   0x84
#define KEY_HOME   0x85
#define KEY_END    0x86
#define KEY_DEL    0x87

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* 8042 controller ports */
#define I8042_DATA   0x60
#define I8042_STATUS 0x64
#define I8042_CMD    0x64

#define STATUS_OUT_FULL 0x01
#define STATUS_IN_FULL  0x02

static void wait_input_empty(void)
{
    int timeout = 100000;
    while ((inb(I8042_STATUS) & STATUS_IN_FULL) && timeout--) {
        /* busy wait */
    }
}

static void wait_output_full(void)
{
    int timeout = 100000;
    while (!(inb(I8042_STATUS) & STATUS_OUT_FULL) && timeout--) {
        /* busy wait */
    }
}

static void send_keyboard_command(uint8_t cmd)
{
    wait_input_empty();
    outb(I8042_DATA, cmd);
}

static uint8_t recv_keyboard_response(void)
{
    wait_output_full();
    return inb(I8042_DATA);
}

static void keyboard_enable_interface(void)
{
    wait_input_empty();
    outb(I8042_CMD, 0xAE);  /* Enable keyboard interface */
}

static void keyboard_disable_interface(void)
{
    wait_input_empty();
    outb(I8042_CMD, 0xAD);  /* Disable keyboard interface */
}

/* Basic US scancode → ASCII map (set 1, unshifted) */
static const char scancode_map[128] = {
    0,    27,
    '1','2','3','4','5','6','7','8','9','0','-','=',
    '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  /* L-Ctrl */
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  /* L-Shift */
    '\\',
    'z','x','c','v','b','n','m',',','.','/',
    0,  /* R-Shift */
    '*',
    0,  /* L-Alt */
    ' ',
};

static const char scancode_map_shift[128] = {
    0,    27,
    '!','@','#','$','%','^','&','*','(',')','_','+',
    '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0,
    '|',
    'Z','X','C','V','B','N','M','<','>','?',
    0, '*', 0, ' ',
};

/* ── ring buffer helpers ──────────────────────────────────────────────────── */

static void keyboard_push(char c)
{
    uint32_t next = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next == buffer_tail) return;   /* buffer full — drop */
    keyboard_buffer[buffer_head] = c;
    buffer_head = next;
}

int keyboard_has_input(void)    { return buffer_head != buffer_tail; }
int keyboard_available(void)    { return buffer_head != buffer_tail; }

char keyboard_getchar(void)
{
    if (buffer_head == buffer_tail) return 0;
    char c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

unsigned char keyboard_read(void) { return (unsigned char)keyboard_getchar(); }

/* ── public blocking / non-blocking read ────────────────────────────────── */

/* FIX: keyboard_getc was nested inside keyboard_handler — now a top-level fn */
char keyboard_getc(void)
{
    while (!keyboard_has_input())
        __asm__ volatile("hlt");
    return keyboard_getchar();
}

char keyboard_getc_nb(void)
{
    return keyboard_has_input() ? keyboard_getchar() : 0;
}

void keyboard_readline(char *buf, int max_len)
{
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_getc();
        if (c == '\n' || c == '\r') break;
        if ((c == '\b' || c == 127) && i > 0) { i--; continue; }
        if (c >= 0x20) buf[i++] = c;
    }
    buf[i] = '\0';
}

/* ── modifier accessors ──────────────────────────────────────────────────── */
int keyboard_shift_held(void) { return mod_shift; }
int keyboard_ctrl_held(void)  { return mod_ctrl;  }
int keyboard_alt_held(void)   { return mod_alt;   }
int keyboard_caps_active(void){ return mod_caps;  }
int keyboard_num_active(void) { return mod_num;   }

/* ── IRQ1 handler (called from keyboard_isr_stub) ───────────────────────── */

void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);

    /* Extended scancode prefix — next byte identifies the actual key */
    if (scancode == 0xE0) {
        pending_e0 = 1;
        outb(0x20, 0x20);
        return;
    }

    if (pending_e0) {
        pending_e0 = 0;
        if (!(scancode & 0x80)) { /* only act on press, ignore release */
            switch (scancode) {
                case 0x4B: keyboard_push(KEY_LEFT);  break;
                case 0x4D: keyboard_push(KEY_RIGHT); break;
                case 0x48: keyboard_push(KEY_UP);    break;
                case 0x50: keyboard_push(KEY_DOWN);  break;
                case 0x47: keyboard_push(KEY_HOME);  break;
                case 0x4F: keyboard_push(KEY_END);   break;
                case 0x53: keyboard_push(KEY_DEL);   break;
                default: break;
            }
        }
        outb(0x20, 0x20);
        return;
    }

    /* Key release — top bit set */
    if (scancode & 0x80) {
        uint8_t rel = scancode & 0x7F;
        if (rel == 0x2A || rel == 0x36) mod_shift = 0;
        if (rel == 0x1D)                mod_ctrl  = 0;
        if (rel == 0x38)                mod_alt   = 0;
        /* Send EOI before returning */
        outb(0x20, 0x20);
        return;
    }

    /* Modifier press */
    if (scancode == 0x2A || scancode == 0x36) { mod_shift = 1; outb(0x20, 0x20); return; }
    if (scancode == 0x1D)                      { mod_ctrl  = 1; outb(0x20, 0x20); return; }
    if (scancode == 0x38)                      { mod_alt   = 1; outb(0x20, 0x20); return; }
    if (scancode == 0x3A)                      { mod_caps ^= 1; outb(0x20, 0x20); return; }
    if (scancode == 0x45)                      { mod_num  ^= 1; outb(0x20, 0x20); return; }

    if (scancode >= 128) { outb(0x20, 0x20); return; }

    int upper = mod_shift ^ mod_caps;
    char c = upper ? scancode_map_shift[scancode] : scancode_map[scancode];
    if (c) keyboard_push(c);

    /* Send EOI to master PIC — MUST happen or PIC blocks all further IRQ1s */
    outb(0x20, 0x20);
}

/* keyboard_irq_handler — called by name from keyboard_isr.s */
void keyboard_irq_handler(void)
{
    keyboard_handler();
}

uint8_t keyboard_get_scancode(void) { return inb(0x60); }

static void keyboard_set_scancode_set(uint8_t set)
{
    send_keyboard_command(0xF0);
    recv_keyboard_response();
    send_keyboard_command(set);
    recv_keyboard_response();
}

static void keyboard_enable_scanning(void)
{
    send_keyboard_command(0xF4);
    recv_keyboard_response();
}

static void keyboard_disable_scanning(void)
{
    send_keyboard_command(0xF5);
    recv_keyboard_response();
}

void keyboard_init(void)
{
    buffer_head = 0;
    buffer_tail = 0;
    mod_shift = mod_ctrl = mod_alt = mod_caps = mod_num = 0;

    /* Initialize the PS/2 keyboard interface and ensure set 1 scancodes. */
    keyboard_disable_interface();
    keyboard_enable_interface();
    keyboard_disable_scanning();
    keyboard_set_scancode_set(1);
    keyboard_enable_scanning();
}

/* =============================================================================
 * keyboard.c — PS/2 keyboard driver (set 1 scancodes)
 *
 * Owns: 8042 controller setup, the IRQ1 handler (scancode -> ASCII / special-
 * key translation), a ring buffer of decoded keys, and modifier-key state
 * (shift/ctrl/alt/caps/num).
 *
 * FIX (this revision): keyboard_init() now (1) reads/modifies/writes the
 * 8042 controller configuration byte to set the IRQ1-enable bit, and
 * (2) unmasks IRQ1 on the master PIC. Without both of these the keyboard
 * scans and ACKs commands fine but never actually raises IRQ1 — total
 * silence, no input. This was the root cause of the "no input registering
 * at all" bug under VirtualBox.
 * ============================================================================= */

#include <stdint.h>
#include "keyboard.h"

/* ── 8042 controller ports ──────────────────────────────────────────────── */
#define I8042_DATA   0x60
#define I8042_STATUS 0x64
#define I8042_CMD    0x64

#define I8042_STATUS_OUT_FULL 0x01
#define I8042_STATUS_IN_FULL  0x02

/* 8042 controller-level commands (sent to I8042_CMD, not the keyboard) */
#define I8042_CMD_READ_CFG   0x20  /* read controller configuration byte  */
#define I8042_CMD_WRITE_CFG  0x60  /* write controller configuration byte */
#define I8042_CFG_IRQ1_EN    0x01  /* config byte bit 0: port-1 IRQ enable */

/* Master PIC command/data ports, EOI code, and IRQ1's mask bit */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC_EOI   0x20

/* ── ring buffer of decoded keys (ASCII or KEY_* special codes) ────────── */
#define KEYBOARD_BUFFER_SIZE 256

static char              keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buffer_head = 0; /* next slot to write */
static volatile uint32_t buffer_tail = 0; /* next slot to read  */

/* ── modifier / prefix state ────────────────────────────────────────────── */
static volatile int mod_shift  = 0;
static volatile int mod_ctrl   = 0;
static volatile int mod_alt    = 0;
static volatile int mod_caps   = 0;
static volatile int mod_num    = 0;
static volatile int pending_e0 = 0; /* saw an 0xE0 extended-scancode prefix */

/* ── port I/O ────────────────────────────────────────────────────────────── */

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

static inline void send_eoi(void)
{
    outb(PIC1_CMD, PIC_EOI);
}

/* ── 8042 controller helpers (used only during init) ────────────────────── */

static void wait_input_empty(void)
{
    int timeout = 100000;
    while ((inb(I8042_STATUS) & I8042_STATUS_IN_FULL) && timeout--) { }
}

static void wait_output_full(void)
{
    int timeout = 100000;
    while (!(inb(I8042_STATUS) & I8042_STATUS_OUT_FULL) && timeout--) { }
}

static void kbc_send_data(uint8_t byte)
{
    wait_input_empty();
    outb(I8042_DATA, byte);
}

static uint8_t kbc_recv_data(void)
{
    wait_output_full();
    return inb(I8042_DATA);
}

static void kbc_send_cmd(uint8_t byte)
{
    wait_input_empty();
    outb(I8042_CMD, byte);
}

/* Send a command to the keyboard itself (not the controller) and consume
 * its single-byte ACK/response. Best-effort — a stuck/absent keyboard
 * must never hang boot, so every wait above is bounded by a timeout. */
static void keyboard_command(uint8_t cmd)
{
    kbc_send_data(cmd);
    kbc_recv_data();
}

/* Unmask IRQ1 on the master PIC (clear bit 1 of the interrupt mask
 * register at 0x21). If a PIC remap routine masked everything at boot,
 * this is required or the CPU never sees the interrupt at all. */
static void pic1_unmask_irq1(void)
{
    uint8_t mask = inb(PIC1_DATA);
    mask &= (uint8_t)~(1 << 1);
    outb(PIC1_DATA, mask);
}

/* ── scancode set 1 -> ASCII tables (US QWERTY) ─────────────────────────── */

static const char scancode_map[128] = {
    0,    27,
    '1','2','3','4','5','6','7','8','9','0','-','=',
    '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   /* L-Ctrl */
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   /* L-Shift */
    '\\',
    'z','x','c','v','b','n','m',',','.','/',
    0,   /* R-Shift */
    '*',
    0,   /* L-Alt */
    ' ',
    /* remaining entries (F-keys, numpad, etc.) default to 0 */
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

/* ── ring buffer ─────────────────────────────────────────────────────────── */

static void keyboard_push(char c)
{
    uint32_t next = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next == buffer_tail) return; /* buffer full — drop the key */
    keyboard_buffer[buffer_head] = c;
    buffer_head = next;
}

static char keyboard_pop(void)
{
    if (buffer_head == buffer_tail) return 0;
    char c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

int keyboard_has_input(void)
{
    return buffer_head != buffer_tail;
}

/* ── public blocking / non-blocking read API ────────────────────────────── */

char keyboard_getc(void)
{
    while (!keyboard_has_input())
        __asm__ volatile("hlt");
    return keyboard_pop();
}

char keyboard_getc_nb(void)
{
    return keyboard_has_input() ? keyboard_pop() : 0;
}

void keyboard_readline(char *buf, int max_len)
{
    if (!buf || max_len <= 0) return;

    int i = 0;
    while (i < max_len - 1)
    {
        char c = keyboard_getc();
        if (c == '\n' || c == '\r') break;
        if ((c == '\b' || c == 127) && i > 0) { i--; continue; }
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x80) buf[i++] = c;
    }
    buf[i] = '\0';
}

/* ── modifier accessors ──────────────────────────────────────────────────── */

int keyboard_shift_held(void)  { return mod_shift; }
int keyboard_ctrl_held(void)   { return mod_ctrl;  }
int keyboard_alt_held(void)    { return mod_alt;   }
int keyboard_caps_active(void) { return mod_caps;  }
int keyboard_num_active(void)  { return mod_num;   }

/* ── IRQ1 handler ────────────────────────────────────────────────────────
 * Called directly from keyboard_isr_stub (kernel/boot/keyboard_isr.s),
 * which has already saved every register the C ABI requires, so this
 * function only needs to read the scancode, decode it, and EOI. */
void keyboard_irq_handler(void)
{
    uint8_t scancode = inb(I8042_DATA);

    /* Extended scancode prefix (arrow keys, Insert/Delete/Home/End, ...):
     * the *next* byte identifies the actual key. */
    if (scancode == 0xE0)
    {
        pending_e0 = 1;
        send_eoi();
        return;
    }

    if (pending_e0)
    {
        pending_e0 = 0;
        if (!(scancode & 0x80)) /* act on press only, ignore key-up */
        {
            switch (scancode)
            {
                case 0x4B: keyboard_push((char)KEY_LEFT);  break;
                case 0x4D: keyboard_push((char)KEY_RIGHT); break;
                case 0x48: keyboard_push((char)KEY_UP);    break;
                case 0x50: keyboard_push((char)KEY_DOWN);  break;
                case 0x47: keyboard_push((char)KEY_HOME);  break;
                case 0x4F: keyboard_push((char)KEY_END);   break;
                case 0x53: keyboard_push((char)KEY_DEL);   break;
                default: break;
            }
        }
        send_eoi();
        return;
    }

    /* Key release — top bit set. Only modifier releases matter to us. */
    if (scancode & 0x80)
    {
        switch (scancode & 0x7F)
        {
            case 0x2A: case 0x36: mod_shift = 0; break;
            case 0x1D:            mod_ctrl  = 0; break;
            case 0x38:            mod_alt   = 0; break;
            default: break;
        }
        send_eoi();
        return;
    }

    /* Modifier press */
    switch (scancode)
    {
        case 0x2A: case 0x36: mod_shift = 1;   send_eoi(); return;
        case 0x1D:            mod_ctrl  = 1;   send_eoi(); return;
        case 0x38:            mod_alt   = 1;   send_eoi(); return;
        case 0x3A:            mod_caps ^= 1;   send_eoi(); return;
        case 0x45:            mod_num  ^= 1;   send_eoi(); return;
        default: break;
    }

    if (scancode < 128)
    {
        int upper = mod_shift ^ mod_caps;
        char c = upper ? scancode_map_shift[scancode] : scancode_map[scancode];

        /* Ctrl+letter -> control code (Ctrl+C = 0x03, etc.), mainly useful
         * for future shell hotkeys. */
        if (mod_ctrl && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
        else if (mod_ctrl && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);

        if (c) keyboard_push(c);
    }

    send_eoi();
}

/* ── 8042 controller / keyboard init ─────────────────────────────────────
 * Call once, after the IDT is loaded and IRQ1's gate is wired, but before
 * interrupts are globally enabled (sti). */
void keyboard_init(void)
{
    buffer_head = 0;
    buffer_tail = 0;
    mod_shift = mod_ctrl = mod_alt = mod_caps = mod_num = 0;
    pending_e0 = 0;

    /* Reset the controller interface. */
    kbc_send_cmd(0xAD); /* disable keyboard interface   */
    kbc_send_cmd(0xAE); /* enable keyboard interface    */

    /* Read the controller configuration byte, force the IRQ1-enable bit
     * on, write it back. Do this BEFORE talking scancode-set commands to
     * the keyboard itself — some virtual 8042 implementations don't like
     * controller-level and keyboard-level commands interleaved. */
    kbc_send_cmd(I8042_CMD_READ_CFG);
    uint8_t cfg = kbc_recv_data();
    cfg |= I8042_CFG_IRQ1_EN;
    kbc_send_cmd(I8042_CMD_WRITE_CFG);
    kbc_send_data(cfg);

    /* Force scancode set 1 so the decode tables above stay valid
     * regardless of what firmware or a previous OS left the keyboard on. */
    keyboard_command(0xF5);         /* disable scanning              */
    kbc_send_data(0xF0);            /* "set scancode set" ...         */
    kbc_recv_data();
    kbc_send_data(0x01);            /* ... = set 1                    */
    kbc_recv_data();
    keyboard_command(0xF4);         /* re-enable scanning             */

    /* Unmask IRQ1 on the PIC last, once the controller/keyboard are
     * fully configured, so we don't eat a spurious interrupt mid-init. */
    pic1_unmask_irq1();
}
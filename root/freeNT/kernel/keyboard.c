/* =============================================================================
 * keyboard.c - PS/2 keyboard driver (scancode set 1, IRQ1)
 * ========================================================================= */

#include <stdint.h>
#include "keyboard.h"
#include "serial.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}

#define I8042_DATA          0x60
#define I8042_STATUS        0x64
#define I8042_CMD           0x64
#define I8042_STATUS_OUT_FULL 0x01
#define I8042_STATUS_IN_FULL  0x02
#define I8042_STATUS_AUX      0x20
#define I8042_CMD_READ_CFG    0x20
#define I8042_CMD_WRITE_CFG   0x60
#define I8042_CFG_IRQ1_EN     0x01
#define PIC1_DATA 0x21

static int wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if (!(inb(I8042_STATUS) & I8042_STATUS_IN_FULL)) return 0;
    }
    return -1;
}

static int wait_output_full(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if (inb(I8042_STATUS) & I8042_STATUS_OUT_FULL) return 0;
    }
    return -1;
}

static void kbc_write_cmd(uint8_t byte) {
    wait_input_empty();
    outb(I8042_CMD, byte);
}

static void kbc_write_data(uint8_t byte) {
    wait_input_empty();
    outb(I8042_DATA, byte);
}

static uint8_t kbc_read_data(void) {
    wait_output_full();
    return inb(I8042_DATA);
}

static uint8_t keyboard_command(uint8_t cmd) {
    kbc_write_data(cmd);
    return kbc_read_data();
}

static void pic1_unmask_irq1(void) {
    uint8_t mask = inb(PIC1_DATA);
    mask &= (uint8_t)~(1u << 1);
    outb(PIC1_DATA, mask);
}

static inline void send_eoi(void) {
    outb(0x20, 0x20);
}

static inline uint8_t scancode_to_keycode(uint8_t scancode) {
    return (uint8_t)(scancode & 0x7F);
}

static const char keycode_map[128] = {
    0,    27,
    '1','2','3','4','5','6','7','8','9','0','-','=',
    '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,
    '\\',
    'z','x','c','v','b','n','m',',','.','/',
    0,
    '*',
    0,
    ' ',
};

static const char keycode_map_shift[128] = {
    0,    27,
    '!','@','#','$','%','^','&','*','(',')','_','+',
    '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0,
    '|',
    'Z','X','C','V','B','N','M','<','>','?',
    0,
    '*',
    0,
    ' ',
};

#define KC_LSHIFT   0x2A
#define KC_RSHIFT   0x36
#define KC_LCTRL    0x1D
#define KC_LALT     0x38
#define KC_CAPSLOCK 0x3A
#define KC_NUMLOCK  0x45

static uint8_t key_down[16];

static inline int key_is_down(uint8_t keycode) {
    return (key_down[keycode >> 3] >> (keycode & 7)) & 1;
}

static inline void key_set_down(uint8_t keycode, int down) {
    uint8_t mask = (uint8_t)(1u << (keycode & 7));
    if (down) key_down[keycode >> 3] |= mask;
    else      key_down[keycode >> 3] &= (uint8_t)~mask;
}

static volatile int mod_shift = 0;
static volatile int mod_ctrl  = 0;
static volatile int mod_alt   = 0;
static volatile int mod_caps  = 0;
static volatile int mod_num   = 0;
static volatile int pending_e0 = 0;

int keyboard_shift_held(void) { return mod_shift; }
int keyboard_ctrl_held(void)  { return mod_ctrl;  }
int keyboard_alt_held(void)   { return mod_alt;   }
int keyboard_caps_active(void){ return mod_caps;  }
int keyboard_num_active(void) { return mod_num;   }

#define KBD_BUF_SIZE 128

static volatile char     kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

static void keyboard_push(char c) {
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) return;
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static char keyboard_pop(void) {
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

int keyboard_has_input(void) {
    return kbd_head != kbd_tail;
}

char keyboard_getc(void) {
    while (!keyboard_has_input()) {
        __asm__ volatile("hlt");
    }
    return keyboard_pop();
}

char keyboard_getc_nb(void) {
    return keyboard_has_input() ? keyboard_pop() : 0;
}

void keyboard_readline(char *buf, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_getc();
        if (c == '\n' || c == '\r') break;
        if (c == '\b') {
            if (i > 0) i--;
            continue;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
}

static void handle_extended_keycode(uint8_t keycode, int down) {
    if (!down) return;
    switch (keycode) {
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

static void keyboard_process_transition(uint8_t keycode, int down)
{
    switch (keycode) {
        case KC_LSHIFT: case KC_RSHIFT: mod_shift = down; return;
        case KC_LCTRL:                  mod_ctrl  = down; return;
        case KC_LALT:                   mod_alt   = down; return;
        case KC_CAPSLOCK: if (down) mod_caps ^= 1; return;
        case KC_NUMLOCK:  if (down) mod_num  ^= 1; return;
        default: break;
    }

    if (!down) return;

    if (mod_ctrl) {
        char base = keycode_map[keycode];
        if (base >= 'a' && base <= 'z') {
            keyboard_push((char)(base - 'a' + 1));
        }
        return;
    }

    int use_shift = mod_shift;
    char lower = keycode_map[keycode];
    if (mod_caps && lower >= 'a' && lower <= 'z') {
        use_shift = !use_shift;
    }
    char c = use_shift ? keycode_map_shift[keycode] : keycode_map[keycode];
    if (c != 0) {
        keyboard_push(c);
    }
}

static void keyboard_process_byte(uint8_t scancode)
{
    if (scancode == 0xE0) {
        pending_e0 = 1;
        return;
    }

    uint8_t keycode = scancode_to_keycode(scancode);
    int     is_break = (scancode & 0x80) != 0;

    if (pending_e0) {
        pending_e0 = 0;
        handle_extended_keycode(keycode, !is_break);
        return;
    }

    if (is_break) {
        if (!key_is_down(keycode)) return;
        key_set_down(keycode, 0);
        keyboard_process_transition(keycode, 0);
    } else {
        key_set_down(keycode, 1);
        keyboard_process_transition(keycode, 1);
    }
}

void keyboard_irq_handler(void)
{
    for (int guard = 0; guard < 16; guard++) {
        uint8_t status = inb(I8042_STATUS);
        if (!(status & I8042_STATUS_OUT_FULL)) break;
        if (status & I8042_STATUS_AUX) break;
        uint8_t scancode = inb(I8042_DATA);
        keyboard_process_byte(scancode);
    }
    send_eoi();
}

void keyboard_init(void)
{
    for (int i = 0; i < 16; i++) key_down[i] = 0;

    kbc_write_cmd(0xAD);
    kbc_write_cmd(0xA7);

    while (inb(I8042_STATUS) & I8042_STATUS_OUT_FULL) {
        (void)inb(I8042_DATA);
    }

    kbc_write_cmd(I8042_CMD_READ_CFG);
    uint8_t cfg = kbc_read_data();
    cfg |= I8042_CFG_IRQ1_EN;
    kbc_write_cmd(I8042_CMD_WRITE_CFG);
    kbc_write_data(cfg);

    kbc_write_cmd(0xAE);

    keyboard_command(0xF5);

    int set1_confirmed = 0;
    for (int attempt = 0; attempt < 5 && !set1_confirmed; attempt++) {
        uint8_t r1 = keyboard_command(0xF0);
        if (r1 != 0xFA) continue;
        uint8_t r2 = keyboard_command(0x01);
        if (r2 == 0xFA) set1_confirmed = 1;
    }

    if (set1_confirmed) {
        serial_puts("[KBD] Scancode set 1 confirmed.\n");
    } else {
        serial_puts("[KBD] WARNING: scancode set 1 not confirmed after retries!\n");
    }

    keyboard_command(0xF4);

    while (inb(I8042_STATUS) & I8042_STATUS_OUT_FULL) {
        (void)inb(I8042_DATA);
    }

    pic1_unmask_irq1();
}
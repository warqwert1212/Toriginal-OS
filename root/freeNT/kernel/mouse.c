#include "mouse.h"
#include "string.h"
#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Force a tiny hardware delay on the motherboard bus to protect raw CPU speeds */
static inline void io_wait(void) {
    outb(0x80, 0);
}

/* ==============================================================================
 * MOUSE.C — PS/2 Mouse Driver Implementation
 * ============================================================================== */

/* ─ 8042 I/O ports ─────────────────────────────────────────────────────── */
#define I8042_DATA   0x60
#define I8042_STATUS 0x64
#define I8042_CMD    0x64

#define STATUS_OUT_FULL  0x01   /* data ready to read */
#define STATUS_IN_FULL   0x02   /* controller still processing input */

/* ─ Driver state ───────────────────────────────────────────────────────── */

typedef struct {
    int32_t x, y;               /* absolute position */
    int32_t dx, dy;             /* accumulated deltas since last read */
    int     left, right, middle; /* button states */
    
    /* Bounds for absolute position clamping */
    int32_t screen_w, screen_h;
    
    /* PS/2 packet assembly */
    uint8_t packet[3];
    int     packet_idx;         /* which byte (0, 1, or 2) we're filling */
} mouse_driver_t;

static mouse_driver_t g_mouse = {0};

/* ─ Utility: wait for controller status ─────────────────────────────────── */

static void wait_input_empty(void) {
    /* Spin until input buffer is empty, using volatile deep checking to prevent optimization */
    for (volatile uint32_t i = 0; i < 500000u; i++) {
        if (!(inb(I8042_STATUS) & STATUS_IN_FULL)) return;
        io_wait();
    }
}

static int wait_output_full(void) {
    /* Spin until output buffer is full. Returns -1 if hardware times out. */
    for (volatile uint32_t i = 0; i < 500000u; i++) {
        if (inb(I8042_STATUS) & STATUS_OUT_FULL) return 0;
        io_wait();
    }
    return -1;
}

/* ─ 8042 communication: send commands to mouse through the controller ───── */

static void send_to_mouse(uint8_t cmd) {
    /* Tell the controller "next byte at port 0x60 is for the mouse" */
    wait_input_empty();
    outb(I8042_CMD, 0xD4);     /* "Send to mouse" command */
    
    wait_input_empty();
    outb(I8042_DATA, cmd);     /* The actual mouse command */
}

static uint8_t recv_from_mouse(void) {
    /* Read the mouse's response safely. Usually 0xFA = ACK. */
    if (wait_output_full() < 0) {
        return 0; /* Fallback baseline to prevent hard locking the boot loop */
    }
    return inb(I8042_DATA);
}

/* ─ Initialization ─────────────────────────────────────────────────────── */

void mouse_init(void) {
    /* Preserve bounds if something (gterm_init(), via mouse_set_bounds())
     * already configured them before this runs - kernel_init() brings up
     * graphics/gterm at step [2/7], well before mouse_wire_init() calls
     * this at step [6/7], so by the time we get here the real framebuffer
     * dimensions are usually already known and correct. Blindly memset-ing
     * the whole struct back to a hardcoded 320x200 "safe default" would
     * silently overwrite that, trapping the cursor in the top-left corner
     * of a 1024x768 screen. Only fall back to 320x200 if bounds genuinely
     * haven't been set yet (both still 0, e.g. at true cold boot before
     * any gterm_init() has run). */
    int32_t prev_w = g_mouse.screen_w;
    int32_t prev_h = g_mouse.screen_h;

    memset(&g_mouse, 0, sizeof(g_mouse));

    if (prev_w > 0 && prev_h > 0) {
        g_mouse.screen_w = prev_w;
        g_mouse.screen_h = prev_h;
    } else {
        g_mouse.screen_w = 320;     /* safe defaults */
        g_mouse.screen_h = 200;
    }
    
    /* 1. Enable the auxiliary mouse port on the 8042 controller */
    wait_input_empty();
    outb(I8042_CMD, 0xA8);     /* "Enable auxiliary port (mouse)" */
    io_wait();
    
    /* 2. Configure the 8042 Controller Byte to route IRQ12 interrupts properly */
    wait_input_empty();
    outb(I8042_CMD, 0x20);     /* Command 0x20: Read Controller Configuration Byte */
    uint8_t status = recv_from_mouse();
    
    status |= 0x02;            /* Bit 1: Enable IRQ12 (Mouse Interrupt) */
    status &= (uint8_t)~0x20;  /* Bit 5: Disable Mouse Clock Inhibit (turn line on) */
    
    wait_input_empty();
    outb(I8042_CMD, 0x60);     /* Command 0x60: Write Controller Configuration Byte */
    wait_input_empty();
    outb(I8042_DATA, status);  /* Push the updated configuration byte */
    io_wait();
    
    /* 3. Send the mouse the "enable data reporting" command */
    send_to_mouse(0xF4);        /* 0xF4 = Enable Data Reporting */
    recv_from_mouse();          /* Wait for ACK (0xFA) */
    io_wait();
    
    /* Flush out remaining byte artifacts so they do not jam the line */
    while (inb(I8042_STATUS) & STATUS_OUT_FULL) {
        (void)inb(I8042_DATA);
        io_wait();
    }
}

/* ─ Bounds ─────────────────────────────────────────────────────────────── */

void mouse_set_bounds(int32_t screen_w, int32_t screen_h) {
    g_mouse.screen_w = screen_w;
    g_mouse.screen_h = screen_h;
}

/* ─ IRQ12 handler — called when mouse has data ──────────────────────────── */

void mouse_irq_handler(void) {
    uint8_t status = inb(I8042_STATUS);
    
    /* Only process if data is ready AND it came from the auxiliary (mouse)
     * port. Bit 5 of status = 0 means keyboard, bit 5 = 1 means mouse. */
    if (!(status & STATUS_OUT_FULL) || !(status & 0x20)) {
        return;  /* Not our data, or no data ready */
    }
    
    uint8_t byte = inb(I8042_DATA);
    
    /* ── Packet assembly ────────────────────────────────────────────────── */
    
    g_mouse.packet[g_mouse.packet_idx] = byte;
    g_mouse.packet_idx++;
    
    if (g_mouse.packet_idx < 3) {
        return;  /* Wait for all 3 bytes */
    }
    
    /* We have a complete packet. Parse it. */
    g_mouse.packet_idx = 0;
    
    uint8_t b0 = g_mouse.packet[0];
    uint8_t b1 = g_mouse.packet[1];
    uint8_t b2 = g_mouse.packet[2];
    
    /* ── Extract buttons ───────────────────────────────────────────────── */
    g_mouse.left   = (b0 & 0x01) ? 1 : 0;
    g_mouse.right  = (b0 & 0x02) ? 1 : 0;
    g_mouse.middle = (b0 & 0x04) ? 1 : 0;
    
    /* ── Extract deltas as signed 9-bit values ────────────────────────── */
    
    /* X: magnitude in b1, sign in bit 4 of b0 */
    int32_t dx = b1;
    if (b0 & 0x10) {            /* negative */
        dx = -(256 - dx);       /* sign-extend 9-bit */
    }
    
    /* Y: magnitude in b2, sign in bit 5 of b0 */
    int32_t dy = b2;
    if (b0 & 0x20) {            /* negative */
        dy = -(256 - dy);       /* sign-extend 9-bit */
    }
    dy = -dy;                   /* invert Y so positive = down on screen */
    
    /* ── Update absolute position ──────────────────────────────────────── */
    g_mouse.x += dx;
    g_mouse.y += dy;
    
    /* Clamp to screen bounds */
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.x >= g_mouse.screen_w) g_mouse.x = g_mouse.screen_w - 1;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.y >= g_mouse.screen_h) g_mouse.y = g_mouse.screen_h - 1;
    
    /* ── Accumulate deltas for polling code ────────────────────────────── */
    g_mouse.dx += dx;
    g_mouse.dy += dy;
}

/* ─ Polled state query ──────────────────────────────────────────────────── */

void mouse_get_state(mouse_state_t *out) {
    if (!out) return;
    
    out->x = g_mouse.x;
    out->y = g_mouse.y;
    out->dx = g_mouse.dx;
    out->dy = g_mouse.dy;
    out->left_button = g_mouse.left;
    out->right_button = g_mouse.right;
    out->middle_button = g_mouse.middle;
    
    /* Clear the delta accumulators after read (so next frame reports fresh deltas) */
    g_mouse.dx = 0;
    g_mouse.dy = 0;
}

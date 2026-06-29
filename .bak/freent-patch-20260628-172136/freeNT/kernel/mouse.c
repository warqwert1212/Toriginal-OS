#include "mouse.h"
#include <stddef.h>
#include <string.h>
#include <port.h>

/* ==============================================================================
 * MOUSE.C — PS/2 Mouse Driver Implementation
 *
 * Hardware protocol (PS/2, 3-byte packets):
 *   Byte 0: [Y-overflow | X-overflow | Y-sign | X-sign | Always 1 | Middle | Right | Left]
 *   Byte 1: X delta magnitude (0-255)
 *   Byte 2: Y delta magnitude (0-255)
 *
 * The sign bits in Byte 0 extend the magnitude bytes to signed 9-bit values.
 * PS/2 Y increases upward; screen Y increases downward, so we negate dy.
 *
 * Controller setup:
 *   - 8042 controller is at ports 0x60 (data) and 0x64 (status/cmd)
 *   - Port 0x64 bit 0: output buffer full (data ready)
 *   - Port 0x64 bit 1: input buffer full (controller busy)
 *   - Mouse packets arrive on IRQ12 (PIC vector 0x2C after remap).
 * ============================================================================== */

#include <stdint.h>

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
    /* Spin until input buffer is empty (controller ready for command) */
    int timeout = 100000;
    while ((inb(I8042_STATUS) & STATUS_IN_FULL) && timeout--) {
        /* busy-wait */
    }
}

static void wait_output_full(void) {
    /* Spin until output buffer is full (data ready to read) */
    int timeout = 100000;
    while (!(inb(I8042_STATUS) & STATUS_OUT_FULL) && timeout--) {
        /* busy-wait */
    }
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
    /* Read the mouse's response (if any). Usually 0xFA = ACK. */
    wait_output_full();
    return inb(I8042_DATA);
}

/* ─ Initialization ─────────────────────────────────────────────────────── */

void mouse_init(void) {
    memset(&g_mouse, 0, sizeof(g_mouse));
    g_mouse.screen_w = 320;     /* safe defaults; caller should call */
    g_mouse.screen_h = 200;     /* mouse_set_bounds() with real values */
    
    /* Enable the mouse port on the 8042 controller */
    wait_input_empty();
    outb(I8042_CMD, 0xA8);     /* "Enable auxiliary port (mouse)" */
    
    /* Send the mouse the "enable data reporting" command */
    send_to_mouse(0xF4);        /* 0xF4 = Enable Data Reporting */
    recv_from_mouse();          /* Wait for ACK (0xFA) */
    
    /* The mouse is now streaming 3-byte packets on IRQ12 */
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
    
    /* Overflow check (bits 6, 7 in b0) — would be here if we cared,
     * but typically we just trust the deltas. */
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
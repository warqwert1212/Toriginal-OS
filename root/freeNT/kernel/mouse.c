#include "mouse.h"
#include "string.h"
#include "ps2.h"
#include <stdint.h>

typedef struct {
    int32_t x, y;
    int32_t dx, dy;
    int     left, right, middle;

    int32_t screen_w, screen_h;

    uint8_t packet[3];
    int     packet_idx;
} mouse_driver_t;

static mouse_driver_t g_mouse = {0};

void mouse_init(void) {

    int32_t prev_w = g_mouse.screen_w;
    int32_t prev_h = g_mouse.screen_h;

    memset(&g_mouse, 0, sizeof(g_mouse));

    if (prev_w > 0 && prev_h > 0) {
        g_mouse.screen_w = prev_w;
        g_mouse.screen_h = prev_h;
    } else {
        g_mouse.screen_w = 320;
        g_mouse.screen_h = 200;
    }

    ps2_enable_port(PS2_PORT_MOUSE);
    ps2_set_port_irq_enabled(PS2_PORT_MOUSE, 1);

    ps2_send_to_device(PS2_PORT_MOUSE, 0xF4);
    ps2_read_data();

    ps2_flush_output();
}

void mouse_set_bounds(int32_t screen_w, int32_t screen_h) {
    g_mouse.screen_w = screen_w;
    g_mouse.screen_h = screen_h;
}

void mouse_irq_handler(void) {
    uint8_t byte;
    if (!ps2_read_data_nb(&byte)) return;
    if (!ps2_status_output_is_mouse()) return;

    g_mouse.packet[g_mouse.packet_idx] = byte;
    g_mouse.packet_idx++;

    if (g_mouse.packet_idx < 3) {
        return;
    }

    g_mouse.packet_idx = 0;

    uint8_t b0 = g_mouse.packet[0];
    uint8_t b1 = g_mouse.packet[1];
    uint8_t b2 = g_mouse.packet[2];

    g_mouse.left   = (b0 & 0x01) ? 1 : 0;
    g_mouse.right  = (b0 & 0x02) ? 1 : 0;
    g_mouse.middle = (b0 & 0x04) ? 1 : 0;

    int32_t dx = b1;
    if (b0 & 0x10) {
        dx = -(256 - dx);
    }

    int32_t dy = b2;
    if (b0 & 0x20) {
        dy = -(256 - dy);
    }
    dy = -dy;

    g_mouse.x += dx;
    g_mouse.y += dy;

    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.x >= g_mouse.screen_w) g_mouse.x = g_mouse.screen_w - 1;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.y >= g_mouse.screen_h) g_mouse.y = g_mouse.screen_h - 1;

    g_mouse.dx += dx;
    g_mouse.dy += dy;
}

void mouse_get_state(mouse_state_t *out) {
    if (!out) return;

    out->x = g_mouse.x;
    out->y = g_mouse.y;
    out->dx = g_mouse.dx;
    out->dy = g_mouse.dy;
    out->left_button = g_mouse.left;
    out->right_button = g_mouse.right;
    out->middle_button = g_mouse.middle;

    g_mouse.dx = 0;
    g_mouse.dy = 0;
}


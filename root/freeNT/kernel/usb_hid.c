#include "usb_hid.h"
#include "uhci.h"
#include "string.h"
#include "serial.h"

#define HID_REQ_SET_PROTOCOL 0x0B
#define HID_BOOT_PROTOCOL 0

#define HID_MOD_LCTRL  0x01
#define HID_MOD_LSHIFT 0x02
#define HID_MOD_LALT   0x04
#define HID_MOD_RCTRL  0x10
#define HID_MOD_RSHIFT 0x20
#define HID_MOD_RALT   0x40

#define HID_BUF_SIZE 128

typedef struct {
    int      in_use;
    int      protocol;
    int      handle;
    uint8_t  last_report[8];
    uint8_t  keys_down[6];
} usb_hid_device_t;

#define USB_HID_MAX_DEVICES 4
static usb_hid_device_t g_hid_devices[USB_HID_MAX_DEVICES];
static int g_hid_device_count = 0;

static volatile char kbd_buf[HID_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

static usb_hid_mouse_state_t g_mouse = {0};

static const char hid_keycode_map[104] = {
    0,0,0,0,
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0',
    '\n', 27, '\b', '\t', ' ',
    '-','=','[',']','\\', 0, ';', '\'', '`', ',', '.', '/',
    0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,
    0,0,0,
    USB_HID_KEY_RIGHT, USB_HID_KEY_LEFT, USB_HID_KEY_DOWN, USB_HID_KEY_UP,
};

static const char hid_keycode_map_shift[104] = {
    0,0,0,0,
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    '\n', 27, '\b', '\t', ' ',
    '_','+','{','}','|', 0, ':', '"', '~', '<', '>', '?',
    0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,
    0,0,0,
    USB_HID_KEY_RIGHT, USB_HID_KEY_LEFT, USB_HID_KEY_DOWN, USB_HID_KEY_UP,
};

static void kbd_push(char c) {
    uint32_t next = (kbd_head + 1) % HID_BUF_SIZE;
    if (next == kbd_tail) return;
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static char kbd_pop(void) {
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % HID_BUF_SIZE;
    return c;
}

int usb_hid_has_input(void) {
    return kbd_head != kbd_tail;
}

char usb_hid_getc_nb(void) {
    return usb_hid_has_input() ? kbd_pop() : 0;
}

static int find_key_in(const uint8_t *report, uint8_t code) {
    for (int i = 2; i < 8; i++) {
        if (report[i] == code) return 1;
    }
    return 0;
}

static void process_keyboard_report(usb_hid_device_t *dev, const uint8_t *report) {
    uint8_t modifier = report[0];
    int shift = (modifier & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) != 0;

    for (int i = 2; i < 8; i++) {
        uint8_t code = report[i];
        if (code == 0) continue;
        if (find_key_in(dev->last_report, code)) continue;
        if (code >= 104) continue;

        char c = shift ? hid_keycode_map_shift[code] : hid_keycode_map[code];
        if (c != 0) {
            kbd_push(c);
        }
    }

    memcpy(dev->last_report, report, 8);
}

static void process_mouse_report(const uint8_t *report) {
    uint8_t buttons = report[0];
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];

    g_mouse.left_button   = (buttons & 0x01) ? 1 : 0;
    g_mouse.right_button  = (buttons & 0x02) ? 1 : 0;
    g_mouse.middle_button = (buttons & 0x04) ? 1 : 0;

    g_mouse.x += dx;
    g_mouse.y += dy;
    g_mouse.dx += dx;
    g_mouse.dy += dy;
}

void usb_hid_mouse_set_bounds(int32_t screen_w, int32_t screen_h) {
    (void)screen_w;
    (void)screen_h;
}

void usb_hid_mouse_get_state(usb_hid_mouse_state_t *out) {
    if (!out) return;
    *out = g_mouse;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
}

void usb_hid_attach(usb_device_t *dev) {
    if (g_hid_device_count >= USB_HID_MAX_DEVICES) return;

    uint8_t setup[8];
    setup[0] = 0x21;
    setup[1] = HID_REQ_SET_PROTOCOL;
    setup[2] = HID_BOOT_PROTOCOL;
    setup[3] = 0;
    setup[4] = dev->interface_class ? 0 : 0;
    setup[5] = 0;
    setup[6] = 0;
    setup[7] = 0;

    uhci_control_transfer(dev->address, dev->max_packet0, dev->low_speed, setup, 0, 0);

    if (dev->int_in_endpoint == 0) {
        serial_puts("[HID] Device has no interrupt IN endpoint - cannot poll.\n");
        return;
    }

    int handle = uhci_setup_interrupt_endpoint(dev->address, dev->int_in_endpoint,
                                                dev->int_in_max_packet, dev->low_speed,
                                                dev->int_in_interval);
    if (handle < 0) {
        serial_puts("[HID] Failed to set up interrupt endpoint.\n");
        return;
    }

    usb_hid_device_t *hid = &g_hid_devices[g_hid_device_count++];
    memset(hid, 0, sizeof(*hid));
    hid->in_use = 1;
    hid->handle = handle;

    if (dev->interface_protocol == USB_HID_PROTOCOL_KEYBOARD) {
        hid->protocol = USB_HID_PROTOCOL_KEYBOARD;
        serial_puts("[HID] Keyboard attached.\n");
    } else if (dev->interface_protocol == USB_HID_PROTOCOL_MOUSE) {
        hid->protocol = USB_HID_PROTOCOL_MOUSE;
        serial_puts("[HID] Mouse attached.\n");
    } else {
        serial_puts("[HID] Unknown boot protocol - treating as keyboard.\n");
        hid->protocol = USB_HID_PROTOCOL_KEYBOARD;
    }
}

void usb_hid_poll(void) {
    uhci_service_interrupt_endpoints();

    for (int i = 0; i < g_hid_device_count; i++) {
        usb_hid_device_t *hid = &g_hid_devices[i];
        if (!hid->in_use) continue;

        uint8_t report[8];
        int n = uhci_poll_interrupt_endpoint(hid->handle, report, sizeof(report));
        if (n <= 0) continue;

        if (hid->protocol == USB_HID_PROTOCOL_KEYBOARD && n >= 8) {
            process_keyboard_report(hid, report);
        } else if (hid->protocol == USB_HID_PROTOCOL_MOUSE && n >= 3) {
            process_mouse_report(report);
        }
    }
}


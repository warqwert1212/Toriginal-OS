#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include "usb.h"

#define USB_HID_PROTOCOL_KEYBOARD 1
#define USB_HID_PROTOCOL_MOUSE    2

#define USB_HID_KEY_LEFT   0x81
#define USB_HID_KEY_RIGHT  0x82
#define USB_HID_KEY_UP     0x83
#define USB_HID_KEY_DOWN   0x84
#define USB_HID_KEY_HOME   0x85
#define USB_HID_KEY_END    0x86
#define USB_HID_KEY_DEL    0x87

typedef struct {
    int32_t x, y;
    int32_t dx, dy;
    int left_button, right_button, middle_button;
} usb_hid_mouse_state_t;

void usb_hid_attach(usb_device_t *dev);
void usb_hid_poll(void);

char usb_hid_getc_nb(void);
int  usb_hid_has_input(void);

void usb_hid_mouse_set_bounds(int32_t screen_w, int32_t screen_h);
void usb_hid_mouse_get_state(usb_hid_mouse_state_t *out);

#endif


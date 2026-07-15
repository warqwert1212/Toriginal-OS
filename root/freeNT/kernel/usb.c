#include "usb.h"
#include "uhci.h"
#include "usb_hid.h"
#include "serial.h"
#include "string.h"

static usb_device_t g_devices[USB_MAX_DEVICES];
static int g_device_count = 0;
static uint8_t g_next_address = 1;

static int get_descriptor(uint8_t addr, uint8_t max_packet, int low_speed,
                           uint8_t type, uint8_t index, void *buf, uint16_t len) {
    uint8_t setup[8];
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = index;
    setup[3] = type;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = (uint8_t)(len & 0xFF);
    setup[7] = (uint8_t)(len >> 8);
    return uhci_control_transfer(addr, max_packet, low_speed, setup, buf, len);
}

static int set_address(uint8_t max_packet, int low_speed, uint8_t new_addr) {
    uint8_t setup[8];
    setup[0] = 0x00;
    setup[1] = USB_REQ_SET_ADDRESS;
    setup[2] = new_addr;
    setup[3] = 0;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 0;
    setup[7] = 0;
    return uhci_control_transfer(0, max_packet, low_speed, setup, 0, 0);
}

static int set_configuration(uint8_t addr, uint8_t max_packet, int low_speed, uint8_t config_value) {
    uint8_t setup[8];
    setup[0] = 0x00;
    setup[1] = USB_REQ_SET_CONFIGURATION;
    setup[2] = config_value;
    setup[3] = 0;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 0;
    setup[7] = 0;
    return uhci_control_transfer(addr, max_packet, low_speed, setup, 0, 0);
}

static void parse_config_and_attach(usb_device_t *dev, uint8_t *buf, uint16_t total_len) {
    uint8_t *ptr = buf;
    uint8_t *end = buf + total_len;
    int found_hid_interface = 0;

    while (ptr + 2 <= end) {
        uint8_t len = ptr[0];
        uint8_t type = ptr[1];
        if (len < 2 || ptr + len > end) break;

        if (type == USB_DESC_INTERFACE && len >= sizeof(usb_interface_descriptor_t)) {
            usb_interface_descriptor_t *iface = (usb_interface_descriptor_t *)ptr;
            if (iface->bInterfaceClass == USB_CLASS_HID) {
                dev->interface_class = iface->bInterfaceClass;
                dev->interface_subclass = iface->bInterfaceSubClass;
                dev->interface_protocol = iface->bInterfaceProtocol;
                found_hid_interface = 1;
            }
        }

        if (found_hid_interface && type == USB_DESC_ENDPOINT && len >= sizeof(usb_endpoint_descriptor_t)) {
            usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t *)ptr;
            if (ep->bEndpointAddress & 0x80) {
                dev->int_in_endpoint = ep->bEndpointAddress & 0x0F;
                dev->int_in_max_packet = (uint8_t)ep->wMaxPacketSize;
                dev->int_in_interval = ep->bInterval;
                found_hid_interface = 0;
            }
        }

        ptr += len;
    }
}

void usb_enumerate_port(int low_speed) {
    if (!uhci_available()) return;
    if (g_device_count >= USB_MAX_DEVICES) return;

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));

    if (get_descriptor(0, 8, low_speed, USB_DESC_DEVICE, 0, buf, 8) != 0) {
        serial_puts("[USB] Failed to fetch initial device descriptor.\n");
        return;
    }

    usb_device_descriptor_t *dd = (usb_device_descriptor_t *)buf;
    uint8_t max_packet0 = dd->bMaxPacketSize0;
    if (max_packet0 == 0 || max_packet0 > 64) max_packet0 = 8;

    uint8_t addr = g_next_address;
    if (set_address(max_packet0, low_speed, addr) != 0) {
        serial_puts("[USB] SET_ADDRESS failed.\n");
        return;
    }
    g_next_address++;

    memset(buf, 0, sizeof(buf));
    if (get_descriptor(addr, max_packet0, low_speed, USB_DESC_DEVICE, 0, buf, sizeof(usb_device_descriptor_t)) != 0) {
        serial_puts("[USB] Failed to fetch full device descriptor after SET_ADDRESS.\n");
        return;
    }
    dd = (usb_device_descriptor_t *)buf;

    usb_device_t *dev = &g_devices[g_device_count];
    memset(dev, 0, sizeof(*dev));
    dev->address = addr;
    dev->max_packet0 = max_packet0;
    dev->low_speed = low_speed;
    dev->vendor_id = dd->idVendor;
    dev->product_id = dd->idProduct;
    dev->device_class = dd->bDeviceClass;

    uint8_t config_buf[9];
    memset(config_buf, 0, sizeof(config_buf));
    if (get_descriptor(addr, max_packet0, low_speed, USB_DESC_CONFIGURATION, 0, config_buf, 9) != 0) {
        serial_puts("[USB] Failed to fetch configuration descriptor header.\n");
        return;
    }
    usb_config_descriptor_t *cd = (usb_config_descriptor_t *)config_buf;
    uint16_t total_len = cd->wTotalLength;
    if (total_len > 256) total_len = 256;

    uint8_t full_config[256];
    memset(full_config, 0, sizeof(full_config));
    if (get_descriptor(addr, max_packet0, low_speed, USB_DESC_CONFIGURATION, 0, full_config, total_len) != 0) {
        serial_puts("[USB] Failed to fetch full configuration descriptor.\n");
        return;
    }

    parse_config_and_attach(dev, full_config, total_len);

    usb_config_descriptor_t *full_cd = (usb_config_descriptor_t *)full_config;
    if (set_configuration(addr, max_packet0, low_speed, full_cd->bConfigurationValue) != 0) {
        serial_puts("[USB] SET_CONFIGURATION failed.\n");
        return;
    }

    dev->valid = 1;
    g_device_count++;

    serial_puts("[USB] Device enumerated and configured.\n");

    if (dev->device_class == USB_CLASS_HID || dev->interface_class == USB_CLASS_HID) {
        usb_hid_attach(dev);
    }
}

void usb_enumerate(void) {
}

int usb_device_count(void) {
    return g_device_count;
}

usb_device_t *usb_device_get(int index) {
    if (index < 0 || index >= g_device_count) return 0;
    return &g_devices[index];
}


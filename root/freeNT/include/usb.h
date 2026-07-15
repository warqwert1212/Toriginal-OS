#ifndef USB_H
#define USB_H

#include <stdint.h>

#define USB_MAX_DEVICES 8

#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_INTERFACE     0x0B

#define USB_DESC_DEVICE        0x01
#define USB_DESC_CONFIGURATION 0x02
#define USB_DESC_STRING        0x03
#define USB_DESC_INTERFACE     0x04
#define USB_DESC_ENDPOINT      0x05
#define USB_DESC_HID           0x21
#define USB_DESC_HID_REPORT    0x22

#define USB_CLASS_HID     0x03
#define USB_CLASS_STORAGE 0x08

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct {
    uint8_t  address;
    uint8_t  max_packet0;
    int      low_speed;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  device_class;
    uint8_t  interface_class;
    uint8_t  interface_subclass;
    uint8_t  interface_protocol;
    uint8_t  int_in_endpoint;
    uint8_t  int_in_max_packet;
    uint8_t  int_in_interval;
    int      valid;
} usb_device_t;

void usb_enumerate(void);
void usb_enumerate_port(int low_speed);
int usb_device_count(void);
usb_device_t *usb_device_get(int index);

#endif


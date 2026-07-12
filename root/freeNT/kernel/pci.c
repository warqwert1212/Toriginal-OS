#include "pci.h"
#include "port.h"
#include "serial.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return 0x80000000u
         | ((uint32_t)bus      << 16)
         | ((uint32_t)device   << 11)
         | ((uint32_t)function << 8)
         | ((uint32_t)offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t v = pci_config_read32(bus, device, function, offset & 0xFC);
    return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t v = pci_config_read32(bus, device, function, offset & 0xFC);
    return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

void pci_enable_device(const pci_device_t *dev)
{
    uint32_t cmd = pci_config_read32(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x7; /* I/O space, memory space, bus master */
    pci_config_write32(dev->bus, dev->device, dev->function, 0x04, cmd);
}

static int scan_function(uint8_t bus, uint8_t device, uint8_t function,
                          pci_device_t *devices, int count, int max_devices)
{
    uint32_t id = pci_config_read32(bus, device, function, 0x00);
    uint16_t vendor_id = (uint16_t)(id & 0xFFFF);
    if (vendor_id == 0xFFFF) return count;
    if (count >= max_devices) return count;

    pci_device_t *d = &devices[count];
    d->bus = bus; d->device = device; d->function = function;
    d->vendor_id = vendor_id;
    d->device_id = (uint16_t)((id >> 16) & 0xFFFF);

    uint32_t class_reg = pci_config_read32(bus, device, function, 0x08);
    d->prog_if    = (uint8_t)((class_reg >> 8)  & 0xFF);
    d->subclass   = (uint8_t)((class_reg >> 16) & 0xFF);
    d->class_code = (uint8_t)((class_reg >> 24) & 0xFF);
    d->header_type = (uint8_t)((pci_config_read32(bus, device, function, 0x0C) >> 16) & 0xFF);

    for (int bar = 0; bar < 6; bar++)
        d->bar[bar] = pci_config_read32(bus, device, function, (uint8_t)(0x10 + bar * 4));

    d->interrupt_line = (uint8_t)(pci_config_read32(bus, device, function, 0x3C) & 0xFF);
    return count + 1;
}

int pci_scan(pci_device_t *devices, int max_devices)
{
    int count = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            uint32_t id0 = pci_config_read32((uint8_t)bus, (uint8_t)device, 0, 0x00);
            if ((id0 & 0xFFFF) == 0xFFFF) continue;

            uint8_t header_type = (uint8_t)((pci_config_read32((uint8_t)bus, (uint8_t)device, 0, 0x0C) >> 16) & 0xFF);
            int multifunction = (header_type & 0x80) != 0;

            count = scan_function((uint8_t)bus, (uint8_t)device, 0, devices, count, max_devices);
            if (multifunction) {
                for (int function = 1; function < 8; function++)
                    count = scan_function((uint8_t)bus, (uint8_t)device, (uint8_t)function, devices, count, max_devices);
            }
        }
    }
    serial_puts("[PCI] scan complete\n");
    return count;
}

int pci_find(const pci_device_t *devices, int count,
             uint16_t vendor_id, uint16_t device_id, pci_device_t *out)
{
    for (int i = 0; i < count; i++) {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id) {
            *out = devices[i];
            return 1;
        }
    }
    return 0;
}

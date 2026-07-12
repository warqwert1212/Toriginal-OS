#ifndef _PCI_H
#define _PCI_H

#include "types.h"

/* pci.h — legacy PCI configuration-space access (0xCF8/0xCFC mechanism).
 * Real, standard x86 mechanism supported since the early '90s, on real
 * hardware and every hypervisor alike — not a VM-specific shortcut. */

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint8_t  interrupt_line;
    uint32_t bar[6];
} pci_device_t;

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

void pci_enable_device(const pci_device_t *dev);
int  pci_scan(pci_device_t *devices, int max_devices);
int  pci_find(const pci_device_t *devices, int count,
              uint16_t vendor_id, uint16_t device_id, pci_device_t *out);

#endif /* _PCI_H */

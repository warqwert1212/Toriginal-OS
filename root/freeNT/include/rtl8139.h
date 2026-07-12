#ifndef _RTL8139_H
#define _RTL8139_H

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

/* Probes the PCI bus for a Realtek RTL8139, brings it up (reset,
 * program MAC, enable RX/TX, wire its IRQ) and registers it with the
 * net core. Returns 1 if found and initialized, 0 otherwise. */
int rtl8139_probe_and_init(void);

#endif /* _RTL8139_H */

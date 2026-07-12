#ifndef _PORT_H
#define _PORT_H

#include "types.h"

/* port.h — x86 port I/O primitives.
 *
 * Every existing driver in this codebase (pit.c, ata.c, mouse.c,
 * keyboard.c) defines its own local static inline inb/outb instead of
 * sharing one header — harmless duplication since each is file-scoped
 * static, but new networking code (pci.c, rtl8139.c) needs 16/32-bit
 * variants too, so it gets one real shared header instead of a fifth
 * copy-pasted version.
 */

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "dN"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "dN"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "dN"(port));
    return v;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "dN"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "dN"(port));
    return v;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "dN"(port));
}

#endif /* _PORT_H */

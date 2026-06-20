#ifndef PIC_H
#define PIC_H

#include <stdint.h>

void pic_remap(void);
void pic_send_eoi(uint8_t irq);   /* FIX: was missing irq param in old pic.h */
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

#endif /* PIC_H */

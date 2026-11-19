#ifndef PIC_H
#define PIC_H

#include "../include/types.h"

#define PIC1          0x20
#define PIC2          0xA0
#define PIC1_COMMAND  PIC1
#define PIC1_DATA     (PIC1+1)
#define PIC2_COMMAND  PIC2
#define PIC2_DATA     (PIC2+1)

void pic_remap();
void pic_send_eoi(uint8_t irq_no);
void pic_unmask_irq(uint8_t irq_no);
void pic_mask_irq(uint8_t irq_no);
bool pic_is_spurious(uint8_t irq_no);   /* IRQ7 / IRQ15 only */

#endif
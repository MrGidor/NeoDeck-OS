#ifndef IRQ_H
#define IRQ_H

#include "../include/types.h"

typedef void (*irq_handler_t)();

/* What irq_common in kernel_entry.asm leaves on the stack (lowest address first).
 * `cs & 3` tells whether the interrupt arrived from ring 3. */
struct irq_frame {
    uint32_t edi, esi, ebp, esp_pusha, ebx, edx, ecx, eax;
    uint32_t irq;
    uint32_t eip, cs, eflags;
};

/* Register a C++ handler for hardware IRQ 0-15 (does not unmask the line). */
void irq_install_handler(uint8_t irq, irq_handler_t handler);

#endif

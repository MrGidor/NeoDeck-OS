#ifndef IRQ_H
#define IRQ_H

#include "../include/types.h"

typedef void (*irq_handler_t)();

/* Register a C++ handler for hardware IRQ 0-15 (does not unmask the line). */
void irq_install_handler(uint8_t irq, irq_handler_t handler);

#endif

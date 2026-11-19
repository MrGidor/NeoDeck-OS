#include "irq.h"
#include "../drivers/io.h"
#include "../drivers/pic.h"

static irq_handler_t irq_handlers[16];

void irq_install_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) irq_handlers[irq] = handler;
}

/* Called from irq_common in kernel_entry.asm with the IRQ number (0-15). */
extern "C" __attribute__((cdecl)) void irq_dispatch(uint32_t irq) {
    if (irq == 7 || irq == 15) {
        if (pic_is_spurious((uint8_t)irq)) {
            /* Spurious: never EOI the line itself, but the master must be
             * told when the spurious one came through the cascade. */
            if (irq == 15) outb(PIC1_COMMAND, 0x20);
            return;
        }
    }

    if (irq < 16 && irq_handlers[irq]) irq_handlers[irq]();

    pic_send_eoi((uint8_t)irq);
}

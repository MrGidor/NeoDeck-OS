typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "pic.h"
#include "io.h"

void pic_remap() {
    uint8_t a1, a2;
    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    (void)a1; (void)a2;
    /* Start with every line masked; drivers unmask their own IRQ. This no
     * longer depends on whatever mask the BIOS happened to leave behind. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask_irq(uint8_t irq_no) {
    uint16_t port = (irq_no < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq_no & 7;
    outb(port, inb(port) & ~(1 << bit));
    if (irq_no >= 8) outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << 2));   /* cascade */
}

void pic_mask_irq(uint8_t irq_no) {
    uint16_t port = (irq_no < 8) ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) | (1 << (irq_no & 7)));
}

bool pic_is_spurious(uint8_t irq_no) {
    uint16_t cmd = (irq_no < 8) ? PIC1_COMMAND : PIC2_COMMAND;
    outb(cmd, 0x0B);                    /* OCW3: read In-Service Register */
    uint8_t isr = inb(cmd);
    return !(isr & (1 << (irq_no & 7)));
}

void pic_send_eoi(uint8_t irq_no) {
    if (irq_no >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}
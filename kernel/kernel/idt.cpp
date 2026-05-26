typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "idt.h"

idt_entry_struct idt[256];
idt_ptr_struct   idt_reg;

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags; 
}

void idt_init() {
    idt_reg.limit = (sizeof(idt_entry_struct) * 256) - 1;
    idt_reg.base  = (uint32_t)&idt;

    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, (uint32_t)default_stub_handler, 0x08, 0x8E);
    }

    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(0x21, (uint32_t)isr21, 0x08, 0x8E);

    idt_set_gate(0x80, (uint32_t)isr80, 0x08, 0xEE);

    load_idt((uint32_t)&idt_reg);
}
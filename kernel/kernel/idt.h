#ifndef IDT_H
#define IDT_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

struct idt_entry_struct {
    uint16_t base_low;           
    uint16_t sel;                
    uint8_t  always0;            
    uint8_t  flags;              
    uint16_t base_high;          
} __attribute__((packed));

struct idt_ptr_struct {
    uint16_t limit;              
    uint32_t base;               
} __attribute__((packed));

extern idt_entry_struct idt[256];
extern idt_ptr_struct   idt_reg;

extern "C" void load_idt(uint32_t idt_ptr_address);
extern "C" void isr0(); 
extern "C" void isr21(); 
extern "C" void isr80();
extern "C" void default_stub_handler();

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void idt_init();

#endif
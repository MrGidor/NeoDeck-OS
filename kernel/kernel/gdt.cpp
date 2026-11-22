#include "../include/types.h"
#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;      /* high nibble: flags, low nibble: limit 19:16 */
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static_assert(sizeof(tss_entry) == 104, "hardware TSS is 104 bytes");

static gdt_entry   gdt[6];
static gdt_pointer gdt_ptr;
static tss_entry   tss;

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[i].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[i].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].access      = access;
    gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[i].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_init() {
    gdt_set(0, 0, 0, 0x00, 0x00);                    /* null                      */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC0);              /* 0x08 kernel code, ring 0  */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);              /* 0x10 kernel data, ring 0  */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC0);              /* 0x18 user code,   ring 3  */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC0);              /* 0x20 user data,   ring 3  */
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);   /* 0x28 32-bit TSS (available) */

    for (unsigned i = 0; i < sizeof(tss); i++) ((volatile uint8_t*)&tss)[i] = 0;
    tss.ss0        = SEL_KERNEL_DATA;
    tss.iomap_base = sizeof(tss);                    /* no I/O bitmap: ring 3 gets no port access */

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    asm volatile(
        "lgdt (%0)\n\t"
        "movw %1, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ss\n\t"
        "ljmp %3, $1f\n"
        "1:\n\t"
        "movw %4, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "r"(&gdt_ptr),
          "i"(SEL_USER_DATA), "i"(SEL_KERNEL_DATA), "i"(SEL_KERNEL_CODE), "i"(SEL_TSS)
        : "eax", "memory");
}

#ifndef GDT_H
#define GDT_H

#include "../include/types.h"

/* Selectors. Every data segment register (DS/ES/FS/GS) is kept at SEL_USER_DATA
 * in ALL contexts, kernel included: a DPL-3 flat data segment is perfectly usable
 * from ring 0, and it means `iret` back to ring 3 never finds a ring-0 selector
 * in a data register (the CPU would null it and the app would fault). */
#define SEL_KERNEL_CODE  0x08
#define SEL_KERNEL_DATA  0x10   /* SS in ring 0 */
#define SEL_USER_CODE    0x1B   /* 0x18 | RPL 3 */
#define SEL_USER_DATA    0x23   /* 0x20 | RPL 3 */
#define SEL_TSS          0x28

/* Replace the boot GDT with: null, kernel code/data, user code/data, TSS; load
 * the task register and reload every segment register. */
void gdt_init();

/* Stack the CPU switches to when an interrupt/syscall arrives from ring 3. */
void tss_set_kernel_stack(uint32_t esp0);

#endif

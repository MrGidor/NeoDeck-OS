#include "../include/types.h"
#include "task_arch.h"
#include "gdt.h"

/* New-task entry stubs. Both end up with every data segment register at the
 * user data selector (see gdt.h for why that is the uniform choice). */
asm(
    ".pushsection .text\n"

    ".global task_user_entry_stub\n"
    "task_user_entry_stub:\n"
    "    call task_after_switch\n"      /* reap dead tasks; keeps ESP unchanged */
    "    movw $0x23, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %fs\n"
    "    movw %ax, %gs\n"
    "    iretl\n"

    ".global task_kthread_entry_stub\n"
    "task_kthread_entry_stub:\n"
    "    call task_after_switch\n"
    "    movw $0x23, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %fs\n"
    "    movw %ax, %gs\n"
    "    call task_kthread_main\n"
    "1:  cli\n"
    "    hlt\n"
    "    jmp 1b\n"

    ".popsection\n"
);

static_assert(SEL_USER_DATA == 0x23, "entry stubs hard-code the user data selector");

extern "C" {

uint32_t arch_irq_save() {
    uint32_t flags;
    asm volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

void arch_irq_restore(uint32_t flags) {
    asm volatile("pushl %0\n\tpopfl" : : "r"(flags) : "memory", "cc");
}

void arch_irq_enable() { asm volatile("sti" : : : "memory"); }

void arch_halt() { asm volatile("sti\n\thlt" : : : "memory"); }

void arch_load_cr3(uint32_t page_directory) {
    asm volatile("movl %0, %%cr3" : : "r"(page_directory) : "memory");
}

void arch_set_kernel_stack(uint32_t esp0) { tss_set_kernel_stack(esp0); }

}

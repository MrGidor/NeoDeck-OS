#ifndef TASK_ARCH_H
#define TASK_ARCH_H

#include "../include/types.h"

/*
 * The only CPU-specific code the scheduler needs. task.cpp calls nothing else
 * that is privileged, so the scheduler logic can be unit-tested on a host by
 * linking test stubs for everything below except task_switch.
 */
extern "C" {

/* Saves ebp/ebx/esi/edi/eflags on the current stack, stores ESP in *old_esp,
 * loads new_esp and restores the same registers from it, then `ret`s.
 * (task_switch.cpp) */
void     task_switch(uint32_t* old_esp, uint32_t new_esp);

/* First code a brand-new task runs (the `ret` target of its fake switch frame).
 *   user:    runs task_after_switch(), loads user data segments, iret -> ring 3
 *   kthread: runs task_after_switch(), then task_kthread_main() */
void     task_user_entry_stub();
void     task_kthread_entry_stub();

uint32_t arch_irq_save();               /* returns EFLAGS, interrupts now disabled */
void     arch_irq_restore(uint32_t flags);
void     arch_irq_enable();
void     arch_halt();                   /* sti; hlt */
void     arch_load_cr3(uint32_t page_directory);
void     arch_set_kernel_stack(uint32_t esp0);   /* TSS.ESP0 for the next ring3->ring0 entry */

/* Provided by task.cpp, called from the stubs */
void     task_after_switch();
void     task_kthread_main();

}

#endif

#include "../include/types.h"
#include "task.h"

/*
 * CPU exception handler / panic screen.
 *
 * Deliberately self-contained: it writes straight to VGA memory and does not
 * touch terminal.cpp, the heap, the filesystem or any other kernel state, so
 * it still works when whatever just crashed was one of those.
 */

/* Layout must match what isr_common in kernel_entry.asm pushes. */
struct exception_frame {
    uint32_t cr2;
    uint32_t edi, esi, ebp, esp_pusha, ebx, edx, ecx, eax;
    uint32_t vector;
    uint32_t error_code;
    uint32_t eip, cs, eflags;
    uint32_t user_esp, user_ss;
};

static const char* const exception_names[32] = {
    "Divide Error (#DE)",             "Debug (#DB)",
    "Non-Maskable Interrupt",         "Breakpoint (#BP)",
    "Overflow (#OF)",                 "Bound Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",           "Device Not Available (#NM)",
    "Double Fault (#DF)",             "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",              "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",      "General Protection Fault (#GP)",
    "Page Fault (#PF)",               "Reserved",
    "x87 Floating-Point (#MF)",       "Alignment Check (#AC)",
    "Machine Check (#MC)",            "SIMD Floating-Point (#XM)",
    "Virtualization (#VE)",           "Control Protection (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection (#HV)",     "VMM Communication (#VC)",
    "Security Exception (#SX)",       "Reserved"
};

#define PANIC_COLS  80
#define PANIC_ROWS  25
#define PANIC_ATTR  0x4F

static volatile uint16_t* const vga = (volatile uint16_t*)0xB8000;
static int p_row, p_col;

static void p_putc(char c) {
    if (c == '\n') { p_col = 0; p_row++; return; }
    if (p_row >= PANIC_ROWS) return;
    vga[p_row * PANIC_COLS + p_col] = (uint16_t)((PANIC_ATTR << 8) | (uint8_t)c);
    if (++p_col >= PANIC_COLS) { p_col = 0; p_row++; }
}

static void p_str(const char* s) { while (*s) p_putc(*s++); }

static void p_hex(uint32_t v) {
    p_str("0x");
    for (int i = 28; i >= 0; i -= 4) p_putc("0123456789ABCDEF"[(v >> i) & 0xF]);
}

static void p_reg(const char* name, uint32_t v) {
    p_str(name); p_hex(v); p_str("  ");
}

extern "C" __attribute__((cdecl)) void exception_handler(exception_frame* f) {
    asm volatile("cli");

    /* A ring-3 task misbehaved: that is the task's problem, not the kernel's.
     * Report it on the normal console and terminate just that task. (We are on
     * the panic stack here; task_exit() abandons it by switching to another
     * task, so the stack is free again for the next exception.) */
    if ((f->cs & 3) == 3 && task_current_is_user())
        task_handle_user_fault(f->vector, f->eip, f->cr2, f->error_code);

    for (int i = 0; i < PANIC_COLS * PANIC_ROWS; i++)
        vga[i] = (uint16_t)((PANIC_ATTR << 8) | ' ');
    p_row = p_col = 0;

    p_str("*** VISTA KERNEL PANIC ***\n\n");

    p_str("Exception ");  p_hex(f->vector);
    p_str(": ");
    p_str(f->vector < 32 ? exception_names[f->vector] : "Unknown");
    p_str("\n");

    p_reg("Error code: ", f->error_code);
    p_str("\n");
    p_reg("EIP: ", f->eip);
    p_reg("CS: ", f->cs);
    p_reg("EFLAGS: ", f->eflags);
    p_str("\n");
    p_reg("CR2: ", f->cr2);
    p_str("\n");

    if (f->vector == 14) {
        p_str("\nPage fault: ");
        p_str((f->error_code & 1) ? "protection violation" : "page not present");
        p_str((f->error_code & 2) ? " on WRITE" : " on READ");
        if (f->error_code & 4) p_str(" (user mode)");
        if (f->error_code & 8) p_str(" (reserved bit set)");
        if (f->error_code & 16) p_str(" (instruction fetch)");
        p_str("\nFaulting address (CR2): "); p_hex(f->cr2); p_str("\n");
    } else if (f->vector == 13 && f->error_code != 0) {
        p_str("\nSelector error: index "); p_hex(f->error_code >> 3);
        p_str((f->error_code & 1) ? " (external event)" : "");
        p_str("\n");
    }

    if (task_scheduler_active()) {
        p_str("\nRunning task: pid "); p_hex(task_current_pid());
        p_str(" ("); p_str(task_current_name()); p_str(")\n");
    }

    p_str("\nRegisters:\n");
    p_reg("EAX=", f->eax); p_reg("EBX=", f->ebx); p_reg("ECX=", f->ecx); p_str("\n");
    p_reg("EDX=", f->edx); p_reg("ESI=", f->esi); p_reg("EDI=", f->edi); p_str("\n");
    p_reg("EBP=", f->ebp);
    /* ESP at the moment of the fault (no privilege change, so the frame
     * ends with EFLAGS and the old ESP is just above it). */
    p_reg("ESP=", (uint32_t)&f->eflags + 4);
    p_str("\n\nSystem halted.");

    for (;;) asm volatile("cli; hlt");
}

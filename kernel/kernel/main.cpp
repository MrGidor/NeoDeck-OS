#include "../include/types.h"
#include "../include/layout.h"

#include "idt.h"
#include "shell.h"
#include "../drivers/terminal.h"
#include "../drivers/io.h"
#include "../drivers/pic.h"
#include "../drivers/ata.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../memory/memory.h"
#include "../fs/fs.h"
#include "gdt.h"
#include "paging.h"
#include "task.h"

extern "C" void enable_paging(uint32_t page_directory_address);

/* .bss (including the heap bitmap) is zeroed by kernel_entry.asm before main. */
extern "C" void main() {
    terminal_clear();

    kprint("Booting NeoDeck...\n");
    kprint_log("Kernel .bss cleared, stack ready.\n");

    gdt_init();
    kprint_log("GDT with user segments and TSS loaded.\n");

    idt_init();
    kprint_log("IDT loaded (CPU exceptions 0-31 -> panic screen).\n");

    pic_remap();
    timer_init();
    keyboard_init();
    kprint_log("PIC remapped; PIT timer (IRQ0) and keyboard (IRQ1) online.\n");

    kprint_log("Initializing identity paging map...\n");
    paging_init();

    kprint_log("Enabling CPU Memory Management Unit (MMU)...\n");
    enable_paging(PAGE_DIRECTORY_ADDRESS);
    kprint_log("Virtual memory architecture operational!\n");

    fs_init();

    task_init();
    task_create_kthread("shell", shell_main);
    kprint_log("Scheduler online (pid 0 = idle, pid 1 = shell).\n");

    /* From here on main() is the idle task: it only runs when nothing else can.
     * The shell and every app are tasks; the timer preempts ring-3 code. */
    asm volatile("sti");
    while (1) {
        task_yield();
        asm volatile("sti; hlt");
    }
}

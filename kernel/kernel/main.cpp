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

extern void paging_init();
extern "C" void enable_paging(uint32_t page_directory_address);

/* .bss (including the heap bitmap) is zeroed by kernel_entry.asm before main. */
extern "C" void main() {
    terminal_clear();

    kprint("Booting NeoDeck...\n");
    kprint_log("Kernel .bss cleared, stack ready.\n");

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

    asm volatile("sti");

    fs_init();   /* needs no interrupts, but keep boot messages in order */
    kprint("System ready. Type 'help' for a list of supported commands.\n");
    kprint("\n/> ");

    /* The IRQ only fills the keyboard ring buffer; the shell runs here. */
    while (1) {
        shell_handle_char(keyboard_get_char());
    }
}

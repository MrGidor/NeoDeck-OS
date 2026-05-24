typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define NULL 0
#define PAGE_DIRECTORY_ADDRESS  0x9C000
#define PAGE_TABLE_0_ADDRESS    0x9D000

#include "idt.h"
#include "../drivers/terminal.h"
#include "../drivers/io.h"
#include "../drivers/pic.h"
#include "../drivers/ata.h"
#include "../memory/memory.h"

extern void paging_init();
extern "C" void enable_paging(uint32_t page_directory_address);

extern "C" void main() {
    terminal_clear();

    for (int i = 0; i < BITMAP_SIZE / 8; i++)
        allocation_bitmap[i] = 0;

    kprint("Booting NeoDeck...\n");
    kprint_info("Memory allocation bitmap safely initialized.\n");

    idt_init();
    pic_remap(); 
    kprint_info("PIC and hardware registers configured successfully.\n");

    kprint_info("Initializing 4MB identity paging map...\n");
    paging_init();

    kprint_info("Enabling CPU Memory Management Unit (MMU)...\n");
    enable_paging(PAGE_DIRECTORY_ADDRESS);
    kprint_info("Virtual memory architecture operational!\n");

    asm volatile("sti");
    kprint_info("System ready. Type 'help' for a list of supported commands.\n");
    kprint("\n/> ");

    while(1) { 
        asm volatile("hlt");
    }
}
#ifndef NEODECK_LAYOUT_H
#define NEODECK_LAYOUT_H

/*
 * NeoDeck physical memory layout
 *
 * build.sh turns every simple "#define NAME <number>" line in this file into
 *   Binaries/layout.inc  (NASM:  NAME equ <number>)
 *   Binaries/layout.ld   (ld:    NAME = <number>;)
 * so the bootloader, kernel entry stub, linker script and C++ code all read
 * the same numbers. Keep every constant that asm/ld needs as a plain number
 * on one line (no expressions) or it will be skipped by the generator.
 *
 *   0x00000000 - 0x000004FF   real-mode IVT / BIOS data area
 *   0x00007C00 - 0x00007DFF   boot sector (dead once the kernel runs)
 *   0x00010000 - 0x0008FFFF   BOOT STAGE: bootloader reads the kernel here
 *                             with BIOS int 13h, then copies it to 1 MB
 *   0x000A0000 - 0x000FFFFF   VGA / ROMs (VGA text at 0xB8000)
 *   0x00100000 - 0x00DFFFFF   KERNEL image: .text .rodata .data .bss + stack
 *   0x00E00000 - 0x00E0CFFF   page directory + page tables
 *   0x01000000 - 0x02FFFFFF   kernel heap (kmalloc), also app images
 */

#define KERNEL_BASE             0x100000
#define KERNEL_STACK_SIZE       0x8000

#define BOOT_STAGE_ADDRESS      0x10000
#define BOOT_STAGE_MAX_BYTES    0x80000

#define PAGE_DIRECTORY_ADDRESS  0xE00000
#define PAGE_TABLE_0_ADDRESS    0xE01000
#define PAGING_TABLE_COUNT      12

#define KERNEL_HEAP_START       0x1000000
#define KERNEL_HEAP_SIZE        0x2000000

#ifdef __cplusplus
/* Compile-time checks so the numbers above cannot drift apart. */
#define PAGING_MAPPED_BYTES     (PAGING_TABLE_COUNT * 0x400000)
static_assert(PAGE_TABLE_0_ADDRESS == PAGE_DIRECTORY_ADDRESS + 0x1000,
              "page tables must follow the page directory");
static_assert(PAGE_TABLE_0_ADDRESS + PAGING_TABLE_COUNT * 0x1000 <= KERNEL_HEAP_START,
              "page tables overlap the heap");
static_assert(KERNEL_HEAP_START + KERNEL_HEAP_SIZE <= PAGING_MAPPED_BYTES,
              "heap extends past the identity-mapped region");
static_assert(KERNEL_BASE + BOOT_STAGE_MAX_BYTES <= PAGE_DIRECTORY_ADDRESS,
              "kernel image budget exceeds space below the page tables");
static_assert(BOOT_STAGE_ADDRESS + BOOT_STAGE_MAX_BYTES <= 0x9F000,
              "boot staging area runs into the EBDA");
#endif

#endif

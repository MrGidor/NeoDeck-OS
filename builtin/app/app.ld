/* Apps are loaded by `exec` at the start of the kernel heap, which is where
 * kmalloc places the first block when the heap is otherwise empty. */
ENTRY(main)
SECTIONS
{
    . = 0x1000000;   /* must equal KERNEL_HEAP_START in kernel/include/layout.h */
    .text   : { *(.text.entry) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}

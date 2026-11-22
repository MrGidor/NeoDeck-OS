/* Every app is linked at the same virtual address. The kernel gives each task
 * its own page directory that maps this window onto the task's private RAM, so
 * two apps can run at once without ever seeing each other's memory.
 * Must equal APP_VIRT_BASE in kernel/include/layout.h (build_app.sh checks). */
ENTRY(main)
SECTIONS
{
    . = 0x40000000;
    .text   : { *(.text.entry) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}

ENTRY(_start)

SECTIONS
{
    /* Our bootloader loads the kernel right at physical memory 1MB / 0x1000 boundary */
    . = 0x1000;

    /* 1. Put the assembly entry block and all executable code here */
    .text : ALIGN(4)
    {
        *(.text)
    }

    /* 2. Put read-only constants, like string literals ("NeoFS Error..."), here */
    .rodata : ALIGN(4)
    {
        *(.rodata)
    }

    /* 3. Put initialized global data variables here */
    .data : ALIGN(4)
    {
        *(.data)
    }

    /* 4. Put uninitialized variables (like your allocation bitmap array) here */
    .bss : ALIGN(4)
    {
        *(COMMON)
        *(.bss)
    }
}
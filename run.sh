#!/bin/bash

export PATH=$PATH:/usr/local/i386elfgcc/bin

mkdir -p Binaries

nasm "bootloader/boot.asm" -f bin -o "Binaries/boot.bin"

nasm "kernel/kernel_entry.asm" -f elf -o "Binaries/kernel_entry.o"

i386-elf-gcc -ffreestanding -m32 -g -c "kernel/kernel/main.cpp" -o "Binaries/main.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/kernel/idt.cpp" -o "Binaries/idt.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/kernel/shell.cpp" -o "Binaries/shell.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/kernel/paging.cpp" -o "Binaries/paging.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/drivers/terminal.cpp" -o "Binaries/terminal.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/drivers/io.cpp" -o "Binaries/io.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/drivers/pic.cpp" -o "Binaries/pic.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/drivers/ata.cpp" -o "Binaries/ata.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/memory/memory.cpp" -o "Binaries/memory.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/fs/neofs.cpp" -o "Binaries/neofs.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/shell/string.cpp" -o "Binaries/string.o"

i386-elf-ld -T linker.ld -o "Binaries/full_kernel.bin" \
    "Binaries/kernel_entry.o" \
    "Binaries/main.o" \
    "Binaries/idt.o" \
    "Binaries/shell.o" \
    "Binaries/paging.o" \
    "Binaries/terminal.o" \
    "Binaries/io.o" \
    "Binaries/pic.o" \
    "Binaries/ata.o" \
    "Binaries/memory.o" \
    "Binaries/neofs.o" \
    "Binaries/string.o" \
    --oformat binary

cat "Binaries/boot.bin" "Binaries/full_kernel.bin" > "Binaries/OS.bin"

truncate -s 1440k "Binaries/OS.bin"

dd if=/dev/zero of="Binaries/hdd.img" bs=1 count=0 seek=10M 2>/dev/null

qemu-system-i386 -fda "Binaries/OS.bin" -drive format=raw,file="Binaries/hdd.img",bus=0,unit=0,media=disk -m 128M
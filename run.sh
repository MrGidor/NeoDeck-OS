#!/bin/bash

# 1. Update the toolchain path
export PATH=$PATH:/usr/local/i386elfgcc/bin

# 2. Ensure the Binaries output directory exists
mkdir -p Binaries

# 3. Assemble the bootloader
nasm "bootloader/boot.asm" -f bin -o "Binaries/boot.bin"

# 4. Compile the kernel components
nasm "kernel/kernel_entry.asm" -f elf -o "Binaries/kernel_entry.o"
i386-elf-gcc -ffreestanding -m32 -g -c "kernel/kernel.cpp" -o "Binaries/kernel.o"

# 5. Link the kernel into a flat binary at 0x1000
i386-elf-ld -o "Binaries/full_kernel.bin" -Ttext 0x1000 "Binaries/kernel_entry.o" "Binaries/kernel.o" --oformat binary

# 6. Stitch the bootloader and kernel together
cat "Binaries/boot.bin" "Binaries/full_kernel.bin" > "Binaries/OS.bin"

# 7. Dynamically pad the boot image to a standard 1.44MB floppy disk size
truncate -s 1440k "Binaries/OS.bin"

# 8. Generate a completely separate 10MB blank hard drive for NeoFS!
# 'seek=10M' allocates it instantly without hogging host disk space until used
dd if=/dev/zero of="Binaries/hdd.img" bs=1 count=0 seek=10M 2>/dev/null

# 9. Run QEMU with the Floppy OS as boot source, AND the 10MB Virtual HDD as Master IDE
qemu-system-i386 -fda "Binaries/OS.bin" -drive format=raw,file="Binaries/hdd.img",bus=0,unit=0,media=disk -m 128M
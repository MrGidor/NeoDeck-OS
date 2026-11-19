#!/bin/bash
# Builds Binaries/OS.bin (floppy image). Any failure aborts the build.
set -euo pipefail
cd "$(dirname "$0")"

export PATH=$PATH:/usr/local/i386elfgcc/bin
CC=${CC:-i386-elf-gcc}
LD=${LD:-i386-elf-ld}
OBJCOPY=${OBJCOPY:-i386-elf-objcopy}
NM=${NM:-i386-elf-nm}
CFLAGS="-ffreestanding -m32 -g -fno-pic -fno-pie -fno-exceptions -fno-rtti \
        -fno-stack-protector -fno-asynchronous-unwind-tables \
        -Ikernel/include -Ibuiltin/sdk/include"

OUT=Binaries
FLOPPY_SECTORS=2880
mkdir -p "$OUT"

# ---Shared layout constants: layout.h -> layout.inc (NASM) + layout.ld (ld) 
LAYOUT=kernel/include/layout.h
sed -n 's/^#define[ \t]\+\([A-Z_0-9]\+\)[ \t]\+\(0x[0-9A-Fa-f]\+\|[0-9]\+\)\([ \t].*\)\?$/\1 equ \2/p' "$LAYOUT" > "$OUT/layout.inc"
sed -n 's/^#define[ \t]\+\([A-Z_0-9]\+\)[ \t]\+\(0x[0-9A-Fa-f]\+\|[0-9]\+\)\([ \t].*\)\?$/\1 = \2;/p' "$LAYOUT" > "$OUT/layout.ld"
layout_val() { awk -v n="$1" '$1=="#define" && $2==n {print $3}' "$LAYOUT"; }
STAGE_MAX=$(( $(layout_val BOOT_STAGE_MAX_BYTES) ))

# --- Kernel entry stub + every kernel .cpp (new files are picked up automatically)
nasm -f elf -I "$OUT/" kernel/kernel_entry.asm -o "$OUT/kernel_entry.o"

OBJS=("$OUT/kernel_entry.o")
while IFS= read -r src; do
    obj="$OUT/$(basename "${src%.cpp}").o"
    echo "  CC  $src"
    "$CC" $CFLAGS -c "$src" -o "$obj"
    OBJS+=("$obj")
done < <(find kernel -name '*.cpp' | sort)

# ---Link to ELF (keeps symbols for gdb), then flatten
"$LD" -m elf_i386 -L "$OUT" -T linker.ld -o "$OUT/kernel.elf" "${OBJS[@]}"
"$OBJCOPY" -O binary "$OUT/kernel.elf" "$OUT/full_kernel.bin"

# --- Size check, and tell the bootloader how many sectors to load
KSIZE=$(stat -c%s "$OUT/full_kernel.bin")
KSECTORS=$(( (KSIZE + 511) / 512 ))
if (( KSIZE > STAGE_MAX )); then
    echo "ERROR: kernel is $KSIZE bytes, bootloader staging area holds only $STAGE_MAX" >&2
    exit 1
fi
if (( KSECTORS + 1 > FLOPPY_SECTORS )); then
    echo "ERROR: kernel needs $KSECTORS sectors, floppy has only $((FLOPPY_SECTORS - 1)) after the boot sector" >&2
    exit 1
fi
BSS_END=$("$NM" "$OUT/kernel.elf" | awk '$3=="__kernel_end"{print "0x"$1}')
echo "  kernel: $KSIZE bytes on disk, $KSECTORS sectors; memory image ends at $BSS_END"

nasm -f bin -I "$OUT/" -DKERNEL_SECTORS="$KSECTORS" bootloader/boot.asm -o "$OUT/boot.bin"
[[ $(stat -c%s "$OUT/boot.bin") -eq 512 ]] || { echo "ERROR: boot.bin is not 512 bytes" >&2; exit 1; }

# --- Floppy image 
cat "$OUT/boot.bin" "$OUT/full_kernel.bin" > "$OUT/OS.bin"
truncate -s 1440k "$OUT/OS.bin"

# Data disk is only created if missing so your NeoFS contents survive rebuilds
[[ -f "$OUT/hdd.img" ]] || truncate -s 10M "$OUT/hdd.img"

echo "Build OK: $OUT/OS.bin"

#!/bin/bash
# Usage: builtin/app/build_app.sh hello ticker burner     -> hello.bin ticker.bin burner.bin
# Then:  python3 builtin/app/app_injector.py builtin/app/*.bin   (copies them onto the FAT32 disk)
set -euo pipefail
cd "$(dirname "$0")"
export PATH=$PATH:/usr/local/i386elfgcc/bin
CC=${CC:-i386-elf-gcc}; LD=${LD:-i386-elf-ld}; OBJCOPY=${OBJCOPY:-i386-elf-objcopy}
[[ $# -ge 1 ]] || { echo "usage: $0 <app name without .cpp> [more apps...]" >&2; exit 1; }

# app.ld links at a literal address; it must match the kernel's app window
BASE=$(grep -E '^#define[[:space:]]+APP_VIRT_BASE' ../../kernel/include/layout.h | awk '{print $3}')
grep -q "\. = $BASE;" app.ld || { echo "app.ld base does not match APP_VIRT_BASE ($BASE) in layout.h" >&2; exit 1; }

FLAGS="-ffreestanding -m32 -fno-pic -fno-pie -fno-exceptions -fno-rtti -fno-stack-protector -fno-asynchronous-unwind-tables -I../sdk/include"
$CC $FLAGS -c ../sdk/src/vista.cpp -o sdk.o
for name in "$@"; do
    $CC $FLAGS -c "$name.cpp" -o "$name.o"
    $LD -m elf_i386 -T app.ld -o "$name.elf" "$name.o" sdk.o
    $OBJCOPY -O binary "$name.elf" "$name.bin"
    rm -f "$name.o" "$name.elf"
    echo "built $name.bin ($(stat -c%s "$name.bin") bytes)"
done
rm -f sdk.o

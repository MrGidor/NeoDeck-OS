#!/bin/bash
# Usage: builtin/app/build_app.sh hello      -> builtin/app/hello.bin
# Then:  python3 builtin/app/app_injector.py builtin/app/hello.bin   (copies it onto the FAT32 disk)
set -euo pipefail
cd "$(dirname "$0")"
export PATH=$PATH:/usr/local/i386elfgcc/bin
CC=${CC:-i386-elf-gcc}; LD=${LD:-i386-elf-ld}; OBJCOPY=${OBJCOPY:-i386-elf-objcopy}
name=${1:?app name (without .cpp)}
HDR=$(grep -E '^#define[[:space:]]+KERNEL_HEAP_START' ../../kernel/include/layout.h | awk '{print $3}')
grep -q "0x1000000" app.ld && [[ "$HDR" == "0x1000000" ]] || { echo "app.ld base does not match KERNEL_HEAP_START ($HDR)" >&2; exit 1; }
$CC -ffreestanding -m32 -fno-pic -fno-pie -fno-exceptions -fno-rtti -fno-stack-protector \
    -fno-asynchronous-unwind-tables -I../sdk/include -c "$name.cpp" -o "$name.o"
$CC -ffreestanding -m32 -fno-pic -fno-pie -fno-exceptions -fno-rtti -fno-stack-protector \
    -fno-asynchronous-unwind-tables -I../sdk/include -c ../sdk/src/neodeck.cpp -o sdk.o
$LD -m elf_i386 -T app.ld -o "$name.elf" "$name.o" sdk.o
$OBJCOPY -O binary "$name.elf" "$name.bin"
rm -f "$name.o" sdk.o "$name.elf"
echo "built $name.bin ($(stat -c%s "$name.bin") bytes)"

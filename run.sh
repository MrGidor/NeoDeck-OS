#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

./build.sh

# QEMU_DEBUG=1 ./run.sh  -> stop on triple fault instead of rebooting and log
#                           every interrupt/exception to Binaries/qemu.log
EXTRA=()
if [[ "${QEMU_DEBUG:-0}" == "1" ]]; then
    EXTRA=(-no-reboot -d int,cpu_reset -D Binaries/qemu.log)
fi

qemu-system-i386 -boot order=a -fda Binaries/OS.bin \
    -drive format=raw,file=Binaries/hdd.img,bus=0,unit=0,media=disk \
    -m 128M -display gtk "${EXTRA[@]}"
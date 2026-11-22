#!/bin/bash
# Multitasking core test: the real scheduler, paging and heap code as a 32-bit Linux program.
# Needs a gcc that can target -m32 (no 32-bit libc required).
set -euo pipefail
cd "$(dirname "$0")/.."
gcc -m32 -O1 -g -ffreestanding -nostdlib -static -fno-pic -fno-pie -fno-stack-protector \
    -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -Wall -Wno-unused-function \
    -Ikernel/include -Ibuiltin/sdk/include \
    tests/sched_host_test.cpp kernel/kernel/task.cpp kernel/kernel/task_switch.cpp \
    kernel/kernel/paging.cpp kernel/memory/memory.cpp \
    -o /tmp/sched_host_test
/tmp/sched_host_test

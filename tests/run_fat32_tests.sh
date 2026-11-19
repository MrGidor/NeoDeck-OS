#!/bin/bash
# Builds the FAT32 driver for the host and runs tests/fat32_host_test.cpp.
# Needs: g++, python3. Run from anywhere.
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++17 -O1 -g -Wall -Wno-unused-label -fno-exceptions \
    -Ikernel/include -Ibuiltin/sdk/include \
    tests/fat32_host_test.cpp kernel/fs/fat32.cpp kernel/fs/fs.cpp kernel/shell/string.cpp \
    -o /tmp/fat32_host_test
/tmp/fat32_host_test

#!/usr/bin/env python3
"""Copy a built app onto the FAT32 data disk (Binaries/hdd.img).

    python3 app_injector.py [hostfile [/dest/name]]      default: hello.bin -> /hello.bin

Thin wrapper around tools/fat32img.py (which also offers ls/cat/mkdir/check).
Do not run it while QEMU has the disk open.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "fat32img.py")
DISK = os.path.join(HERE, "..", "..", "Binaries", "hdd.img")

host = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "hello.bin")
dest = sys.argv[2] if len(sys.argv) > 2 else "/" + os.path.basename(host)

if not os.path.exists(DISK):
    sys.exit("Binaries/hdd.img not found - run ./build.sh first")
sys.exit(subprocess.call([sys.executable, TOOL, "put", DISK, host, dest]))

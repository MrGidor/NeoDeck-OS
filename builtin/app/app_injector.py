#!/usr/bin/env python3
"""Copy built apps onto the FAT32 data disk (Binaries/hdd.img).

    python3 app_injector.py [file.bin ...]        default: every *.bin next to this script
    python3 app_injector.py hello.bin ticker.bin:/bin/ticker.bin     (host file[:dest on disk])

Thin wrapper around tools/fat32img.py (which also offers ls/cat/mkdir/check).
Do not run it while QEMU has the disk open.
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "fat32img.py")
DISK = os.path.join(HERE, "..", "..", "Binaries", "hdd.img")

if not os.path.exists(DISK):
    sys.exit("Binaries/hdd.img not found - run ./build.sh first")

args = sys.argv[1:] or sorted(glob.glob(os.path.join(HERE, "*.bin")))
if not args:
    sys.exit("no .bin files found - run build_app.sh first")

rc = 0
for arg in args:
    host, _, dest = arg.partition(":")
    dest = dest or "/" + os.path.basename(host)
    rc |= subprocess.call([sys.executable, TOOL, "put", DISK, host, dest])
sys.exit(rc)

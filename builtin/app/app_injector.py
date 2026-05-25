import sys
import os

DISK_IMG = "../../Binaries/hdd.img"  
INODE_TABLE_SECTOR = 2
ROOT_DIR_SECTOR = 3

def inject_file(host_filename, os_filename):
    if not os.path.exists(host_filename):
        print(f"Error: Host file '{host_filename}' not found.")
        return

    with open(host_filename, "rb") as f:
        payload = f.read()
    payload_len = len(payload)

    with open(DISK_IMG, "r+b") as disk:
        disk.seek(INODE_TABLE_SECTOR * 512)
        inode_bytes = bytearray(disk.read(512))
        
        disk.seek(ROOT_DIR_SECTOR * 512)
        dir_bytes = bytearray(disk.read(512))

        # Find an empty Inode (skip root inode 0)
        free_inode = -1
        for i in range(1, 16):
            offset = i * 10
            used = inode_bytes[offset + 9]
            if used == 0:
                free_inode = i
                break

        if free_inode == -1:
            print("Error: No free inodes on NeoFS disk!")
            return

        # Find a free directory entry slot
        free_slot = -1
        for i in range(16):
            offset = i * 19
            used = dir_bytes[offset + 18]
            if used == 0:
                free_slot = i
                break

        if free_slot == -1:
            print("Error: Root directory entry slots full!")
            return

        # Assign hardcoded start sector for simplicity (matching your touch logic)
        start_sector = 20 + free_inode

        inode_offset = free_inode * 10
        inode_bytes[inode_offset + 0] = 1 # TYPE_FILE
        
        # Clean sequential indices fixing the inline syntax bug
        size_start = inode_offset + 1
        size_end = inode_offset + 5
        inode_bytes[size_start:size_end] = payload_len.to_bytes(4, 'little')
        
        sector_start = size_end
        sector_end = size_end + 4
        inode_bytes[sector_start:sector_end] = start_sector.to_bytes(4, 'little')
        
        inode_bytes[inode_offset + 9] = 1 # USED

        dir_offset = free_slot * 19
        dir_bytes[dir_offset + 0:dir_offset + 4] = free_inode.to_bytes(4, 'little')
        
        # Clear name field and write new filename string
        for j in range(14):
            dir_bytes[dir_offset + 4 + j] = 0
        for j, char in enumerate(os_filename[:14]):
            dir_bytes[dir_offset + 4 + j] = ord(char)
        dir_bytes[dir_offset + 18] = 1 # USED

        disk.seek(INODE_TABLE_SECTOR * 512)
        disk.write(inode_bytes)
        disk.seek(ROOT_DIR_SECTOR * 512)
        disk.write(dir_bytes)

        disk.seek(start_sector * 512)
        # Pad payload to match full 512-byte block structures if it's a short test file
        padded_payload = payload + b'\x00' * (512 - (payload_len % 512))
        disk.write(padded_payload)

        print(f"Successfully injected '{host_filename}' into NeoFS as '{os_filename}' ({payload_len} bytes)!")

if __name__ == "__main__":
    inject_file("hello.bin", "hello.bin")
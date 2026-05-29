
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define NULL 0

#include "../drivers/ata.h"
#include "../fs/neofs.h"
#include "../memory/memory.h"
#include "../shell/string.h"
#include "../drivers/terminal.h"

typedef void (*entry_point_t)();

extern "C" uint32_t current_app_base_address;

void sys_exec(const char* filename) {
    alignas(4) uint8_t sector_buffer[512];
    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

    alignas(4) uint8_t dir_buffer[512];
    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    int target_inode = -1;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, filename) == 0) {
            target_inode = entries[i].inode_num;
            break;
        }
    }

    if (target_inode == -1 || inodes[target_inode].type != TYPE_FILE) {
        kprint_error("Exec Error: Executable binary not found.\n");
        return;
    }

    uint32_t bin_size = inodes[target_inode].size;
    if (bin_size == 0) {
        kprint_error("Exec Error: Binary file is empty.\n");
        return;
    }

    uint32_t blocks_needed = (bin_size + (BLOCK_SIZE - 1)) / BLOCK_SIZE;

    uint8_t* program_space = (uint8_t*)kmalloc(blocks_needed);

    if (program_space == NULL) {
        kprint_error("Exec Error: Failed to allocate process memory.\n");
        return;
    }
    
    // Grab the start sector directly from the inode we already found right here!
    uint32_t start_sector = inodes[target_inode].start_sector;

    // Call our lightweight data stream reader
    neofs_read_raw_data(start_sector, bin_size, (char*)program_space);

    current_app_base_address = (uint32_t)program_space;

    entry_point_t start_program = (entry_point_t)program_space;
    
    start_program(); 

    kprint_success("Process finished execution. Regaining kernel control.\n");
    
    // kfree(program_space); 
}
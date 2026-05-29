typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define NULL 0

#include "neofs.h"
#include "../drivers/ata.h"
#include "../drivers/terminal.h"
#include "../memory/memory.h"
#include "../shell/string.h"

int current_directory_inode = 0;

void neofs_init() {
    current_directory_inode = 0; 
    alignas(4) uint8_t startup_buffer[512];
    ata_read_sector(INODE_TABLE_SECTOR, startup_buffer);
}

void neofs_ls() {
    alignas(4) uint8_t sector_buffer[512];
    alignas(4) uint8_t inode_buffer[512];
    
    ata_read_sector(INODE_TABLE_SECTOR, inode_buffer);
    neofs_inode* inodes = (neofs_inode*)inode_buffer;
    uint32_t dir_sector = inodes[current_directory_inode].start_sector;

    ata_read_sector(dir_sector, sector_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)sector_buffer;

    int items_found = 0;
    kprint("Directory listing:\n");

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1) {
            items_found++;
            uint32_t item_inode = entries[i].inode_num;
            
            if (inodes[item_inode].type == TYPE_DIR) {
                kprint("  [DIR]  ");
            } else {
                kprint("  [FILE] ");
            }
            
            kprint(entries[i].name);
            kprint("\n");
        }
    }

    if (items_found == 0) {
        kprint("  (empty directory)\n");
    }
}

void neofs_cd(const char* target_name) {
    alignas(4) uint8_t sector_buffer[512];

    if (kstrcmp(target_name, "..") == 0) {
        current_directory_inode = 0;
        return;
    }

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;
    uint32_t dir_sector = inodes[current_directory_inode].start_sector;

    ata_read_sector(dir_sector, sector_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)sector_buffer;

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, target_name) == 0) {
            current_directory_inode = entries[i].inode_num;
            return;
        }
    }

    kprint("NeoFS Error: Directory '");
    kprint(target_name);
    kprint("' not found.\n");
}

void neofs_mkdir(const char* dir_name) {
    alignas(4) uint8_t inode_buffer[512];
    alignas(4) uint8_t dir_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, inode_buffer);
    neofs_inode* inodes = (neofs_inode*)inode_buffer;

    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, dir_name) == 0) {
            kprint("NeoFS Error: Directory or file already exists.\n");
            return;
        }
    }

    int free_inode = -1;
    for (int i = 1; i < MAX_INODES; i++) {
        if (inodes[i].used == 0) {
            free_inode = i;
            break;
        }
    }

    if (free_inode == -1) {
        kprint("NeoFS Error: Maximum Inode allocation limit hit!\n");
        return;
    }

    int free_entry_slot = -1;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 0) {
            free_entry_slot = i;
            break;
        }
    }

    if (free_entry_slot == -1) {
        kprint("NeoFS Error: Current directory is completely full!\n");
        return;
    }

    inodes[free_inode].type = TYPE_DIR;
    inodes[free_inode].size = 0;
    inodes[free_inode].start_sector = 10 + free_inode; 
    inodes[free_inode].used = 1;

    entries[free_entry_slot].inode_num = free_inode;
    entries[free_entry_slot].used = 1;
    kstrncpy(entries[free_entry_slot].name, dir_name, MAX_FILENAME);

    ata_write_sector(INODE_TABLE_SECTOR, inode_buffer);
    ata_write_sector(current_dir_sector, dir_buffer);

    alignas(4) uint8_t clear_buffer[512];
    for(int i=0; i<512; i++) clear_buffer[i] = 0;
    ata_write_sector(inodes[free_inode].start_sector, clear_buffer);

    kprint("Directory '");
    kprint(dir_name);
    kprint("' created.\n");
}

void neofs_touch(const char* filename) {
    alignas(4) uint8_t inode_buffer[512];
    alignas(4) uint8_t dir_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, inode_buffer);
    neofs_inode* inodes = (neofs_inode*)inode_buffer;

    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, filename) == 0) {
            return; 
        }
    }

    int free_inode = -1;
    for (int i = 1; i < MAX_INODES; i++) {
        if (inodes[i].used == 0) {
            free_inode = i;
            break;
        }
    }

    if (free_inode == -1) {
        kprint("NeoFS Error: Maximum Inode limit reached!\n");
        return;
    }

    int free_entry_slot = -1;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 0) {
            free_entry_slot = i;
            break;
        }
    }

    if (free_entry_slot == -1) {
        kprint("NeoFS Error: Current directory is full!\n");
        return;
    }

    inodes[free_inode].type = TYPE_FILE;
    inodes[free_inode].size = 0;          
    inodes[free_inode].start_sector = 20 + free_inode;
    inodes[free_inode].used = 1;

    entries[free_entry_slot].inode_num = free_inode;
    entries[free_entry_slot].used = 1;
    kstrncpy(entries[free_entry_slot].name, filename, MAX_FILENAME);

    ata_write_sector(INODE_TABLE_SECTOR, inode_buffer);
    ata_write_sector(current_dir_sector, dir_buffer);

    alignas(4) uint8_t clear_buffer[512];
    for(int i = 0; i < 512; i++) clear_buffer[i] = 0;
    ata_write_sector(inodes[free_inode].start_sector, clear_buffer);

    kprint("File '");
    kprint(filename);
    kprint("' created.\n");
}

void neofs_write(const char* filename, const char* text) {
    alignas(4) uint8_t sector_buffer[512];
    alignas(4) uint8_t dir_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

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
        kprint_error("NeoFS Error: File not found.\n");
        return;
    }

    uint32_t clear_sector = inodes[target_inode].start_sector;
    alignas(4) uint8_t blank_block[512];
    for (int i = 0; i < 512; i++) blank_block[i] = 0;

    if (clear_sector != 0) {
        alignas(4) uint8_t trace_buffer[512];
        ata_read_sector(clear_sector, trace_buffer);
        uint32_t next_linked_sector = *(uint32_t*)&trace_buffer[508];
        
        ata_write_sector(clear_sector, blank_block); // Reset base
        clear_sector = next_linked_sector;

        while (clear_sector != 0xFFFFFFFF && clear_sector != 0) {
            ata_read_sector(clear_sector, trace_buffer);
            uint32_t next = *(uint32_t*)&trace_buffer[508];
            ata_write_sector(clear_sector, blank_block); // Erase block completely
            clear_sector = next;
        }
    }

    uint32_t total_len = 0;
    while (text[total_len] != '\0') {
        total_len++;
    }

    if (total_len == 0) {
        kprint_warning("NeoFS Warning: Empty text payload. Writing blank file.\n");
    }

    uint8_t* data_block = (uint8_t*)kmalloc(32); 
    if (data_block == NULL) {
        kprint_error("NeoFS Error: Out of heap memory! kmalloc failed.\n");
        return;
    }

    uint32_t bytes_written = 0;
    uint32_t current_sector = inodes[target_inode].start_sector;

    while (bytes_written < total_len || total_len == 0) {
        for (int i = 0; i < 512; i++) data_block[i] = 0;

        uint32_t chunk_size = (total_len - bytes_written > 508) ? 508 : (total_len - bytes_written);
        for (uint32_t i = 0; i < chunk_size; i++) {
            data_block[i] = text[bytes_written + i];
        }
        bytes_written += chunk_size;

        uint32_t next_sector = 0xFFFFFFFF;

        if (bytes_written < total_len) {
            next_sector = neofs_find_free_sector(); 
            if (next_sector == 0) {
                kprint_error("NeoFS Error: Disk storage capacity full!\n");
                return;
            }
        }

        uint32_t* next_sector_link = (uint32_t*)&data_block[508];
        *next_sector_link = next_sector;

        ata_write_sector(current_sector, data_block);
        current_sector = next_sector;

        if (total_len == 0) break;
    }

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    inodes = (neofs_inode*)sector_buffer;
    inodes[target_inode].size = total_len;

    ata_write_sector(INODE_TABLE_SECTOR, sector_buffer);              
    kprint_success("Committed large payload to multi-sector storage chain.\n");
}

void neofs_cat(const char* filename) {
    alignas(4) uint8_t sector_buffer[512];
    alignas(4) uint8_t dir_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

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
        kprint_error("NeoFS Error: File not found.\n");
        return;
    }

    uint32_t current_sector = inodes[target_inode].start_sector;
    uint32_t total_bytes_left = inodes[target_inode].size;

    if (total_bytes_left == 0) {
        kprint_info("(Empty File)\n");
        return;
    }

    alignas(4) uint8_t data_block[512];
    while (current_sector != 0xFFFFFFFF && total_bytes_left > 0) {
        ata_read_sector(current_sector, data_block);
        uint32_t bytes_to_print = (total_bytes_left > 508) ? 508 : total_bytes_left;

        for (uint32_t i = 0; i < bytes_to_print; i++) {
            terminal_put_char((char)data_block[i]);
        }

        total_bytes_left -= bytes_to_print;
        uint32_t* next_sector_ptr = (uint32_t*)&data_block[508];
        current_sector = *next_sector_ptr;
    }
    kprint("\n");
}

void neofs_rm(const char* filename) {
    alignas(4) uint8_t inode_buffer[512];
    alignas(4) uint8_t dir_buffer[512];
    alignas(4) uint8_t scratch_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, inode_buffer);
    neofs_inode* inodes = (neofs_inode*)inode_buffer;

    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    int target_entry_slot = -1;
    int target_inode = -1;

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, filename) == 0) {
            target_entry_slot = i;
            target_inode = entries[i].inode_num;
            break;
        }
    }

    if (target_inode == -1) {
        kprint("NeoFS Error: File not found.\n");
        return;
    }
    if (inodes[target_inode].type == TYPE_DIR) {
        kprint("NeoFS Error: Cannot remove a directory with 'rm'.\n");
        return;
    }

    uint32_t current_sector = inodes[target_inode].start_sector;
    for (int i = 0; i < 512; i++) scratch_buffer[i] = 0;

    while (current_sector != 0xFFFFFFFF && current_sector != 0) {
        alignas(4) uint8_t link_capture_buffer[512];
        ata_read_sector(current_sector, link_capture_buffer);
        uint32_t next_sector = *(uint32_t*)&link_capture_buffer[508];

        ata_write_sector(current_sector, scratch_buffer);
        current_sector = next_sector;
    }

    inodes[target_inode].used = 0;
    inodes[target_inode].size = 0;
    inodes[target_inode].start_sector = 0;

    entries[target_entry_slot].used = 0;
    entries[target_entry_slot].inode_num = 0;
    for (int i = 0; i < MAX_FILENAME; i++) entries[target_entry_slot].name[i] = '\0';

    ata_write_sector(INODE_TABLE_SECTOR, inode_buffer);
    ata_write_sector(current_dir_sector, dir_buffer);

    kprint("File '");
    kprint(filename);
    kprint("' removed successfully.\n");
}

void neofs_format() {
    alignas(4) uint8_t sector_buffer[512];
    for (int i = 0; i < 512; i++) sector_buffer[i] = 0;
    
    neofs_inode* inodes = (neofs_inode*)sector_buffer;
    inodes[0].type = TYPE_DIR;
    inodes[0].size = 0;
    inodes[0].start_sector = ROOT_DIR_SECTOR;
    inodes[0].used = 1;

    ata_write_sector(INODE_TABLE_SECTOR, sector_buffer);

    for (int i = 0; i < 512; i++) sector_buffer[i] = 0;
    ata_write_sector(ROOT_DIR_SECTOR, sector_buffer);

    kprint("NeoFS filesystem initialized with root layout successfully.\n");

    neofs_mkdir("home");
    neofs_mkdir("bin");
    neofs_mkdir("etc");
    neofs_mkdir("var");
    neofs_mkdir("tmp");
    neofs_touch("readme.txt");
    neofs_write("readme.txt", "Welcome to NeoDeck OS! This is a simple text file created on the root directory of your NeoFS virtual disk. Feel free to explore the filesystem, create new directories and files, and write your own content. \n");
}

uint32_t neofs_read_to_buffer(const char* filename, char* out_buffer, uint32_t max_size) {
    alignas(4) uint8_t sector_buffer[512];
    alignas(4) uint8_t dir_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

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

    // If file doesn't exist, has no metadata, or is a folder, abort buffer fill
    if (target_inode == -1 || inodes[target_inode].type != TYPE_FILE) {
        return 0; 
    }

    uint32_t current_sector = inodes[target_inode].start_sector;
    uint32_t total_bytes_left = inodes[target_inode].size;
    uint32_t bytes_read = 0;

    alignas(4) uint8_t data_block[512];
    
    while (current_sector != 0xFFFFFFFF && total_bytes_left > 0 && bytes_read < max_size - 1) {
        ata_read_sector(current_sector, data_block);
        
        uint32_t chunk = (total_bytes_left > 508) ? 508 : total_bytes_left;

        for (uint32_t i = 0; i < chunk; i++) {
            if (bytes_read < max_size - 1) {
                out_buffer[bytes_read++] = (char)data_block[i];
            }
        }
        
        total_bytes_left -= chunk;
        
        uint32_t* next_sector_ptr = (uint32_t*)&data_block[508];
        current_sector = *next_sector_ptr;
    }
    
    out_buffer[bytes_read] = '\0'; 
    return bytes_read; 
}

uint32_t neofs_find_free_sector() {
    alignas(4) uint8_t sector_buffer[512];
    
    for (uint32_t sector = 20; sector < 2000; sector++) {
        ata_read_sector(sector, sector_buffer);
        
        int is_free = 1;
        for (int i = 0; i < 512; i++) {
            if (sector_buffer[i] != 0) {
                is_free = 0;
                break;
            }
        }
        if (is_free) return sector;
    }
    return 0; 
}

// A streamlined version that takes a known start sector and size directly
uint32_t neofs_read_raw_data(uint32_t start_sector, uint32_t file_size, char* out_buffer) {
    uint32_t current_sector = start_sector;
    uint32_t total_bytes_left = file_size;
    uint32_t bytes_read = 0;
    
    alignas(4) uint8_t data_block[512];
    
    while (current_sector != 0xFFFFFFFF && total_bytes_left > 0) {

        ata_read_sector(current_sector, data_block);
        
        uint32_t chunk = (total_bytes_left > 508) ? 508 : total_bytes_left;

        for (uint32_t i = 0; i < chunk; i++) {
            out_buffer[bytes_read++] = (char)data_block[i];
        }

        total_bytes_left -= chunk;

        uint32_t* next_sector_ptr = (uint32_t*)&data_block[508];
        current_sector = *next_sector_ptr;

    }

    out_buffer[bytes_read] = '\0'; 
    return bytes_read; 
}

// Returns the size of a file in bytes, or 0 if it doesn't exist
uint32_t neofs_get_file_size(const char* filename) {
    alignas(4) uint8_t sector_buffer[512];
    alignas(4) uint8_t dir_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, filename) == 0) {
            uint32_t target_inode = entries[i].inode_num;
            if (inodes[target_inode].type == TYPE_FILE) {
                return inodes[target_inode].size;
            }
        }
    }
    return 0; // File not found or is a directory
}

// Safely dumps raw directory entries directly into a user-provided buffer
void neofs_get_dir_entries(void* dest_buffer) {
    alignas(4) uint8_t sector_buffer[512];
    alignas(4) uint8_t dir_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    
    // Read the active entries right from the drive and drop them into the target buffer space
    ata_read_sector(current_dir_sector, (uint8_t*)dest_buffer);
}
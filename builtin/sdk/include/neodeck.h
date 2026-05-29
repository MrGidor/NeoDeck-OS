#ifndef NEODECK_H
#define NEODECK_H

typedef unsigned char  uint8_t;
typedef unsigned int   uint32_t;

// Core definitions shared with userspace
#define MAX_DIR_ENTRIES 16
#define MAX_FILENAME    32
#define TYPE_FILE       1
#define TYPE_DIR        2
#define ROOT_DIR_SECTOR 2

struct neofs_dir_entry {
    uint32_t inode_num;
    char name[MAX_FILENAME];
    uint8_t used;
};

// Syscall API Wrappers
void sys_print(const char* text);
void sys_clear();
char sys_get_key();
void sys_readline(char* buffer, int max_len);
void sys_get_dir_entries(void* dest_buffer);
void sys_mkdir(const char* dir_name);
void sys_cd(const char* target_dir);
void sys_touch(const char* filename);
void sys_write_file(const char* filename, const char* text);
void sys_cat(const char* filename);
uint32_t sys_get_active_dir_inode();
void sys_format();

#endif
#ifndef NEOFS_H
#define NEOFS_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define INODE_TABLE_SECTOR 2
#define ROOT_DIR_SECTOR    3
#define MAX_INODES 16
#define MAX_DIR_ENTRIES 16
#define MAX_FILENAME 14
#define TYPE_FILE 1
#define TYPE_DIR  2

struct neofs_inode {
    uint8_t  type;
    uint32_t size;
    uint32_t start_sector;
    uint8_t  used;
} __attribute__((packed));

struct neofs_dir_entry {
    uint32_t inode_num;
    char     name[MAX_FILENAME];
    uint8_t  used;
} __attribute__((packed));

void neofs_init();
uint32_t neofs_find_free_sector();
void neofs_format();
void neofs_ls();
void neofs_cd(const char* target_name);
void neofs_mkdir(const char* dir_name);
void neofs_touch(const char* filename);
void neofs_write(const char* filename, const char* text);
void neofs_cat(const char* filename);
void neofs_rm(const char* filename);
uint32_t neofs_read_to_buffer(const char* filename, char* out_buffer, uint32_t max_size);

extern int current_directory_inode;

#endif
#ifndef FAT32_H
#define FAT32_H

#include "../include/types.h"

/*
 * FAT32 driver (512-byte sectors, any power-of-two sectors-per-cluster).
 *
 *  - Volume found either at LBA 0 ("superfloppy") or in the first FAT32
 *    partition (type 0x0B/0x0C) of an MBR.
 *  - Reads long file names (VFAT); creates 8.3 names only (see fat32_make_sfn).
 *  - No printing in here: every call returns FAT_OK or a negative FAT_E_* code.
 *    The user-facing messages live in fs.cpp.
 */

#define FAT_OK            0
#define FAT_E_NOFS       -1
#define FAT_E_NOTFOUND   -2
#define FAT_E_EXISTS     -3
#define FAT_E_NOTDIR     -4
#define FAT_E_ISDIR      -5
#define FAT_E_NAME       -6   /* name cannot be stored as an 8.3 name   */
#define FAT_E_FULL       -7   /* out of clusters                        */
#define FAT_E_NOTEMPTY   -8
#define FAT_E_TOOLONG    -9   /* path or component too long             */
#define FAT_E_CORRUPT   -10
#define FAT_E_INVAL     -11
#define FAT_E_BUSY      -12   /* e.g. removing the current directory    */
#define FAT_E_NODISK    -13   /* ATA drive missing (format)             */

#define FAT32_NAME_MAX   127
#define FAT32_PATH_MAX   128

#define FAT_ATTR_READONLY 0x01
#define FAT_ATTR_HIDDEN   0x02
#define FAT_ATTR_SYSTEM   0x04
#define FAT_ATTR_VOLUME   0x08
#define FAT_ATTR_DIR      0x10
#define FAT_ATTR_ARCHIVE  0x20

#define FAT32_MAX_LFN_ENTRIES 20

/* One directory entry as returned by listing/lookup.
 * The location fields are internal bookkeeping (used for update/delete). */
struct fat32_dirent {
    char     name[FAT32_NAME_MAX + 1];
    uint8_t  attr;
    uint32_t cluster;
    uint32_t size;

    uint8_t  sfn[11];
    uint32_t lba;
    uint16_t off;
    uint8_t  lfn_count;
    uint32_t lfn_lba[FAT32_MAX_LFN_ENTRIES];
    uint16_t lfn_off[FAT32_MAX_LFN_ENTRIES];
};

/* Callback for fat32_list_dir: return false to stop early. */
typedef bool (*fat32_list_cb)(const fat32_dirent* e, void* ctx);
/* Callback for fat32_stream_file: called once per chunk (<= 512 bytes). */
typedef void (*fat32_stream_cb)(const uint8_t* data, uint32_t len, void* ctx);

int      fat32_mount();
bool     fat32_is_mounted();
int      fat32_format(const char* label, uint32_t volume_id);
uint32_t fat32_cluster_bytes();
uint32_t fat32_total_clusters();
uint32_t fat32_free_clusters();

const char* fat32_cwd();
int      fat32_chdir(const char* path);

int      fat32_list_dir(const char* path, fat32_list_cb cb, void* ctx);
int      fat32_mkdir(const char* path);
int      fat32_create(const char* path);
int      fat32_remove(const char* path);
int      fat32_rmdir(const char* path);
int      fat32_stat(const char* path, fat32_dirent* out);

int      fat32_read_file(const char* path, void* buf, uint32_t max, uint32_t* out_len);
int      fat32_stream_file(const char* path, fat32_stream_cb cb, void* ctx);
int      fat32_write_file(const char* path, const void* data, uint32_t len);

int      fat32_make_sfn(const char* name, uint8_t sfn[11], uint8_t* nt_flags);

#endif

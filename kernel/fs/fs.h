#ifndef FS_H
#define FS_H

#include "../include/types.h"

/*
 * Filesystem front-end used by the shell, editor, exec and syscalls.
 * It prints the user-facing messages and hides which filesystem is behind it
 * (FAT32 today, see fat32.h). Swapping the backend later only touches fs.cpp.
 */

void        fs_init();                       /* mount at boot, tell the user what happened */
void        fs_format();                     /* wipe the disk and create a fresh FAT32 */
void        fs_df();                         /* volume usage */

const char* fs_cwd();                        /* "/home" */
void        fs_ls(const char* path = 0);
void        fs_cd(const char* path);
void        fs_mkdir(const char* path);
void        fs_touch(const char* path);
void        fs_write(const char* path, const char* text);   /* create or replace */
void        fs_cat(const char* path);
void        fs_rm(const char* path);
void        fs_rmdir(const char* path);
bool        fs_prepare_file(const char* path);   /* create if missing; false (with a message) if it cannot be used */

/* Quiet helpers (no messages) for exec/editor/syscalls */
uint32_t    fs_file_size(const char* path);                       /* 0 if missing, empty or a directory */
uint32_t    fs_read_raw(const char* path, void* out, uint32_t max);               /* bytes read */
uint32_t    fs_read_to_buffer(const char* path, char* out, uint32_t max_size);    /* NUL-terminated */
int         fs_get_dir_entries(void* dest, uint32_t max_entries);                 /* neodeck_dirent[], returns count */
int         fs_stat(const char* path, void* dest);                                /* neodeck_stat, 0 = ok */
int         fs_remove_quiet(const char* path);
int         fs_rmdir_quiet(const char* path);

#endif

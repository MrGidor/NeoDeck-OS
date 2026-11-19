#ifndef NEODECK_H
#define NEODECK_H

#include "neodeck_abi.h"   /* syscall numbers + filesystem structs (shared with the kernel) */

typedef unsigned char  uint8_t;
typedef unsigned int   uint32_t;

/*
 * App entry point: the first function in the binary must be
 *     int main(int argc, char** argv)
 * argv[0] is the file name given to `exec`. Return a status or call sys_exit().
 */

// Syscall API Wrappers
void sys_print(const char* text);
void sys_clear();
char sys_get_key();                 // blocks until a key is pressed
void sys_readline(char* buffer, int max_len);
uint32_t sys_read_file(const char* filename, char* output_buffer, uint32_t buffer_size);  // returns bytes read
int  sys_get_dir_entries(neodeck_dirent* dest, uint32_t max_entries);   // returns count, -1 on error
void sys_mkdir(const char* dir_name);
void sys_cd(const char* target_dir);
void sys_touch(const char* filename);
void sys_write_file(const char* filename, const char* text);
void sys_cat(const char* filename);
uint32_t sys_get_cwd(char* buffer, uint32_t size);   // current directory path, returns its length
void sys_format();
int  sys_remove(const char* path);
int  sys_rmdir(const char* path);
int  sys_stat(const char* path, neodeck_stat* out);
void sys_exit(int status) __attribute__((noreturn));
void sys_sleep(uint32_t milliseconds);
uint32_t sys_uptime_ms();

#endif

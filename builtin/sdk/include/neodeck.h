#ifndef NEODECK_H
#define NEODECK_H

#include "neodeck_abi.h"   /* syscall numbers + NeoFS constants (shared with the kernel) */

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
void sys_read_file(const char* filename, char* output_buffer);
void sys_get_dir_entries(void* dest_buffer);
void sys_mkdir(const char* dir_name);
void sys_cd(const char* target_dir);
void sys_touch(const char* filename);
void sys_write_file(const char* filename, const char* text);
void sys_cat(const char* filename);
uint32_t sys_get_active_dir_inode();
void sys_format();
void sys_exit(int status) __attribute__((noreturn));
void sys_sleep(uint32_t milliseconds);
uint32_t sys_uptime_ms();

#endif

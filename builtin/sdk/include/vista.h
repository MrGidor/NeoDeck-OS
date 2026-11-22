#ifndef VISTA_H
#define VISTA_H

#include "vista_abi.h"

typedef unsigned char  uint8_t;
typedef unsigned int   uint32_t;

/*
 * App entry point: the first function in the binary must be
 *     int main(int argc, char** argv)
 * argv[0] is the file name given to `exec`. Return a status or call sys_exit().
 */

void sys_print(const char* text);
void sys_clear();
char sys_get_key();
void sys_readline(char* buffer, int max_len);
uint32_t sys_read_file(const char* filename, char* output_buffer, uint32_t buffer_size);
int  sys_get_dir_entries(vista_dirent* dest, uint32_t max_entries);
void sys_mkdir(const char* dir_name);
void sys_cd(const char* target_dir);
void sys_touch(const char* filename);
void sys_write_file(const char* filename, const char* text);
void sys_cat(const char* filename);
uint32_t sys_get_cwd(char* buffer, uint32_t size);
void sys_format();
int  sys_remove(const char* path);
int  sys_rmdir(const char* path);
int  sys_stat(const char* path, vista_stat* out);
void sys_exit(int status) __attribute__((noreturn));
void sys_sleep(uint32_t milliseconds);
uint32_t sys_uptime_ms();
void sys_yield();
uint32_t sys_getpid();
int  sys_spawn(const char* cmdline, uint32_t flags);
int  sys_wait(int pid, int* status);
int  sys_kill(int pid);

#endif

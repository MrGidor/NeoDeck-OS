typedef unsigned int   uint32_t;

#include "vista.h"

void sys_print(const char* text) {
    asm volatile(
        "int $0x80"
        :
        : "a"(VISTA_SYS_PRINT), "b"(text)
        : "memory"
    );
}

void sys_clear() {
    asm volatile(
        "int $0x80"
        :
        : "a"(VISTA_SYS_CLEAR)
        : "memory"
    );
}

char sys_get_key() {
    char key;
    asm volatile(
        "int $0x80"
        : "=a"(key)  
        : "a"(VISTA_SYS_GET_KEY)
        : "memory"
    );
    return key;
}

void sys_readline(char* buffer, int max_len) {
    int index = 0;
    while (index < max_len - 1) {
        char c = sys_get_key(); 
        if (c == 0) { sys_yield(); continue; }
        
        if (c == '\n') {
            sys_print("\n");
            break;
        }
        else if (c == '\b') {
            if (index > 0) {
                index--;
                sys_print("\b");
            }
        } 
        else {
            buffer[index++] = c;
        }
    }
    buffer[index] = '\0';
}

uint32_t sys_read_file(const char* filename, char* output_buffer, uint32_t buffer_size) {
    uint32_t n;
    asm volatile(
        "int $0x80"
        : "=a"(n)
        : "a"(VISTA_SYS_READ_FILE), "b"(filename), "c"(output_buffer), "d"(buffer_size)
        : "memory"
    );
    return n;
}

int sys_get_dir_entries(vista_dirent* dest, uint32_t max_entries) {
    int n;
    asm volatile(
        "int $0x80"
        : "=a"(n)
        : "a"(VISTA_SYS_GET_DIR_ENTRIES), "b"(dest), "c"(max_entries)
        : "memory"
    );
    return n;
}

void sys_mkdir(const char* name) { asm volatile("int $0x80" :: "a"(VISTA_SYS_MKDIR), "b"(name) : "memory"); }
void sys_cd(const char* name) { asm volatile("int $0x80" :: "a"(VISTA_SYS_CD), "b"(name) : "memory"); }
void sys_touch(const char* name) { asm volatile("int $0x80" :: "a"(VISTA_SYS_TOUCH), "b"(name) : "memory"); }
void sys_write_file(const char* name, const char* text) { asm volatile("int $0x80" :: "a"(VISTA_SYS_WRITE_FILE), "b"(name), "c"(text) : "memory"); }
void sys_cat(const char* name) { asm volatile("int $0x80" :: "a"(VISTA_SYS_CAT), "b"(name) : "memory"); }

uint32_t sys_get_cwd(char* buffer, uint32_t size) {
    uint32_t len;
    asm volatile("int $0x80" : "=a"(len) : "a"(VISTA_SYS_GET_CWD), "b"(buffer), "c"(size) : "memory");
    return len;
}

void sys_format() { asm volatile("int $0x80" :: "a"(VISTA_SYS_FORMAT) : "memory"); }

int sys_remove(const char* path) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(VISTA_SYS_REMOVE), "b"(path) : "memory");
    return r;
}

int sys_rmdir(const char* path) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(VISTA_SYS_RMDIR), "b"(path) : "memory");
    return r;
}

int sys_stat(const char* path, vista_stat* out) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(VISTA_SYS_STAT), "b"(path), "c"(out) : "memory");
    return r;
}

void sys_exit(int status) {
    asm volatile("int $0x80" :: "a"(VISTA_SYS_EXIT), "b"(status) : "memory");
    for (;;) { }
}

void sys_sleep(uint32_t milliseconds) {
    asm volatile("int $0x80" :: "a"(VISTA_SYS_SLEEP), "b"(milliseconds) : "memory");
}

uint32_t sys_uptime_ms() {
    uint32_t ms;
    asm volatile("int $0x80" : "=a"(ms) : "a"(VISTA_SYS_UPTIME_MS) : "memory");
    return ms;
}

void sys_yield() {
    asm volatile("int $0x80" :: "a"(VISTA_SYS_YIELD) : "memory");
}

uint32_t sys_getpid() {
    uint32_t pid;
    asm volatile("int $0x80" : "=a"(pid) : "a"(VISTA_SYS_GETPID) : "memory");
    return pid;
}

int sys_spawn(const char* cmdline, uint32_t flags) {
    int pid;
    asm volatile("int $0x80" : "=a"(pid) : "a"(VISTA_SYS_SPAWN), "b"(cmdline), "c"(flags) : "memory");
    return pid;
}

int sys_wait(int pid, int* status) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(VISTA_SYS_WAIT), "b"(pid), "c"(status) : "memory");
    return r;
}

int sys_kill(int pid) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(VISTA_SYS_KILL), "b"(pid) : "memory");
    return r;
}

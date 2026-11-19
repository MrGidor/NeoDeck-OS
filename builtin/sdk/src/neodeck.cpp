typedef unsigned int   uint32_t;

#include "neodeck.h"

void sys_print(const char* text) {
    asm volatile(
        "int $0x80"
        :
        : "a"(NEODECK_SYS_PRINT), "b"(text)
        : "memory"
    );
}

void sys_clear() {
    asm volatile(
        "int $0x80"
        :
        : "a"(NEODECK_SYS_CLEAR)
        : "memory"
    );
}

char sys_get_key() {
    char key;
    // Blocks inside the kernel (hlt) until a key is typed
    // "=a"(key) means: read the final value of EAX into the 'key' variable when done
    asm volatile(
        "int $0x80"
        : "=a"(key)  
        : "a"(NEODECK_SYS_GET_KEY)
        : "memory"
    );
    return key;
}

void sys_readline(char* buffer, int max_len) {
    int index = 0;
    while (index < max_len - 1) {
        // This call will now freeze execution inside the KERNEL until a key is hit
        char c = sys_get_key(); 
        
        if (c == '\n') {
            sys_print("\n");
            break;
        }
        else if (c == '\b') { // Backspace handling
            if (index > 0) {
                index--;
                sys_print("\b"); // Erase character visually on screen
            }
        } 
        else {
            buffer[index++] = c;
        }
    }
    buffer[index] = '\0'; // Null-terminate string
}

uint32_t sys_read_file(const char* filename, char* output_buffer, uint32_t buffer_size) {
    uint32_t n;
    asm volatile(
        "int $0x80"
        : "=a"(n)
        : "a"(NEODECK_SYS_READ_FILE), "b"(filename), "c"(output_buffer), "d"(buffer_size)
        : "memory"
    );
    return n;
}

int sys_get_dir_entries(neodeck_dirent* dest, uint32_t max_entries) {
    int n;
    asm volatile(
        "int $0x80"
        : "=a"(n)
        : "a"(NEODECK_SYS_GET_DIR_ENTRIES), "b"(dest), "c"(max_entries)
        : "memory"
    );
    return n;
}

void sys_mkdir(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_MKDIR), "b"(name) : "memory"); }
void sys_cd(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_CD), "b"(name) : "memory"); }
void sys_touch(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_TOUCH), "b"(name) : "memory"); }
void sys_write_file(const char* name, const char* text) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_WRITE_FILE), "b"(name), "c"(text) : "memory"); }
void sys_cat(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_CAT), "b"(name) : "memory"); }

uint32_t sys_get_cwd(char* buffer, uint32_t size) {
    uint32_t len;
    asm volatile("int $0x80" : "=a"(len) : "a"(NEODECK_SYS_GET_CWD), "b"(buffer), "c"(size) : "memory");
    return len;
}

void sys_format() { asm volatile("int $0x80" :: "a"(NEODECK_SYS_FORMAT) : "memory"); }

int sys_remove(const char* path) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(NEODECK_SYS_REMOVE), "b"(path) : "memory");
    return r;
}

int sys_rmdir(const char* path) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(NEODECK_SYS_RMDIR), "b"(path) : "memory");
    return r;
}

int sys_stat(const char* path, neodeck_stat* out) {
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(NEODECK_SYS_STAT), "b"(path), "c"(out) : "memory");
    return r;
}

void sys_exit(int status) {
    asm volatile("int $0x80" :: "a"(NEODECK_SYS_EXIT), "b"(status) : "memory");
    for (;;) { }   // never reached; the kernel unwinds back to the shell
}

void sys_sleep(uint32_t milliseconds) {
    asm volatile("int $0x80" :: "a"(NEODECK_SYS_SLEEP), "b"(milliseconds) : "memory");
}

uint32_t sys_uptime_ms() {
    uint32_t ms;
    asm volatile("int $0x80" : "=a"(ms) : "a"(NEODECK_SYS_UPTIME_MS) : "memory");
    return ms;
}

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

void sys_read_file(const char* filename, char* output_buffer) {
    asm volatile(
        "int $0x80"
        :
        : "a"(NEODECK_SYS_READ_FILE), "b"(filename), "c"(output_buffer)
        : "memory"
    );
}

void sys_get_dir_entries(void* dest_buffer) {
    asm volatile(
        "int $0x80" 
        : 
        : "a"(NEODECK_SYS_GET_DIR_ENTRIES), "b"(dest_buffer) 
        : "memory"
    );
}

void sys_mkdir(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_MKDIR), "b"(name) : "memory"); }
void sys_cd(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_CD), "b"(name) : "memory"); }
void sys_touch(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_TOUCH), "b"(name) : "memory"); }
void sys_write_file(const char* name, const char* text) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_WRITE_FILE), "b"(name), "c"(text) : "memory"); }
void sys_cat(const char* name) { asm volatile("int $0x80" :: "a"(NEODECK_SYS_CAT), "b"(name) : "memory"); }

uint32_t sys_get_active_dir_inode() {
    uint32_t res;
    asm volatile("int $0x80" : "=a"(res) : "a"(NEODECK_SYS_GET_ACTIVE_DIR) : "memory");
    return res;
}

void sys_format() { asm volatile("int $0x80" :: "a"(NEODECK_SYS_FORMAT) : "memory"); }

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

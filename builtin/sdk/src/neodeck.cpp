typedef unsigned int   uint32_t;

#include "neodeck.h"

void sys_print(const char* text) {
    asm volatile(
        "int $0x80"
        :
        : "a"(1), "b"(text)
        : "memory"
    );
}

void sys_clear() {
    asm volatile(
        "int $0x80"
        :
        : "a"(2)
        : "memory"
    );
}

char sys_get_key() {
    char key;
    // Syscall ID 3: Get Key
    // "=a"(key) means: read the final value of EAX into the 'key' variable when done
    asm volatile(
        "int $0x80"
        : "=a"(key)  
        : "a"(3)
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
        : "a"(4), "b"(filename), "c"(output_buffer)
        : "memory"
    );
}

void sys_get_dir_entries(void* dest_buffer) {
    asm volatile(
        "int $0x80" 
        : 
        : "a"(5), "b"(dest_buffer) 
        : "memory"
    );
}

void sys_mkdir(const char* name) { asm volatile("int $0x80" :: "a"(6), "b"(name) : "memory"); }
void sys_cd(const char* name) { asm volatile("int $0x80" :: "a"(7), "b"(name) : "memory"); }
void sys_touch(const char* name) { asm volatile("int $0x80" :: "a"(8), "b"(name) : "memory"); }
void sys_write_file(const char* name, const char* text) { asm volatile("int $0x80" :: "a"(9), "b"(name), "c"(text) : "memory"); }
void sys_cat(const char* name) { asm volatile("int $0x80" :: "a"(10), "b"(name) : "memory"); }

uint32_t sys_get_active_dir_inode() {
    uint32_t res;
    asm volatile("int $0x80" : "=a"(res) : "a"(11) : "memory");
    return res;
}

void sys_format() { asm volatile("int $0x80" :: "a"(12) : "memory"); }
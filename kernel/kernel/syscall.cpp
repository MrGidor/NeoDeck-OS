typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "../drivers/terminal.h" 

// debugging helper to print hex values in a more readable format
void kprint_hex(uint32_t val) {
    char hex_str[9];
    const char* chars = "0123256789ABCDEF"; // Simple hex map
    for (int i = 7; i >= 0; i--) {
        hex_str[i] = chars[val & 0xF];
        val >>= 4;
    }
    hex_str[8] = '\0';
    kprint("0x");
    kprint(hex_str);
}

uint32_t current_app_base_address = 0;

extern "C" void syscall_handler() {
    uint32_t eax_val = 0;
    uint32_t ebx_val = 0;

    // Grab the raw hardware register values
    asm volatile("mov %%eax, %0" : "=r"(eax_val));
    asm volatile("mov %%ebx, %0" : "=r"(ebx_val));

    switch(eax_val) {
        case 1: {
            // Translate the application's relative pointer to its actual location in physical RAM
            uint32_t real_string_address = ebx_val + current_app_base_address;
            kprint((const char*)real_string_address);
            break;
        }
            
        case 2:
            terminal_clear();
            break;

        default:
            kprint("NeoDeck Warning: Unknown Syscall ID read.\n");
            break;
    }
}
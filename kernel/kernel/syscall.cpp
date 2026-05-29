typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "../drivers/terminal.h" 
#include "../drivers/keyboard.h"
#include "../fs/neofs.h"



uint32_t current_app_base_address = 0;

struct cpu_registers_t {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

extern "C" __attribute__((cdecl)) void syscall_handler(cpu_registers_t* regs) {
    asm volatile("sti");

    uint32_t eax_val = regs->eax;
    uint32_t ebx_val = regs->ebx;
    uint32_t ecx_val = regs->ecx;

    switch(eax_val) {
        case 1: { // sys_print
            kprint((const char*)ebx_val);
            break;
        }
        case 2: { // sys_clear
            terminal_clear();
            break;
        }
        case 3: { // sys_get_key
            while (!keyboard_has_chars()) {
                asm volatile("hlt"); 
            }
            regs->eax = (uint32_t)keyboard_pop_char(); 
            break;
        }
        case 4: { // sys_read_file
            const char* filename = (const char*)ebx_val;
            char* dest_buffer    = (char*)ecx_val;

            uint32_t file_size = neofs_get_file_size(filename); 
            if (file_size > 0) {
                neofs_read_to_buffer(filename, dest_buffer, file_size + 1);
            }
            break;
        }
        case 5: { // sys_get_dir_entries
            neofs_get_dir_entries((void*)ebx_val);
            break;
        }
        case 6: { // sys_mkdir
            neofs_mkdir((const char*)ebx_val);
            break;
        }
        case 7: { // sys_cd
            neofs_cd((const char*)ebx_val);
            break;
        }
        case 8: { // sys_touch
            neofs_touch((const char*)ebx_val);
            break;
        }
        case 9: { // sys_write_file
            neofs_write((const char*)ebx_val, (const char*)ecx_val);
            break;
        }
        case 10: { // sys_cat
            neofs_cat((const char*)ebx_val);
            break;
        }
        case 11: { // sys_get_active_dir_inode
            regs->eax = (uint32_t)current_directory_inode;
            break;
        }
        case 12: { // sys_format
            neofs_format();
            break;
        }
        default:
            kprint("NeoDeck Warning: Unknown Syscall ID read.\n");
            break;
    }
    asm volatile("cli");
}
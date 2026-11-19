#include "../include/types.h"
#include "neodeck_abi.h"
#include "../drivers/terminal.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../fs/neofs.h"
#include "execve.h"



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
        case NEODECK_SYS_PRINT: { // sys_print
            kprint((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_CLEAR: { // sys_clear
            terminal_clear();
            break;
        }
        case NEODECK_SYS_GET_KEY: { // blocks in hlt until the IRQ fills the buffer
            regs->eax = (uint32_t)(uint8_t)keyboard_get_char();
            break;
        }
        case NEODECK_SYS_READ_FILE: { // sys_read_file
            const char* filename = (const char*)ebx_val;
            char* dest_buffer    = (char*)ecx_val;

            uint32_t file_size = neofs_get_file_size(filename); 
            if (file_size > 0) {
                neofs_read_to_buffer(filename, dest_buffer, file_size + 1);
            }
            break;
        }
        case NEODECK_SYS_GET_DIR_ENTRIES: { // sys_get_dir_entries
            neofs_get_dir_entries((void*)ebx_val);
            break;
        }
        case NEODECK_SYS_MKDIR: { // sys_mkdir
            neofs_mkdir((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_CD: { // sys_cd
            neofs_cd((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_TOUCH: { // sys_touch
            neofs_touch((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_WRITE_FILE: { // sys_write_file
            neofs_write((const char*)ebx_val, (const char*)ecx_val);
            break;
        }
        case NEODECK_SYS_CAT: { // sys_cat
            neofs_cat((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_GET_ACTIVE_DIR: { // sys_get_active_dir_inode
            regs->eax = (uint32_t)current_directory_inode;
            break;
        }
        case NEODECK_SYS_FORMAT: { // sys_format
            neofs_format();
            break;
        }
        case NEODECK_SYS_EXIT: {
            exec_terminate((int)ebx_val);   // unwinds to sys_exec, never returns here
            break;
        }
        case NEODECK_SYS_SLEEP: {
            timer_sleep_ms(ebx_val);
            break;
        }
        case NEODECK_SYS_UPTIME_MS: {
            regs->eax = timer_uptime_ms();
            break;
        }
        default:
            kprint("NeoDeck Warning: Unknown Syscall ID read.\n");
            break;
    }
    asm volatile("cli");
}
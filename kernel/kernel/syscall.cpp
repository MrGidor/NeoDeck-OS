#include "../include/types.h"
#include "neodeck_abi.h"
#include "../drivers/terminal.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../fs/fs.h"
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
    uint32_t edx_val = regs->edx;

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
        case NEODECK_SYS_READ_FILE: { // EBX=path, ECX=buffer, EDX=buffer size
            regs->eax = fs_read_to_buffer((const char*)ebx_val, (char*)ecx_val, edx_val);
            break;
        }
        case NEODECK_SYS_GET_DIR_ENTRIES: { // EBX=neodeck_dirent[], ECX=max entries
            regs->eax = (uint32_t)fs_get_dir_entries((void*)ebx_val, ecx_val);
            break;
        }
        case NEODECK_SYS_MKDIR: {
            fs_mkdir((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_CD: {
            fs_cd((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_TOUCH: {
            fs_touch((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_WRITE_FILE: {
            fs_write((const char*)ebx_val, (const char*)ecx_val);
            break;
        }
        case NEODECK_SYS_CAT: {
            fs_cat((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_GET_CWD: { // EBX=buffer, ECX=size
            const char* cwd = fs_cwd();
            char* dst = (char*)ebx_val;
            uint32_t n = 0;
            if (ecx_val > 0) {
                while (cwd[n] && n + 1 < ecx_val) { dst[n] = cwd[n]; n++; }
                dst[n] = '\0';
            }
            regs->eax = n;
            break;
        }
        case NEODECK_SYS_FORMAT: {
            fs_format();
            break;
        }
        case NEODECK_SYS_REMOVE: {
            regs->eax = (uint32_t)fs_remove_quiet((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_RMDIR: {
            regs->eax = (uint32_t)fs_rmdir_quiet((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_STAT: {
            regs->eax = (uint32_t)fs_stat((const char*)ebx_val, (void*)ecx_val);
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
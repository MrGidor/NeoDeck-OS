#include "../include/types.h"
#include "../include/layout.h"
#include "vista_abi.h"
#include "../drivers/terminal.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../fs/fs.h"
#include "execve.h"
#include "task.h"

/* Registers as saved by isr80 (pusha) followed by the CPU's ring-3 -> ring-0
 * interrupt frame. Syscalls only ever come from ring 3 (int 0x80, DPL 3 gate). */
struct cpu_registers_t {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, user_esp, user_ss;
};

#define SYSCALL_ERROR 0xFFFFFFFFu

/* The kernel runs in the caller's address space, so user pointers can be used
 * directly - once we know they point inside the caller's own app window. */
static bool uptr_ok(uint32_t p, uint32_t len) {
    if (p < APP_VIRT_BASE || p >= APP_VIRT_BASE + APP_MEM_SIZE) return false;
    return len <= APP_MEM_SIZE && p - APP_VIRT_BASE <= APP_MEM_SIZE - len;
}

static bool ustr_ok(uint32_t p) {
    if (p < APP_VIRT_BASE || p >= APP_VIRT_BASE + APP_MEM_SIZE) return false;
    for (uint32_t a = p; a < APP_VIRT_BASE + APP_MEM_SIZE; a++)
        if (*(const char*)a == '\0') return true;
    return false;
}

extern "C" __attribute__((cdecl)) void syscall_handler(cpu_registers_t* regs) {
    asm volatile("sti");

    uint32_t eax_val = regs->eax;
    uint32_t ebx_val = regs->ebx;
    uint32_t ecx_val = regs->ecx;
    uint32_t edx_val = regs->edx;

    switch(eax_val) {
        case VISTA_SYS_PRINT: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            kprint((const char*)ebx_val);
            break;
        }
        case VISTA_SYS_CLEAR: {
            terminal_clear();
            break;
        }
        case VISTA_SYS_GET_KEY: {
            /* Only the foreground task owns the keyboard; the others poll "no key". */
            if (!task_is_foreground()) { regs->eax = 0; break; }
            regs->eax = (uint32_t)(uint8_t)keyboard_get_char();
            break;
        }
        case VISTA_SYS_READ_FILE: {
            if (!ustr_ok(ebx_val) || edx_val == 0 || !uptr_ok(ecx_val, edx_val)) { regs->eax = 0; break; }
            regs->eax = fs_read_to_buffer((const char*)ebx_val, (char*)ecx_val, edx_val);
            break;
        }
        case VISTA_SYS_GET_DIR_ENTRIES: {
            if (ecx_val > APP_MEM_SIZE / sizeof(vista_dirent) ||
                !uptr_ok(ebx_val, ecx_val * sizeof(vista_dirent))) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_get_dir_entries((void*)ebx_val, ecx_val);
            break;
        }
        case VISTA_SYS_MKDIR: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_mkdir((const char*)ebx_val);
            break;
        }
        case VISTA_SYS_CD: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_cd((const char*)ebx_val);
            break;
        }
        case VISTA_SYS_TOUCH: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_touch((const char*)ebx_val);
            break;
        }
        case VISTA_SYS_WRITE_FILE: {
            if (!ustr_ok(ebx_val) || !ustr_ok(ecx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_write((const char*)ebx_val, (const char*)ecx_val);
            break;
        }
        case VISTA_SYS_CAT: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_cat((const char*)ebx_val);
            break;
        }
        case VISTA_SYS_GET_CWD: {
            if (ecx_val == 0 || !uptr_ok(ebx_val, ecx_val)) { regs->eax = 0; break; }
            const char* cwd = fs_cwd();
            char* dst = (char*)ebx_val;
            uint32_t n = 0;
            while (cwd[n] && n + 1 < ecx_val) { dst[n] = cwd[n]; n++; }
            dst[n] = '\0';
            regs->eax = n;
            break;
        }
        case VISTA_SYS_FORMAT: {
            fs_format();
            break;
        }
        case VISTA_SYS_REMOVE: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_remove_quiet((const char*)ebx_val);
            break;
        }
        case VISTA_SYS_RMDIR: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_rmdir_quiet((const char*)ebx_val);
            break;
        }
        case VISTA_SYS_STAT: {
            if (!ustr_ok(ebx_val) || !uptr_ok(ecx_val, sizeof(vista_stat))) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_stat((const char*)ebx_val, (void*)ecx_val);
            break;
        }
        case VISTA_SYS_EXIT: {
            task_exit((int)ebx_val);
        }
        case VISTA_SYS_SLEEP: {
            timer_sleep_ms(ebx_val);
            break;
        }
        case VISTA_SYS_UPTIME_MS: {
            regs->eax = timer_uptime_ms();
            break;
        }
        case VISTA_SYS_YIELD: {
            task_yield();
            break;
        }
        case VISTA_SYS_GETPID: {
            regs->eax = task_current_pid();
            break;
        }
        case VISTA_SYS_SPAWN: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)exec_spawn((const char*)ebx_val, (ecx_val & VISTA_SPAWN_DETACH) != 0);
            break;
        }
        case VISTA_SYS_WAIT: {
            if (ecx_val != 0 && !uptr_ok(ecx_val, sizeof(int))) { regs->eax = SYSCALL_ERROR; break; }
            int status = 0;
            int r = task_wait((int)ebx_val, &status, false);
            if (r == 0 && ecx_val != 0) *(int*)ecx_val = status;
            regs->eax = (uint32_t)r;
            break;
        }
        case VISTA_SYS_KILL: {
            regs->eax = (uint32_t)task_kill((int)ebx_val);   // killing yourself does not return D:
            break;
        }
        default:
            kprint("Vista Warning: Unknown Syscall ID read.\n");
            regs->eax = SYSCALL_ERROR;
            break;
    }
    asm volatile("cli");
}

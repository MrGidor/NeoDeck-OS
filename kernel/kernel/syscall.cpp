#include "../include/types.h"
#include "../include/layout.h"
#include "neodeck_abi.h"
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
    return false;                    /* no terminator before the end of the window */
}

extern "C" __attribute__((cdecl)) void syscall_handler(cpu_registers_t* regs) {
    asm volatile("sti");

    uint32_t eax_val = regs->eax;
    uint32_t ebx_val = regs->ebx;
    uint32_t ecx_val = regs->ecx;
    uint32_t edx_val = regs->edx;

    switch(eax_val) {
        case NEODECK_SYS_PRINT: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            kprint((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_CLEAR: {
            terminal_clear();
            break;
        }
        case NEODECK_SYS_GET_KEY: {
            /* Only the foreground task owns the keyboard; the others poll "no key". */
            if (!task_is_foreground()) { regs->eax = 0; break; }
            regs->eax = (uint32_t)(uint8_t)keyboard_get_char();   /* blocks; other tasks run */
            break;
        }
        case NEODECK_SYS_READ_FILE: { // EBX=path, ECX=buffer, EDX=buffer size
            if (!ustr_ok(ebx_val) || edx_val == 0 || !uptr_ok(ecx_val, edx_val)) { regs->eax = 0; break; }
            regs->eax = fs_read_to_buffer((const char*)ebx_val, (char*)ecx_val, edx_val);
            break;
        }
        case NEODECK_SYS_GET_DIR_ENTRIES: { // EBX=neodeck_dirent[], ECX=max entries
            if (ecx_val > APP_MEM_SIZE / sizeof(neodeck_dirent) ||
                !uptr_ok(ebx_val, ecx_val * sizeof(neodeck_dirent))) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_get_dir_entries((void*)ebx_val, ecx_val);
            break;
        }
        case NEODECK_SYS_MKDIR: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_mkdir((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_CD: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_cd((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_TOUCH: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_touch((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_WRITE_FILE: {
            if (!ustr_ok(ebx_val) || !ustr_ok(ecx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_write((const char*)ebx_val, (const char*)ecx_val);
            break;
        }
        case NEODECK_SYS_CAT: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            fs_cat((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_GET_CWD: { // EBX=buffer, ECX=size
            if (ecx_val == 0 || !uptr_ok(ebx_val, ecx_val)) { regs->eax = 0; break; }
            const char* cwd = fs_cwd();
            char* dst = (char*)ebx_val;
            uint32_t n = 0;
            while (cwd[n] && n + 1 < ecx_val) { dst[n] = cwd[n]; n++; }
            dst[n] = '\0';
            regs->eax = n;
            break;
        }
        case NEODECK_SYS_FORMAT: {
            fs_format();
            break;
        }
        case NEODECK_SYS_REMOVE: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_remove_quiet((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_RMDIR: {
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_rmdir_quiet((const char*)ebx_val);
            break;
        }
        case NEODECK_SYS_STAT: {
            if (!ustr_ok(ebx_val) || !uptr_ok(ecx_val, sizeof(neodeck_stat))) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)fs_stat((const char*)ebx_val, (void*)ecx_val);
            break;
        }
        case NEODECK_SYS_EXIT: {
            task_exit((int)ebx_val);        // never returns; the next task runs
        }
        case NEODECK_SYS_SLEEP: {
            timer_sleep_ms(ebx_val);        // blocks only this task
            break;
        }
        case NEODECK_SYS_UPTIME_MS: {
            regs->eax = timer_uptime_ms();
            break;
        }
        case NEODECK_SYS_YIELD: {
            task_yield();
            break;
        }
        case NEODECK_SYS_GETPID: {
            regs->eax = task_current_pid();
            break;
        }
        case NEODECK_SYS_SPAWN: {           // EBX="file args...", ECX=flags
            if (!ustr_ok(ebx_val)) { regs->eax = SYSCALL_ERROR; break; }
            regs->eax = (uint32_t)exec_spawn((const char*)ebx_val, (ecx_val & NEODECK_SPAWN_DETACH) != 0);
            break;
        }
        case NEODECK_SYS_WAIT: {            // EBX=pid, ECX=int* status or 0
            if (ecx_val != 0 && !uptr_ok(ecx_val, sizeof(int))) { regs->eax = SYSCALL_ERROR; break; }
            int status = 0;
            int r = task_wait((int)ebx_val, &status, false);
            if (r == 0 && ecx_val != 0) *(int*)ecx_val = status;
            regs->eax = (uint32_t)r;
            break;
        }
        case NEODECK_SYS_KILL: {
            regs->eax = (uint32_t)task_kill((int)ebx_val);   // killing yourself does not return D:
            break;
        }
        default:
            kprint("NeoDeck Warning: Unknown Syscall ID read.\n");
            regs->eax = SYSCALL_ERROR;
            break;
    }
    asm volatile("cli");
}

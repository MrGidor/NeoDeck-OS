#ifndef TASK_H
#define TASK_H

#include "../include/types.h"
#include "../include/layout.h"

/*
 * Tasks and the scheduler.
 *
 *   pid 0      idle      boot context, kernel stack from .bss, never blocks
 *   kernel threads       ring 0, kernel page directory (the shell is one)
 *   user tasks           ring 3, own page directory + own RAM at APP_VIRT_BASE
 *
 * Scheduling is round-robin. A task is switched out
 *   - voluntarily: yield / sleep / wait / waiting for a key / exit, or
 *   - by the PIT (IRQ0) after SCHED_SLICE_TICKS, but ONLY when the timer
 *     interrupted ring-3 code. Kernel code (syscalls, the shell, the
 *     filesystem driver) is never preempted, so none of it needs locking.
 * Blocked tasks are re-checked by the scheduler each time it picks a task
 * (polling), which keeps wake-ups free of lost-wake-up races.
 */

#define TASK_NAME_MAX 16

enum task_state_t { TASK_DEAD = 0, TASK_READY, TASK_RUNNING, TASK_BLOCKED };
enum task_wait_t  { WAIT_NONE = 0, WAIT_SLEEP, WAIT_PID, WAIT_KEY };

struct task_t {
    uint32_t     pid;
    bool         used;          /* slot taken. DEAD + used = zombie awaiting task_wait() */
    task_state_t state;
    char         name[TASK_NAME_MAX];
    bool         is_user;
    bool         detached;
    bool         needs_free;

    uint32_t     esp;
    uint32_t     kstack_base;
    uint32_t     kstack_top;
    uint32_t     page_dir;
    uint32_t     page_table;
    uint32_t     mem_base;
    void       (*entry)();

    task_wait_t  wait;
    uint32_t     wake_tick;
    uint32_t     wait_pid;
    bool         interruptible;

    int          exit_status;
    uint32_t     parent_pid;
    uint32_t     cpu_ticks;
};

/* Exit status of a task killed by a CPU exception = TASK_FAULT_BASE + vector */
#define TASK_FAULT_BASE   1000
#define TASK_KILLED       (-1)

void     task_init();
int      task_create_kthread(const char* name, void (*entry)());

/* Create a ring-3 task from a flat binary (entry = first byte), copying `image`
 * into freshly allocated private RAM. Returns pid, or:
 *   -1 no free task slot   -2 out of memory   -3 image/arguments too big */
int      task_create_user(const char* name, const uint8_t* image, uint32_t image_size,
                          int argc, char** argv, bool detached);

void     task_yield();
void     task_exit(int status) __attribute__((noreturn));
void     task_sleep_ticks(uint32_t ticks);
void     task_block_on_key();
int      task_wait(int pid, int* status, bool interruptible);   /* 0 ok, -1 not waitable, -2 interrupted */
int      task_kill(int pid);                            /* 0 ok, -1 no such task, -2 not killable */
void     task_timer_tick(bool from_user);

const task_t* task_find(uint32_t pid);
bool     task_scheduler_active();
uint32_t task_current_pid();
bool     task_current_is_user();
const char* task_current_name();
bool     task_is_foreground();
void     task_set_shell(uint32_t pid);
void     task_set_foreground(uint32_t pid);
void     task_reset_foreground();
void     task_print_list();

void     task_handle_user_fault(uint32_t vector, uint32_t eip, uint32_t cr2, uint32_t error_code)
             __attribute__((noreturn));

#endif

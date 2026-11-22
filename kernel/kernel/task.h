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
    bool         detached;      /* nobody will wait for it: slot is freed as soon as it dies */
    bool         needs_free;    /* DEAD but kernel stack / memory not released yet */

    uint32_t     esp;           /* saved kernel ESP while not running */
    uint32_t     kstack_base;   /* heap block (0 for the idle/boot context) */
    uint32_t     kstack_top;
    uint32_t     page_dir;      /* physical address for CR3 */
    uint32_t     page_table;    /* user tasks: private page table of the app window */
    uint32_t     mem_base;      /* user tasks: physical base of the app's RAM */
    void       (*entry)();      /* kernel threads */

    task_wait_t  wait;
    uint32_t     wake_tick;     /* WAIT_SLEEP */
    uint32_t     wait_pid;      /* WAIT_PID */
    bool         interruptible; /* WAIT_PID can be broken by Ctrl+C */

    int          exit_status;
    uint32_t     parent_pid;
    uint32_t     cpu_ticks;     /* timer ticks spent running */
};

/* Exit status of a task killed by a CPU exception = TASK_FAULT_BASE + vector */
#define TASK_FAULT_BASE   1000
#define TASK_KILLED       (-1)

/* ---- setup ---- */
void     task_init();                                   /* adopt the boot context as pid 0 */
int      task_create_kthread(const char* name, void (*entry)());   /* pid, or negative */

/* Create a ring-3 task from a flat binary (entry = first byte), copying `image`
 * into freshly allocated private RAM. Returns pid, or:
 *   -1 no free task slot   -2 out of memory   -3 image/arguments too big */
int      task_create_user(const char* name, const uint8_t* image, uint32_t image_size,
                          int argc, char** argv, bool detached);

/* ---- scheduling ---- */
void     task_yield();
void     task_exit(int status) __attribute__((noreturn));
void     task_sleep_ticks(uint32_t ticks);
void     task_block_on_key();                           /* until a key is available AND we are foreground */
int      task_wait(int pid, int* status, bool interruptible);   /* 0 ok, -1 not waitable, -2 interrupted */
int      task_kill(int pid);                            /* 0 ok, -1 no such task, -2 not killable */
void     task_timer_tick(bool from_user);               /* called from IRQ0 after the EOI */

/* ---- queries ---- */
const task_t* task_find(uint32_t pid);                 /* read-only view of a task (ps, tests) */
bool     task_scheduler_active();
uint32_t task_current_pid();
bool     task_current_is_user();
const char* task_current_name();
bool     task_is_foreground();                          /* may the caller read the keyboard? */
void     task_set_shell(uint32_t pid);                  /* the shell owns the keyboard by default */
void     task_set_foreground(uint32_t pid);
void     task_reset_foreground();
void     task_print_list();                             /* `ps` */

/* A ring-3 task raised a CPU exception: report it and terminate the task. */
void     task_handle_user_fault(uint32_t vector, uint32_t eip, uint32_t cr2, uint32_t error_code)
             __attribute__((noreturn));

#endif

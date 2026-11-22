#include "../include/types.h"
#include "../include/layout.h"
#include "vista_abi.h"
#include "task.h"
#include "task_arch.h"
#include "gdt.h"
#include "paging.h"
#include "../memory/memory.h"
#include "../drivers/terminal.h"
#include "../drivers/timer.h"
#include "../drivers/keyboard.h"

#define KSTACK_BLOCKS    (TASK_KSTACK_SIZE / BLOCK_SIZE)
#define APP_MEM_BLOCKS   (APP_MEM_SIZE / BLOCK_SIZE)
#define EFLAGS_RESERVED  0x00000002u
#define EFLAGS_IF        0x00000200u
#define MAX_TASK_ARGS    32
#define TRAMPOLINE_SIZE  16

static task_t   tasks[TASK_MAX];
static task_t*  current       = 0;
static uint32_t next_pid      = 1;
static uint32_t shell_pid     = 0;
static uint32_t foreground_pid = 0;
static int      slice_left    = 0;
static bool     sched_active  = false;

static uint32_t str_len(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void str_copy(char* dst, const char* src, uint32_t max) {
    uint32_t i = 0;
    for (; i + 1 < max && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void task_clear(task_t* t) {
    volatile uint8_t* p = (volatile uint8_t*)t;
    for (uint32_t i = 0; i < sizeof(task_t); i++) p[i] = 0;
}

static void print_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 8; i++) b[2 + i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
    b[10] = '\0';
    kprint(b);
}

static task_t* find_task(uint32_t pid) {
    for (int i = 0; i < TASK_MAX; i++)
        if (tasks[i].used && tasks[i].pid == pid) return &tasks[i];
    return 0;
}

static task_t* alloc_slot() {
    for (int i = 1; i < TASK_MAX; i++) {
        if (!tasks[i].used) {
            task_clear(&tasks[i]);
            tasks[i].used = true;                 /* reserved; callers undo on failure */
            return &tasks[i];
        }
    }
    return 0;
}

/* The frame task_switch() expects: pop order eflags, edi, esi, ebx, ebp, ret. */
static uint32_t* push_switch_frame(uint32_t* sp, uint32_t return_address) {
    *--sp = return_address;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = EFLAGS_RESERVED;         /* eflags, interrupts off until the task starts itself */
    return sp;
}

static task_t* wait_target(const task_t* t) { return find_task(t->wait_pid); }

static bool wake_ready(const task_t* t) {
    switch (t->wait) {
        case WAIT_SLEEP:
            return (int32_t)(timer_ticks() - t->wake_tick) >= 0;
        case WAIT_PID: {
            task_t* w = wait_target(t);
            if (!w || w->state == TASK_DEAD) return true;
            return t->interruptible && keyboard_ctrl_c_pending();
        }
        case WAIT_KEY:
            return keyboard_has_chars() && foreground_pid == t->pid;
        default:
            return true;
    }
}

static task_t* pick_next() {
    int start = (int)(current - tasks);
    for (int n = 1; n <= TASK_MAX; n++) {
        task_t* t = &tasks[(start + n) % TASK_MAX];
        if (!t->used || t == &tasks[0]) continue;
        if (t->state == TASK_BLOCKED && wake_ready(t)) t->state = TASK_READY;
        if (t->state == TASK_READY || t->state == TASK_RUNNING) return t;
    }
    return &tasks[0];
}

static void free_task_resources(task_t* t) {
    if (t->is_user) {
        paging_destroy_user_space(t->page_dir, t->page_table);
        if (t->mem_base) kfree((void*)t->mem_base, APP_MEM_BLOCKS);
    }
    if (t->kstack_base) kfree((void*)t->kstack_base, KSTACK_BLOCKS);
    t->kstack_base = 0;
    t->mem_base = 0;
    t->page_table = 0;
}

/* Releases everything a dead task owned. Never touches the running task (we
 * may be standing on its kernel stack), so a task's last act is to switch away
 * and the next task to run does the cleanup. */
static void reap_dead_tasks() {
    for (int i = 1; i < TASK_MAX; i++) {
        task_t* t = &tasks[i];
        if (!t->used || t->state != TASK_DEAD || !t->needs_free || t == current) continue;
        free_task_resources(t);
        t->needs_free = false;
        if (t->detached) t->used = false;
    }
}

extern "C" void task_after_switch() { reap_dead_tasks(); }

static void switch_to(task_t* prev, task_t* next) {
    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    next->wait  = WAIT_NONE;
    current     = next;
    slice_left  = SCHED_SLICE_TICKS;

    if (next->is_user) arch_set_kernel_stack(next->kstack_top);
    if (next->page_dir != prev->page_dir) arch_load_cr3(next->page_dir);

    task_switch(&prev->esp, next->esp);
    /* We are running again: somebody switched back to `prev`. */
}

static void schedule() {
    uint32_t flags = arch_irq_save();
    task_t* prev = current;
    task_t* next = pick_next();
    if (next != prev) {
        switch_to(prev, next);
    } else {
        prev->state = TASK_RUNNING;
        prev->wait  = WAIT_NONE;
    }
    reap_dead_tasks();
    arch_irq_restore(flags);
}

void task_yield() {
    if (!sched_active) return;
    schedule();
}

void task_timer_tick(bool from_user) {
    if (!sched_active) return;
    current->cpu_ticks++;
    if (slice_left > 0) slice_left--;
    /* Preempt only ring-3 code: kernel code is not re-entrant. */
    if (slice_left == 0 && from_user) schedule();
}

void task_sleep_ticks(uint32_t ticks) {
    if (!sched_active || current == &tasks[0]) {
        uint32_t start = timer_ticks();
        while ((int32_t)(timer_ticks() - start) < (int32_t)ticks) arch_halt();
        return;
    }
    current->wake_tick = timer_ticks() + ticks;
    current->wait  = WAIT_SLEEP;
    current->state = TASK_BLOCKED;
    schedule();
}

void task_block_on_key() {
    if (!sched_active || current == &tasks[0]) { arch_halt(); return; }
    current->wait  = WAIT_KEY;
    current->state = TASK_BLOCKED;
    schedule();
}

void task_init() {
    for (int i = 0; i < TASK_MAX; i++) task_clear(&tasks[i]);
    task_t* idle = &tasks[0];
    idle->used     = true;
    idle->pid      = 0;
    idle->state    = TASK_RUNNING;
    idle->page_dir = paging_kernel_directory();
    str_copy(idle->name, "idle", TASK_NAME_MAX);
    current       = idle;
    slice_left    = SCHED_SLICE_TICKS;
    sched_active  = true;
}

int task_create_kthread(const char* name, void (*entry)()) {
    task_t* t = alloc_slot();
    if (!t) return -1;
    uint8_t* kstack = (uint8_t*)kmalloc(KSTACK_BLOCKS);
    if (!kstack) { t->used = false; return -2; }

    t->pid         = next_pid++;
    t->parent_pid  = current ? current->pid : 0;
    t->is_user     = false;
    t->detached    = true;
    t->kstack_base = (uint32_t)kstack;
    t->kstack_top  = t->kstack_base + TASK_KSTACK_SIZE;
    t->page_dir    = paging_kernel_directory();
    t->entry       = entry;
    str_copy(t->name, name, TASK_NAME_MAX);

    t->esp   = (uint32_t)push_switch_frame((uint32_t*)t->kstack_top, (uint32_t)task_kthread_entry_stub);
    t->state = TASK_READY;
    return (int)t->pid;
}

extern "C" void task_kthread_main() {
    arch_irq_enable();
    current->entry();
    task_exit(0);
}

static const uint8_t exit_trampoline[] = {
    0x89, 0xC3,                                                  /* mov ebx, eax (main's return value) */
    0xB8, VISTA_SYS_EXIT & 0xFF, (VISTA_SYS_EXIT >> 8) & 0xFF, 0, 0,   /* mov eax, SYS_EXIT */
    0xCD, 0x80,                                                  /* int 0x80 */
    0xEB, 0xFE                                                   /* jmp $ (never reached) */
};
static_assert(sizeof(exit_trampoline) <= TRAMPOLINE_SIZE, "trampoline does not fit its slot");

int task_create_user(const char* name, const uint8_t* image, uint32_t image_size,
                     int argc, char** argv, bool detached) {
    if (image_size == 0 || image_size > APP_MEM_SIZE - APP_STACK_SIZE) return -3;
    if (argc < 0 || argc > MAX_TASK_ARGS) return -3;

    uint32_t str_bytes = 0;
    for (int i = 0; i < argc; i++) str_bytes += str_len(argv[i]) + 1;
    if (TRAMPOLINE_SIZE + str_bytes + 4 + 4u * (argc + 1) + 12 > APP_STACK_SIZE / 2) return -3;

    task_t* t = alloc_slot();
    if (!t) return -1;

    uint8_t* mem    = (uint8_t*)kmalloc_aligned(APP_MEM_BLOCKS, 4096 / BLOCK_SIZE);
    uint8_t* kstack = (uint8_t*)kmalloc(KSTACK_BLOCKS);
    uint32_t pd = 0, pt = 0;
    if (!mem || !kstack || !paging_create_user_space((uint32_t)mem, &pd, &pt)) {
        if (mem)    kfree(mem, APP_MEM_BLOCKS);
        if (kstack) kfree(kstack, KSTACK_BLOCKS);
        t->used = false;
        return -2;
    }

    /* Private RAM: zero it all (this is the app's .bss and stack too), then
     * lay out  [ image ........ free ........ | argv strings, argv[], frame ]  */
    uint32_t* words = (uint32_t*)mem;
    for (uint32_t i = 0; i < APP_MEM_SIZE / 4; i++) words[i] = 0;
    for (uint32_t i = 0; i < image_size; i++) mem[i] = image[i];

    uint32_t top = APP_MEM_SIZE;
    top -= TRAMPOLINE_SIZE;
    uint32_t tramp_off = top;
    for (uint32_t i = 0; i < sizeof(exit_trampoline); i++) mem[tramp_off + i] = exit_trampoline[i];

    uint32_t str_va[MAX_TASK_ARGS];
    for (int i = argc - 1; i >= 0; i--) {
        uint32_t len = str_len(argv[i]) + 1;
        top -= len;
        for (uint32_t k = 0; k < len; k++) mem[top + k] = (uint8_t)argv[i][k];
        str_va[i] = APP_VIRT_BASE + top;
    }
    top &= ~3u;

    top -= 4u * (argc + 1);
    uint32_t argv_off = top;
    for (int i = 0; i < argc; i++) *(uint32_t*)(mem + argv_off + 4u * i) = str_va[i];
    *(uint32_t*)(mem + argv_off + 4u * argc) = 0;

    /* What main(int argc, char** argv) expects on entry: [ret][argc][argv] */
    top -= 12;
    *(uint32_t*)(mem + top + 0) = APP_VIRT_BASE + tramp_off;
    *(uint32_t*)(mem + top + 4) = (uint32_t)argc;
    *(uint32_t*)(mem + top + 8) = APP_VIRT_BASE + argv_off;
    uint32_t user_esp = APP_VIRT_BASE + top;

    /* Kernel stack of the new task: the switch frame, whose return address is
     * the user entry stub, which then irets through this ring-3 frame. */
    uint32_t* sp = (uint32_t*)(kstack + TASK_KSTACK_SIZE);
    *--sp = SEL_USER_DATA;
    *--sp = user_esp;
    *--sp = EFLAGS_RESERVED | EFLAGS_IF;
    *--sp = SEL_USER_CODE;
    *--sp = APP_VIRT_BASE;
    sp = push_switch_frame(sp + 0, (uint32_t)task_user_entry_stub);

    t->pid         = next_pid++;
    t->parent_pid  = current ? current->pid : 0;
    t->is_user     = true;
    t->detached    = detached;
    t->kstack_base = (uint32_t)kstack;
    t->kstack_top  = (uint32_t)kstack + TASK_KSTACK_SIZE;
    t->page_dir    = pd;
    t->page_table  = pt;
    t->mem_base    = (uint32_t)mem;
    t->esp         = (uint32_t)sp;
    str_copy(t->name, name, TASK_NAME_MAX);
    t->state       = TASK_READY;
    return (int)t->pid;
}

/* Children of a dying task can no longer be waited for. */
static void release_children(uint32_t pid) {
    for (int i = 1; i < TASK_MAX; i++) {
        task_t* c = &tasks[i];
        if (!c->used || c->parent_pid != pid) continue;
        c->detached = true;
        if (c->state == TASK_DEAD && !c->needs_free) c->used = false;
    }
}

static void announce_exit(const task_t* t) {
    kprint("\n[pid ");
    kprint_dec(t->pid);
    kprint("] ");
    kprint(t->name);
    kprint(" exited, status ");
    if (t->exit_status < 0) { kprint("-"); kprint_dec((uint32_t)(-t->exit_status)); }
    else kprint_dec((uint32_t)t->exit_status);
    kprint("\n");
}

void task_exit(int status) {
    arch_irq_save();                                  /* never restored: this task is finished */
    task_t* t = current;

    t->exit_status = status;
    t->state       = TASK_DEAD;
    t->needs_free  = true;
    release_children(t->pid);
    if (foreground_pid == t->pid) foreground_pid = shell_pid;
    if (t->detached && t->is_user) announce_exit(t);

    schedule();
    for (;;) arch_halt();
}

int task_kill(int pid) {
    task_t* t = find_task((uint32_t)pid);
    if (!t || t->state == TASK_DEAD) return -1;
    if (!t->is_user) return -2;

    if (t == current) task_exit(TASK_KILLED);

    uint32_t flags = arch_irq_save();
    t->exit_status = TASK_KILLED;
    t->state       = TASK_DEAD;
    t->needs_free  = true;
    release_children(t->pid);
    if (foreground_pid == t->pid) foreground_pid = shell_pid;
    reap_dead_tasks();
    arch_irq_restore(flags);
    return 0;
}

int task_wait(int pid, int* status, bool interruptible) {
    task_t* t = find_task((uint32_t)pid);
    if (!t || t->detached || t == current) return -1;

    while (t->state != TASK_DEAD) {
        if (interruptible && keyboard_ctrl_c_pending()) return -2;
        current->wait          = WAIT_PID;
        current->wait_pid      = (uint32_t)pid;
        current->interruptible = interruptible;
        current->state         = TASK_BLOCKED;
        schedule();
    }

    reap_dead_tasks();                                /* make sure its memory is gone before the slot is reused */
    if (status) *status = t->exit_status;
    t->used = false;
    return 0;
}

void task_handle_user_fault(uint32_t vector, uint32_t eip, uint32_t cr2, uint32_t error_code) {
    kprint_error("\n[pid ");
    kprint_dec(current->pid);
    kprint_error("] ");
    kprint_error(current->name);
    kprint_error(" crashed: CPU exception ");
    kprint_dec(vector);
    kprint_error(" at EIP=");  print_hex(eip);
    if (vector == 14) { kprint_error(", address "); print_hex(cr2); }
    kprint_error(", error code "); print_hex(error_code);
    kprint_error(". Task terminated.\n");
    task_exit((int)(TASK_FAULT_BASE + vector));
}

const task_t* task_find(uint32_t pid) { return find_task(pid); }
bool        task_scheduler_active() { return sched_active; }
uint32_t    task_current_pid()      { return current ? current->pid : 0; }
bool        task_current_is_user()  { return current && current->is_user; }
const char* task_current_name()     { return current ? current->name : "?"; }

bool task_is_foreground() { return current && current->pid == foreground_pid; }
void task_set_shell(uint32_t pid) { shell_pid = pid; foreground_pid = pid; }
void task_set_foreground(uint32_t pid) { foreground_pid = pid; }
void task_reset_foreground() { foreground_pid = shell_pid; }

static void pad_to(uint32_t used, uint32_t width) {
    while (used++ < width) kprint(" ");
}

static uint32_t dec_digits(uint32_t v) { uint32_t n = 1; while (v >= 10) { v /= 10; n++; } return n; }

void task_print_list() {
    kprint_info("  PID  STATE    TYPE   NAME             PHYS RAM    CPU(ticks)\n");
    for (int i = 0; i < TASK_MAX; i++) {
        const task_t* t = &tasks[i];
        if (!t->used) continue;

        kprint("  ");
        kprint_dec(t->pid);
        pad_to(dec_digits(t->pid), 5);

        const char* st = "READY";
        if (t->state == TASK_RUNNING) st = "RUNNING";
        else if (t->state == TASK_BLOCKED) st = t->wait == WAIT_SLEEP ? "SLEEP" : t->wait == WAIT_KEY ? "KEY" : "WAIT";
        else if (t->state == TASK_DEAD) st = "ZOMBIE";
        kprint(st);
        pad_to(str_len(st), 9);

        const char* kind = (i == 0) ? "idle" : t->is_user ? "user" : "kernel";
        kprint(kind);
        pad_to(str_len(kind), 7);

        kprint(t->name);
        pad_to(str_len(t->name), 17);

        if (t->is_user && t->mem_base) print_hex(t->mem_base); else kprint("-         ");
        kprint("  ");
        kprint_dec(t->cpu_ticks);
        kprint("\n");
    }
}

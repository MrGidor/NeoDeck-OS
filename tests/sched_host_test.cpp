
#include "../kernel/include/types.h"
#include "../kernel/include/layout.h"
#include "../kernel/kernel/task.h"
#include "../kernel/kernel/task_arch.h"
#include "../kernel/kernel/paging.h"
#include "../kernel/memory/memory.h"
#include "neodeck_abi.h"

static inline int sys3(int n, int a, int b, int c) {
    int r; asm volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r;
}
static void out(const char* s) { int n = 0; while (s[n]) n++; sys3(4, 1, (int)s, n); }
static void out_dec(long v) {
    char b[24]; int i = 23; b[i] = 0; bool neg = v < 0; unsigned long u = neg ? -v : v;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (neg) b[--i] = '-';
    out(&b[i]);
}
extern "C" void* memset(void* d, int c, unsigned n) { unsigned char* p = (unsigned char*)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void* memcpy(void* d, const void* s, unsigned n) { unsigned char* p = (unsigned char*)d; const unsigned char* q = (const unsigned char*)s; while (n--) *p++ = *q++; return d; }

static void map_fixed(uint32_t addr, uint32_t len) {
    unsigned args[6] = { addr, len, 3 /*RW*/, 0x32 /*PRIVATE|FIXED|ANON*/, (unsigned)-1, 0 };
    int r = sys3(90, (int)args, 0, 0);               // old_mmap
    if (r != (int)addr) { out("FATAL: mmap failed\n"); sys3(252, 3, 0, 0); }
}

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; out("  FAIL line "); out_dec(__LINE__); out(": " #c "\n"); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); if (_a == _b) g_pass++; else { g_fail++; out("  FAIL line "); out_dec(__LINE__); out(": " #a " == " #b "  ("); out_dec(_a); out(" vs "); out_dec(_b); out(")\n"); } } while (0)

static uint32_t g_ticks = 0;
static bool     g_has_key = false, g_ctrl_c = false;
static uint32_t g_cr3_loads = 0, g_last_cr3 = 0, g_last_esp0 = 0;
static bool     g_quiet = true;

void terminal_clear() {}
void terminal_scroll() {}
void terminal_put_char(char) {}
void kprint(const char* s) { if (!g_quiet) out(s); }
void kprint_log(const char* s, bool) { kprint(s); }
void kprint_color(const char* s, uint8_t) { kprint(s); }
void kprint_success(const char* s) { kprint(s); }
void kprint_warning(const char* s) { kprint(s); }
void kprint_error(const char* s) { kprint(s); }
void kprint_info(const char* s) { kprint(s); }
void kprint_dec(uint32_t v) { if (!g_quiet) out_dec(v); }
uint32_t timer_ticks() { return g_ticks; }
bool keyboard_has_chars() { return g_has_key; }
bool keyboard_ctrl_c_pending() { return g_ctrl_c; }

extern "C" {
uint32_t arch_irq_save() { return 0x202; }
void arch_irq_restore(uint32_t) {}
void arch_irq_enable() {}
void arch_halt() { g_ticks++; }
void arch_load_cr3(uint32_t pd) { g_cr3_loads++; g_last_cr3 = pd; }
void arch_set_kernel_stack(uint32_t esp0) { g_last_esp0 = esp0; }
void test_user_main();
}

// "ring 3" entry for user tasks: run test_user_main() on the task's kernel stack.
// (The real stub loads segment registers and irets; that cannot be done from a Linux process.)
asm(
    ".pushsection .text\n"
    ".global task_user_entry_stub\n"
    "task_user_entry_stub:\n"
    "    call task_after_switch\n"
    "    call test_user_main\n"
    "    ud2\n"
    ".global task_kthread_entry_stub\n"
    "task_kthread_entry_stub:\n"
    "    call task_after_switch\n"
    "    call task_kthread_main\n"
    "    ud2\n"
    ".popsection\n"
);

//helpers
static uint32_t used_blocks() {
    uint32_t n = 0;
    for (int i = 0; i < allocation_bitmap_size; i++) n += get_bitmap(allocation_bitmap, i);
    return n;
}

// Software MMU: translate a virtual address through a task's page directory.
static bool v2p(uint32_t pd, uint32_t va, uint32_t* pa, uint32_t* flags) {
    uint32_t pde = ((uint32_t*)pd)[va >> 22];
    if (!(pde & 1)) return false;
    uint32_t pte = ((uint32_t*)(pde & ~0xFFFu))[(va >> 12) & 0x3FF];
    if (!(pte & 1)) return false;
    *pa = (pte & ~0xFFFu) | (va & 0xFFF);
    if (flags) *flags = pde & pte;                   // effective permissions need both levels
    return true;
}
static uint32_t rd32v(uint32_t pd, uint32_t va) { uint32_t pa; v2p(pd, va, &pa, 0); return *(uint32_t*)pa; }
static uint8_t  rd8v(uint32_t pd, uint32_t va)  { uint32_t pa; v2p(pd, va, &pa, 0); return *(uint8_t*)pa; }

static bool streq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

// What the simulated apps do, chosen by image[0]; image[1] is a parameter.
enum { P_BUSY = 'B', P_SLEEP = 'S', P_EXIT = 'X', P_FAULT = 'F', P_LOOP = 'L', P_KEY = 'K', P_SPAWNER = 'P' };

struct record {
    bool seen; int argc; char arg[4][24]; bool layout_ok; bool bss_zero; bool trampoline_ok;
    uint32_t wake_before, wake_after; uint32_t cr3_seen, esp0_seen; uint32_t mem_phys;
};
static record g_rec[TASK_MAX + 8];
static char   g_log[512]; static int g_log_n = 0;
static void   log_char(char c) { if (g_log_n < (int)sizeof(g_log) - 1) g_log[g_log_n++] = c; g_log[g_log_n] = 0; }
static uint8_t g_child_image[] = { P_EXIT, 7 };

extern "C" void test_user_main() {
    uint32_t pid = task_current_pid();
    const task_t* t = task_find(pid);
    record* r = &g_rec[pid % (TASK_MAX + 8)];
    r->seen = true;
    r->cr3_seen = g_last_cr3; r->esp0_seen = g_last_esp0; r->mem_phys = t->mem_base;


    uint32_t* frame = (uint32_t*)(t->kstack_top - 5 * 4);
    uint32_t eip = frame[0], cs = frame[1], eflags = frame[2], uesp = frame[3], ss = frame[4];
    r->layout_ok = (eip == APP_VIRT_BASE && cs == 0x1B && ss == 0x23 && (eflags & 0x200) && !(eflags & 0x3000));
    uint32_t ret = rd32v(t->page_dir, uesp), argc = rd32v(t->page_dir, uesp + 4), argv = rd32v(t->page_dir, uesp + 8);
    r->argc = (int)argc;
    for (uint32_t i = 0; i < argc && i < 4; i++) {
        uint32_t s = rd32v(t->page_dir, argv + 4 * i);
        for (int k = 0; k < 23; k++) { char c = (char)rd8v(t->page_dir, s + k); r->arg[i][k] = c; r->arg[i][k + 1] = 0; if (!c) break; }
    }
    r->layout_ok = r->layout_ok && rd32v(t->page_dir, argv + 4 * argc) == 0 && ret >= APP_VIRT_BASE && ret < APP_VIRT_BASE + APP_MEM_SIZE;
    r->trampoline_ok = rd8v(t->page_dir, ret) == 0x89 && rd8v(t->page_dir, ret + 1) == 0xC3 && rd8v(t->page_dir, ret + 2) == 0xB8 &&
                       rd8v(t->page_dir, ret + 3) == NEODECK_SYS_EXIT && rd8v(t->page_dir, ret + 7) == 0xCD && rd8v(t->page_dir, ret + 8) == 0x80;
    r->bss_zero = true;                                  // everything between the image and the stack must be zero
    for (uint32_t off = 64; off < 4096; off++) if (rd8v(t->page_dir, APP_VIRT_BASE + off) != 0) r->bss_zero = false;

    uint8_t kind = rd8v(t->page_dir, APP_VIRT_BASE), param = rd8v(t->page_dir, APP_VIRT_BASE + 1);
    switch (kind) {
    case P_BUSY: 
        for (int i = 0; i < 20; i++) { log_char((char)('A' + param)); g_ticks++; task_timer_tick(true); }
        task_exit(param);
    case P_SLEEP:
        r->wake_before = g_ticks; task_sleep_ticks(param); r->wake_after = g_ticks;
        task_exit(0);
    case P_EXIT:   task_exit(param);
    case P_FAULT:  task_handle_user_fault(14, APP_VIRT_BASE + 0x123, 0xDEAD0000, 6);
    case P_LOOP:   for (;;) task_yield();
    case P_KEY:
        while (!(g_has_key && task_is_foreground())) task_block_on_key();
        log_char('k'); g_has_key = false;
        task_exit(42);
    case P_SPAWNER: {  
        char* av[] = { (char*)"child", 0 };
        int c = task_create_user("child", g_child_image, sizeof g_child_image, 1, av, false);
        task_exit(c);
    }
    }
    task_exit(-99);
}

static uint8_t img_busyA[] = { P_BUSY, 0 }, img_busyB[] = { P_BUSY, 1 };
static uint8_t img_sleep5[] = { P_SLEEP, 5 }, img_sleep1[] = { P_SLEEP, 1 };
static uint8_t img_exit3[] = { P_EXIT, 3 }, img_fault[] = { P_FAULT, 0 }, img_loop[] = { P_LOOP, 0 };
static uint8_t img_key[] = { P_KEY, 0 }, img_spawner[] = { P_SPAWNER, 0 };

static int spawn(const char* name, uint8_t* img, uint32_t n, bool detached, int argc = 1, char** argv = 0) {
    static char* dflt[] = { (char*)"prog", 0 };
    return task_create_user(name, img, n, argc, argv ? argv : dflt, detached);
}


static volatile bool g_done = false;

static void test_address_spaces() {
    out("[1] per-task address spaces\n");
    uint32_t base_blocks = used_blocks();
    char* av[] = { (char*)"alpha", (char*)"--flag", (char*)"third arg" };
    int a = spawn("alpha", img_exit3, 2, false, 3, av);
    int b = spawn("beta", img_exit3, 2, false);
    CHECK(a > 0 && b > 0 && a != b);
    const task_t* ta = task_find(a); const task_t* tb = task_find(b);
    CHECK(ta->page_dir != tb->page_dir && ta->mem_base != tb->mem_base);
    CHECK(ta->mem_base % 4096 == 0 && ta->page_dir % 4096 == 0 && ta->page_table % 4096 == 0);
    CHECK(ta->mem_base + APP_MEM_SIZE <= tb->mem_base || tb->mem_base + APP_MEM_SIZE <= ta->mem_base);   // disjoint RAM
    CHECK(ta->kstack_base != tb->kstack_base);
    uint32_t pa, pb, fa;
    CHECK(v2p(ta->page_dir, APP_VIRT_BASE, &pa, &fa) && v2p(tb->page_dir, APP_VIRT_BASE, &pb, 0));
    CHECK(pa == ta->mem_base && pb == tb->mem_base && pa != pb);
    CHECK((fa & 7) == 7);                                                                  // present|write|user
    CHECK(v2p(ta->page_dir, APP_VIRT_BASE + APP_MEM_SIZE - 4, &pa, 0) && pa == ta->mem_base + APP_MEM_SIZE - 4);
    CHECK(!v2p(ta->page_dir, APP_VIRT_BASE + APP_MEM_SIZE, &pa, 0));                       // just past the window
    CHECK(!v2p(ta->page_dir, 0x50000000, &pa, 0));
    uint32_t kf;
    CHECK(v2p(ta->page_dir, KERNEL_BASE, &pa, &kf) && pa == KERNEL_BASE && !(kf & 4));
    CHECK(v2p(ta->page_dir, 0xB8000, &pa, &kf) && pa == 0xB8000 && !(kf & 4));
    CHECK(v2p(ta->page_dir, ta->kstack_base, &pa, &kf) && pa == ta->kstack_base && !(kf & 4));
    for (int i = 0; i < PAGING_TABLE_COUNT; i++) CHECK(((uint32_t*)ta->page_dir)[i] == ((uint32_t*)paging_kernel_directory())[i]);
    CHECK(!(((uint32_t*)paging_kernel_directory())[APP_VIRT_BASE >> 22] & 1));            // kernel PD never maps the window

    int st = -1;
    CHECK_EQ(task_wait(a, &st, false), 0); CHECK_EQ(st, 3);
    CHECK_EQ(task_wait(b, &st, false), 0);
    record* r = &g_rec[a % (TASK_MAX + 8)];
    CHECK(r->seen && r->layout_ok && r->trampoline_ok && r->bss_zero);
    CHECK_EQ(r->argc, 3);
    CHECK(streq(r->arg[0], "alpha") && streq(r->arg[1], "--flag") && streq(r->arg[2], "third arg"));
    CHECK(r->cr3_seen == ta->page_dir && r->esp0_seen == ta->kstack_top);   // scheduler loaded this task's CR3 and TSS stack
    CHECK_EQ(used_blocks(), base_blocks);                                      // nothing leaked
    CHECK(!task_find(a) && !task_find(b));
}

static void test_limits() {
    out("[2] limits and failure paths\n");
    uint32_t base = used_blocks();
    static uint8_t big[APP_MEM_SIZE];
    CHECK_EQ(spawn("big", big, APP_MEM_SIZE - APP_STACK_SIZE + 1, false), -3);
    CHECK_EQ(spawn("empty", img_exit3, 0, false), -3);
    int pids[TASK_MAX + 2], n = 0;
    while (n < TASK_MAX + 2) { pids[n] = spawn("x", img_loop, 2, true); if (pids[n] < 0) break; n++; }
    CHECK_EQ(n, TASK_MAX - 2);                  // slot 0 = idle, one slot = this shell thread
    CHECK_EQ(pids[n], -1);
    for (int i = 0; i < n; i++) CHECK_EQ(task_kill(pids[i]), 0);
    CHECK_EQ(used_blocks(), base);
    // heap exhaustion: no slot or memory may leak when allocation fails halfway
    const int heap = allocation_bitmap_size;
    int left_mem_only = 1024 + 16;                            // app RAM fits, the kernel stack after it does not
    void* hog = kmalloc(heap - left_mem_only);
    CHECK(hog != 0);
    CHECK_EQ(spawn("nostack", img_exit3, 2, false), -2);      // mem allocated, then kstack fails -> mem must be returned
    kfree(hog, heap - left_mem_only);
    hog = kmalloc(heap - 900);                                // not even the app RAM fits
    CHECK(hog != 0);
    CHECK_EQ(spawn("nomem", img_exit3, 2, false), -2);
    kfree(hog, heap - 900);
    CHECK_EQ(used_blocks(), base);
    int q = spawn("again", img_exit3, 2, false);                // slot reusable after the failure
    CHECK(q > 0);
    int st; CHECK_EQ(task_wait(q, &st, false), 0);
    CHECK_EQ(used_blocks(), base);
}

static void test_preemption() {
    out("[3] round-robin preemption of CPU-bound ring-3 tasks\n");
    g_log_n = 0; g_log[0] = 0;
    int a = spawn("busyA", img_busyA, 2, false), b = spawn("busyB", img_busyB, 2, false);
    int st;
    CHECK_EQ(task_wait(a, &st, false), 0); CHECK_EQ(st, 0);
    CHECK_EQ(task_wait(b, &st, false), 0); CHECK_EQ(st, 1);
    int na = 0, nb = 0, switches = 0, max_run = 0, run = 0;
    for (int i = 0; i < g_log_n; i++) {
        if (g_log[i] == 'A') na++; else nb++;
        if (i && g_log[i] != g_log[i - 1]) { switches++; run = 0; }
        if (++run > max_run) max_run = run;
    }
    CHECK_EQ(na, 20); CHECK_EQ(nb, 20);
    CHECK(switches >= 8);                                      // they really interleaved
    CHECK(max_run <= SCHED_SLICE_TICKS + 1);                   // nobody hogged the CPU beyond its slice
    out("    interleaving: "); out(g_log); out("\n");
}

static void test_sleep_and_idle() {
    out("[4] sleeping tasks do not block others\n");
    int a = spawn("s5", img_sleep5, 2, false), b = spawn("s1", img_sleep1, 2, false);
    int st;
    CHECK_EQ(task_wait(b, &st, false), 0);
    uint32_t t_b_done = g_ticks;
    CHECK_EQ(task_wait(a, &st, false), 0);
    record* ra = &g_rec[a % (TASK_MAX + 8)];
    CHECK(ra->wake_after - ra->wake_before >= 5);
    CHECK(t_b_done <= g_ticks);
    task_sleep_ticks(3);                                       // the shell thread sleeping too
    CHECK(true);
}

static void test_exit_kill_fault() {
    out("[5] exit status, kill, CPU-exception handling, detached tasks\n");
    uint32_t base = used_blocks();
    int x = spawn("exit3", img_exit3, 2, false), st = 0;
    CHECK_EQ(task_wait(x, &st, false), 0); CHECK_EQ(st, 3);
    CHECK_EQ(task_wait(x, &st, false), -1);                    // already collected

    int f = spawn("fault", img_fault, 2, false), lp = spawn("loop", img_loop, 2, false);
    CHECK_EQ(task_wait(f, &st, false), 0); CHECK_EQ(st, TASK_FAULT_BASE + 14);
    CHECK(task_find(lp) && task_find(lp)->state != TASK_DEAD);  // the other task is unaffected
    CHECK_EQ(task_kill(lp), 0);
    CHECK_EQ(task_wait(lp, &st, false), 0); CHECK_EQ(st, TASK_KILLED);
    CHECK_EQ(task_kill(lp), -1);
    CHECK_EQ(task_kill(0), -2);                                 // idle is not killable
    CHECK_EQ(task_kill((int)task_current_pid()), -2);           // neither is a kernel thread (the "shell")
    CHECK_EQ(task_kill(9999), -1);

    int d = spawn("detached", img_exit3, 2, true);
    CHECK_EQ(task_wait(d, &st, false), -1);                    // detached tasks cannot be waited for
    while (task_find(d)) task_yield();                         // slot recycles by itself
    CHECK_EQ(used_blocks(), base);

    // a parent that dies leaves a zombie child: it must be cleaned up, not leaked
    int p = spawn("parent", img_spawner, 2, false);
    CHECK_EQ(task_wait(p, &st, false), 0);
    int child = st;
    CHECK(child > 0);
    for (int i = 0; i < 20 && task_find(child); i++) task_yield();
    CHECK(!task_find(child));
    CHECK_EQ(used_blocks(), base);
}

static void test_keyboard_ownership() {
    out("[6] keyboard ownership and Ctrl+C\n");
    g_log_n = 0; g_log[0] = 0; g_has_key = false;
    int k = spawn("keyreader", img_key, 2, false);
    for (int i = 0; i < 5; i++) task_yield();
    CHECK_EQ(g_log_n, 0);                                       // blocked: nothing typed
    g_has_key = true;
    for (int i = 0; i < 5; i++) task_yield();
    CHECK_EQ(g_log_n, 0);                                       // key present but the shell is foreground: still blocked
    task_set_foreground(k);
    int st; CHECK_EQ(task_wait(k, &st, false), 0);
    CHECK_EQ(st, 42); CHECK(g_log[0] == 'k');
    task_reset_foreground();
    CHECK(task_is_foreground());                                // the shell owns the keyboard again

    // an interruptible wait is broken by Ctrl+C, a plain one is not
    int l = spawn("loop2", img_loop, 2, false);
    g_ctrl_c = true;
    CHECK_EQ(task_wait(l, &st, true), -2);
    g_ctrl_c = false;
    CHECK_EQ(task_kill(l), 0);
    CHECK_EQ(task_wait(l, &st, false), 0);
}

extern "C" void yield_c() { task_yield(); }   // callable from the asm blocks below
static volatile uint32_t g_regs_ok = 0;
static void reg_thread() {
    uint32_t b, s, d, p;
    asm volatile(
        "movl $0x11111111, %%ebx\n\tmovl $0x22222222, %%esi\n\tmovl $0x33333333, %%edi\n\t"
        "movl $0x44444444, %%ebp\n\t"
        "call yield_c\n\tcall yield_c\n\tcall yield_c\n\t"
        "movl %%ebp, %3\n\t"
        "movl %%ebx, %0\n\tmovl %%esi, %1\n\tmovl %%edi, %2\n\t"
        : "=m"(b), "=m"(s), "=m"(d), "=m"(p) : : "eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "memory");
    g_regs_ok = (b == 0x11111111 && s == 0x22222222 && d == 0x33333333 && p == 0x44444444) ? 1 : 2;
}
static void clobber_thread() {
    for (int i = 0; i < 6; i++) {
        asm volatile("movl $0xDEAD0001, %%ebx\n\tmovl $0xDEAD0002, %%esi\n\tmovl $0xDEAD0003, %%edi\n\tmovl $0xDEAD0004, %%ebp" ::: "ebx", "esi", "edi", "ebp");
        task_yield();
    }
}

// Raw test of the asm primitive itself: no C scheduler code between the two switches,
// so only task_switch can be the reason the registers survive.
extern "C" { uint32_t raw_esp_main, raw_esp_a; }
static uint32_t raw_stack_a[1024];
static volatile uint32_t raw_result[4];
static void raw_thread_a() {
    asm volatile(
        "movl $0xBAD00001, %%ebx\n\tmovl $0xBAD00002, %%esi\n\tmovl $0xBAD00003, %%edi\n\tmovl $0xBAD00004, %%ebp\n\t"
        "pushl raw_esp_main\n\tpushl $raw_esp_a\n\tcall task_switch\n\taddl $8, %%esp\n\t"
        ::: "eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "memory", "cc");
    for (;;) {}
}

static void test_context_switch_registers() {
    out("[7] context switch preserves callee-saved registers\n");

    // (a) the bare asm primitive, between two hand-built contexts
    uint32_t* sp = raw_stack_a + 1024;
    *--sp = (uint32_t)raw_thread_a;                  // "return address": where the new context starts
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;      // ebp ebx esi edi
    *--sp = 0x2;                                     // eflags
    raw_esp_a = (uint32_t)sp;
    uint32_t b, s, d, p;
    asm volatile(
        "movl $0x11111111, %%ebx\n\tmovl $0x22222222, %%esi\n\tmovl $0x33333333, %%edi\n\tmovl $0x44444444, %%ebp\n\t"
        "pushl raw_esp_a\n\tpushl $raw_esp_main\n\tcall task_switch\n\taddl $8, %%esp\n\t"
        "movl %%ebx, %0\n\tmovl %%esi, %1\n\tmovl %%edi, %2\n\tmovl %%ebp, %3\n\t"
        : "=m"(b), "=m"(s), "=m"(d), "=m"(p) : : "eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "memory", "cc");
    CHECK(b == 0x11111111 && s == 0x22222222 && d == 0x33333333 && p == 0x44444444);
    (void)raw_result;

    // (b) through the scheduler, with a second thread clobbering every register
    int a = task_create_kthread("regs", reg_thread), c = task_create_kthread("clob", clobber_thread);
    CHECK(a > 0 && c > 0);
    for (int i = 0; i < 30 && g_regs_ok == 0; i++) task_yield();
    CHECK_EQ(g_regs_ok, 1);
    for (int i = 0; i < 10; i++) task_yield();
}

static void shell_thread() {
    task_set_shell(task_current_pid());
    uint32_t baseline = used_blocks();
    test_address_spaces();
    test_limits();
    test_preemption();
    test_sleep_and_idle();
    test_exit_kill_fault();
    test_keyboard_ownership();
    test_context_switch_registers();
    out("[8] final leak check\n");
    CHECK_EQ(used_blocks(), baseline);
    g_done = true;
}

extern "C" void _start_c() {
    map_fixed(KERNEL_HEAP_START, KERNEL_HEAP_SIZE);               // the "physical" heap
    map_fixed(PAGE_DIRECTORY_ADDRESS, 0x1000 + PAGING_TABLE_COUNT * 0x1000);   // kernel PD + page tables
    paging_init();                                                // the real identity-map builder

    task_init();
    task_create_kthread("shell", shell_thread);
    uint32_t guard = 0;
    while (!g_done && ++guard < 2000000) { task_yield(); g_ticks++; }   // main() is the idle task
    if (!g_done) { out("FATAL: tests did not finish (scheduler stuck?)\n"); g_fail++; }

    out("\n"); out_dec(g_pass); out(" checks passed, "); out_dec(g_fail); out(" failed\n");
    sys3(252, g_fail ? 1 : 0, 0, 0);                              // exit_group
}

asm(".pushsection .text\n.global _start\n_start:\n    xorl %ebp, %ebp\n    call _start_c\n    ud2\n.popsection\n");

#include "neodeck.h"

/*
 * ticker <label> [count] [delay_ms]
 *
 * Prints a line every delay_ms using sys_sleep, and checks a private 4 KB
 * buffer each time. Start two of them ("spawn ticker.bin A 6 300",
 * "spawn ticker.bin B 6 450") to see two apps running at once. Both print the
 * SAME virtual address for their buffer (every app lives at 0x40000000) but
 * `ps` shows different physical RAM, and the checks prove nobody scribbles on
 * anybody else.
 */

static unsigned char canary[4096];

static void print_dec(uint32_t v) {
    char buf[11]; int i = 10; buf[i] = 0;
    do { buf[--i] = '0' + v % 10; v /= 10; } while (v);
    sys_print(&buf[i]);
}

static void print_hex(uint32_t v) {
    char buf[11]; buf[0] = '0'; buf[1] = 'x'; buf[10] = 0;
    for (int i = 0; i < 8; i++) buf[2 + i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
    sys_print(buf);
}

static uint32_t parse(const char* s, uint32_t dflt) {
    if (!s || !*s) return dflt;
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

static void fill(uint32_t seed) { for (int i = 0; i < 4096; i++) canary[i] = (unsigned char)(seed * 31 + i); }
static bool intact(uint32_t seed) {
    for (int i = 0; i < 4096; i++) if (canary[i] != (unsigned char)(seed * 31 + i)) return false;
    return true;
}

// Must be the first function in the binary (see app.ld) - the kernel starts the task at offset 0.
extern "C" __attribute__((section(".text.entry"))) int main(int argc, char** argv) {
    const char* label = argc > 1 ? argv[1] : "T";
    uint32_t count    = argc > 2 ? parse(argv[2], 5) : 5;
    uint32_t delay    = argc > 3 ? parse(argv[3], 300) : 300;
    uint32_t pid      = sys_getpid();

    fill(pid);
    sys_print("["); sys_print(label); sys_print(" pid="); print_dec(pid);
    sys_print("] started, my buffer is at "); print_hex((uint32_t)canary); sys_print("\n");

    bool ok = true;
    for (uint32_t i = 1; i <= count; i++) {
        sys_sleep(delay);
        if (!intact(pid)) ok = false;
        sys_print("["); sys_print(label); sys_print(" pid="); print_dec(pid);
        sys_print("] tick "); print_dec(i); sys_print("/"); print_dec(count);
        sys_print("  t="); print_dec(sys_uptime_ms()); sys_print("ms  memory ");
        sys_print(ok ? "OK\n" : "CORRUPTED\n");
    }
    return ok ? 0 : 1;
}

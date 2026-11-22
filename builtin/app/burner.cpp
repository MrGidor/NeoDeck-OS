#include "vista.h"

/*
 * burner <label> [seconds]
 *
 * A CPU hog: spins without ever calling sys_sleep/sys_yield (it only reads the
 * clock and prints twice a second). If the timer did not preempt ring-3 code,
 * every other task - including the shell - would freeze while this runs. Start
 * it next to a ticker ("spawn burner.bin B 4", "spawn ticker.bin A 8 250"): the
 * ticker keeps its rhythm. It also checks a 64 KB private buffer on every pass.
 */

static volatile unsigned char buf[65536];

static void print_dec(uint32_t v) {
    char b[11]; int i = 10; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    sys_print(&b[i]);
}

static uint32_t parse(const char* s, uint32_t dflt) {
    if (!s || !*s) return dflt;
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

extern "C" __attribute__((section(".text.entry"))) int main(int argc, char** argv) {
    const char* label = argc > 1 ? argv[1] : "B";
    uint32_t seconds  = argc > 2 ? parse(argv[2], 3) : 3;
    uint32_t pid      = sys_getpid();

    uint32_t start = sys_uptime_ms();
    uint32_t next_report = start + 500;
    uint32_t passes = 0;
    bool ok = true;

    sys_print("["); sys_print(label); sys_print(" pid="); print_dec(pid);
    sys_print("] burning CPU for "); print_dec(seconds); sys_print("s without yielding\n");

    while (sys_uptime_ms() - start < seconds * 1000) {
        for (uint32_t i = 0; i < 65536; i++) buf[i] = (unsigned char)(i + pid + passes);
        for (uint32_t i = 0; i < 65536; i++)
            if (buf[i] != (unsigned char)(i + pid + passes)) ok = false;
        passes++;

        if (sys_uptime_ms() >= next_report) {
            next_report += 500;
            sys_print("["); sys_print(label); sys_print(" pid="); print_dec(pid);
            sys_print("] still spinning: "); print_dec(passes); sys_print(" passes, memory ");
            sys_print(ok ? "OK\n" : "CORRUPTED\n");
        }
    }
    sys_print("["); sys_print(label); sys_print("] done\n");
    return ok ? 0 : 1;
}

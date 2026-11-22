#include "vista.h"

static void print_dec(uint32_t v) {
    char buf[11]; int i = 10; buf[i] = 0;
    do { buf[--i] = '0' + v % 10; v /= 10; } while (v);
    sys_print(&buf[i]);
}

// Must be the first function in the binary (see app.ld) - the kernel calls offset 0.
extern "C" __attribute__((section(".text.entry"))) int main(int argc, char** argv) {
    sys_print("Vista hello: ");
    print_dec(argc);
    sys_print(" argument(s)\n");

    for (int i = 0; i < argc; i++) {
        sys_print("  argv[");
        print_dec(i);
        sys_print("] = ");
        sys_print(argv[i]);
        sys_print("\n");
    }

    sys_print("Sleeping 1 second...\n");
    uint32_t before = sys_uptime_ms();
    sys_sleep(1000);
    sys_print("Slept for ~");
    print_dec(sys_uptime_ms() - before);
    sys_print(" ms\n");

    return argc;
}

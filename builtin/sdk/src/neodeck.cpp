#include "neodeck.h"

void sys_print(const char* text) {
    asm volatile(
        "int $0x80"
        :
        : "a"(1), "b"(text)
        : "memory"
    );
}

void sys_clear() {
    asm volatile(
        "int $0x80"
        :
        : "a"(2)
        : "memory"
    );
}
typedef unsigned int uint32_t;

void sys_print(const char* text) {
    // "a" explicitly binds the value 1 to the EAX register
    // "b" explicitly binds the pointer 'text' to the EBX register
    asm volatile(
        "int $0x80"
        :
        : "a"(1), "b"(text)
        : "memory"
    );
}

void sys_clear() {
    // "a" explicitly binds the value 2 to the EAX register
    asm volatile(
        "int $0x80"
        :
        : "a"(2)
        : "memory"
    );
}

void main() {
    sys_clear();
    sys_print("NeoDeck Engine: Hello via a real Syscall Interrupt!\n");
    sys_print("This scales naturally inside your kernel print buffer.\n");
    
    // Lock the CPU here so it stays on screen when it works!
    while(1) {
        asm volatile("hlt");
    }
}
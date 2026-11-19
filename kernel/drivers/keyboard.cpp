#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "../kernel/irq.h"

static const char keyboard_map[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
  '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,   0,   0, ' '
};

static_assert((KEYBOARD_BUFFER_SIZE & (KEYBOARD_BUFFER_SIZE - 1)) == 0,
              "KEYBOARD_BUFFER_SIZE must be a power of two");

/* Single producer (IRQ1) / single consumer (main context): head is written
 * only by the IRQ, tail only by the consumer, so no lock is needed on x86. */
static volatile char     buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

static void keyboard_irq() {
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) return;                    /* key release */
    if (scancode >= sizeof(keyboard_map)) return;   /* extended / unmapped */

    char c = keyboard_map[scancode];
    if (c == 0) return;

    uint32_t next = (head + 1) & (KEYBOARD_BUFFER_SIZE - 1);
    if (next == tail) return;                       /* full: drop the key */

    buffer[head] = c;
    head = next;
}

void keyboard_init() {
    head = tail = 0;
    irq_install_handler(1, keyboard_irq);
    pic_unmask_irq(1);
}

bool keyboard_has_chars() {
    return head != tail;
}

char keyboard_pop_char() {
    if (head == tail) return 0;
    char c = buffer[tail];
    tail = (tail + 1) & (KEYBOARD_BUFFER_SIZE - 1);
    return c;
}

char keyboard_get_char() {
    while (!keyboard_has_chars()) {
        asm volatile("sti; hlt");   /* sleep until the next interrupt */
    }
    return keyboard_pop_char();
}

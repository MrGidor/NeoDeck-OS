typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "keyboard.h"
#include "io.h"

char keyboard_get_key() {
    if (inb(0x64) & 0x01) {
        uint8_t scancode = inb(0x60);
        
        if (scancode == 0x01) return 27;  // ESC key
        if (scancode == 0x0E) return '\b'; // Backspace
        if (scancode == 0x1C) return '\n'; // Enter
        
        // Simple example mapping row 1
        const char scancode_table[] = {
            0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
            0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
            'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
            'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' '
        };
        
        if (scancode < sizeof(scancode_table)) {
            return scancode_table[scancode];
        }
    }
    return 0; // Return zero if no character was typed
}
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "terminal.h"
#include "io.h"

volatile char* video_memory = (volatile char*)VGA_ADDRESS;
int terminal_column = 0;
int terminal_row = 0;
uint8_t terminal_color = 0x0F;

void update_hardware_cursor() {
    uint16_t position = (terminal_row * VGA_COLS) + terminal_column;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

void terminal_clear() {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        video_memory[i * 2]     = ' ';
        video_memory[i * 2 + 1] = DEFAULT_COLOR;
    }
    terminal_column = 0;
    terminal_row = 0;
    update_hardware_cursor();
}

void terminal_scroll() {
    for (int y = 1; y < VGA_ROWS; y++) {
        for (int x = 0; x < VGA_COLS; x++) {
            int dest_idx = ((y - 1) * VGA_COLS + x) * 2;
            int src_idx  = (y * VGA_COLS + x) * 2;
            video_memory[dest_idx]     = video_memory[src_idx];
            video_memory[dest_idx + 1] = video_memory[src_idx + 1];
        }
    }
    int last_row_start = (VGA_ROWS - 1) * VGA_COLS;
    for (int x = 0; x < VGA_COLS; x++) {
        video_memory[(last_row_start + x) * 2]     = ' ';
        video_memory[(last_row_start + x) * 2 + 1] = DEFAULT_COLOR;
    }
    terminal_row = VGA_ROWS - 1;
}

void terminal_put_char(char c) {
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else {
        int index = (terminal_row * VGA_COLS + terminal_column) * 2;
        video_memory[index]     = c;
        video_memory[index + 1] = terminal_color;
        terminal_column++;
    }
    if (terminal_column >= VGA_COLS) {
        terminal_column = 0;
        terminal_row++;
    }
    if (terminal_row >= VGA_ROWS) {
        terminal_scroll();
    }
    update_hardware_cursor();
}

void kprint(const char* str) {
    while (*str) {
        terminal_put_char(*str);
        str++;
    }
}

void kprint_color(const char* str, uint8_t color) {
    uint8_t old_color = terminal_color;
    terminal_color = color;
    kprint(str);
    terminal_color = old_color;
}

void kprint_success(const char* str) { kprint_color(str, COLOR_SUCCESS); }
void kprint_warning(const char* str) { kprint_color(str, COLOR_WARNING); }
void kprint_error(const char* str)   { kprint_color(str, COLOR_ERROR);   }
void kprint_info(const char* str)    { kprint_color(str, COLOR_INFO);    }
#ifndef TERMINAL_H
#define TERMINAL_H

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ADDRESS 0xB8000

#define DEFAULT_COLOR 0x0F
#define COLOR_SUCCESS  0x0A
#define COLOR_WARNING  0x0E
#define COLOR_ERROR    0x0C
#define COLOR_INFO     0x0B

extern volatile char* video_memory;
extern int terminal_column;
extern int terminal_row;
extern uint8_t terminal_color;

void terminal_clear();
void terminal_scroll();
void terminal_put_char(char c);
void kprint(const char* str);
void kprint_color(const char* str, uint8_t color);
void kprint_success(const char* str);
void kprint_warning(const char* str);
void kprint_error(const char* str);
void kprint_info(const char* str);
void update_hardware_cursor();

#endif
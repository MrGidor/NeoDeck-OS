typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

// A temporary buffer in kernel heap/BSS to hold our active file text
#define EDITOR_BUF_SIZE 4096
alignas(4) char editor_buffer[EDITOR_BUF_SIZE];

#include "editor.h"
#include "../drivers/terminal.h"
#include "../drivers/keyboard.h"
#include "../fs/neofs.h"

void run_text_editor(const char* filename) {
    for (int i = 0; i < EDITOR_BUF_SIZE; i++) editor_buffer[i] = '\0';
    
    int buffer_index = neofs_read_to_buffer(filename, editor_buffer, EDITOR_BUF_SIZE);

    terminal_clear();
    
    uint8_t old_color = terminal_color;
    terminal_color = 0x70; 
    kprint(" NeoDeck Editor v1.0 | File: ");
    kprint(filename);
    kprint(" | Press ESC to Save and Exit \n");
    terminal_color = old_color; 

    terminal_row = 2;
    terminal_column = 0;
    update_hardware_cursor();

    if (buffer_index > 0) {
        kprint(editor_buffer);
    }

    int editing = 1;
    while (editing) {

        char c = keyboard_get_key(); 

        if (c == 0) continue; 

        if (c == 27) { 
            editing = 0;
            break;
        }

        if (c == '\b') { 
            if (buffer_index > 0) {
                buffer_index--;
                editor_buffer[buffer_index] = '\0';
                
                if (terminal_column > 0) {
                    terminal_column--;
                } else if (terminal_row > 2) { 
                    terminal_row--;
                    terminal_column = VGA_COLS - 1;
                }
                
                int index = (terminal_row * VGA_COLS + terminal_column) * 2;
                video_memory[index] = ' ';
                update_hardware_cursor();
            }
            continue;
        }

        if (c == '\n') {
            if (buffer_index < EDITOR_BUF_SIZE - 1) {
                editor_buffer[buffer_index++] = '\n';
                terminal_column = 0;
                terminal_row++;
                if (terminal_row >= VGA_ROWS) terminal_scroll();
                update_hardware_cursor();
            }
            continue;
        }


        if (buffer_index < EDITOR_BUF_SIZE - 1) {
            editor_buffer[buffer_index++] = c;
            terminal_put_char(c); 
        }
    }

    terminal_clear();
    
    neofs_write(filename, editor_buffer); 
    
}
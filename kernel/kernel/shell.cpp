#include "../include/types.h"
#include "shell.h"
#include "idt.h"
#include "../drivers/terminal.h"
#include "../drivers/io.h"
#include "../drivers/pic.h"
#include "../drivers/ata.h"
#include "../shell/string.h"
#include "../fs/neofs.h"
#include "../memory/memory.h"
#include "../shell/editor.h"
#include "../drivers/keyboard.h"
#include "execve.h"
#include "../drivers/timer.h"

#define COMMAND_BUFFER_SIZE 256
char command_buffer[COMMAND_BUFFER_SIZE];
int command_len = 0;

void parse_command(const char* cmd);

/* Called from the main loop for every character the keyboard IRQ buffered.
 * Runs in normal context with interrupts enabled, never inside an IRQ. */
void shell_handle_char(char c) {
    if (c == '\n') {
        command_buffer[command_len] = '\0';
        terminal_put_char('\n');

        if (command_len > 0) {
            parse_command(command_buffer);
        } else {
            kprint("> ");
        }
        command_len = 0;

    } else if (c == '\b') {
        if (command_len > 0) {
            command_len--;

            if (terminal_column > 0) {
                terminal_column--;
            } else if (terminal_row > 0) {
                terminal_row--;
                terminal_column = VGA_COLS - 1;
            }

            int index = (terminal_row * VGA_COLS + terminal_column) * 2;
            video_memory[index] = ' ';
            video_memory[index + 1] = DEFAULT_COLOR;
            update_hardware_cursor();
        }
    } else if (c >= ' ' && c < 127 && command_len < COMMAND_BUFFER_SIZE - 1) {
        command_buffer[command_len++] = c;
        terminal_put_char(c);
    }
}

void neodeck_meminfo() {
    uint32_t used_blocks = 0;

    for (int i = 0; i < allocation_bitmap_size; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;

        if (allocation_bitmap[byte_index] & (1 << bit_index)) {
            used_blocks++;
        }
    }

    uint32_t total_heap_bytes = KERNEL_HEAP_SIZE;
    uint32_t used_heap_bytes  = used_blocks * BLOCK_SIZE;
    uint32_t free_heap_bytes  = total_heap_bytes - used_heap_bytes;

    kprint_info("NeoDeck Heap Memory Diagnostics:\n");
    kprint_info("  Total Heap Space: ");
    
    kprint_dec(total_heap_bytes / 1024); kprint_info(" KB\n  Used Heap Space:  ");
    kprint_dec(used_heap_bytes);         kprint_info(" Bytes (");
    kprint_dec(used_blocks);             kprint_info(" blocks)\n  Free Heap Space:  ");
    kprint_dec(free_heap_bytes / 1024);  kprint_info(" KB\n");
}

void parse_command(const char* cmd) {
    if (kstrcmp(cmd, "help") == 0) {
        kprint("NeoDeck OS Supported Commands:\n");
        kprint("  help      - Display this menu\n");
        kprint("  clear     - Flush the terminal screen\n");
        kprint("  format    - Wipe hard disk and create NeoFS Root\n");
        kprint("  ls        - List contents of current directory\n");
        kprint("  mkdir <d> - Create a custom directory entry\n");
        kprint("  cd <dir>  - Change active directory path (use '..' for root)\n");
        kprint("  touch <file> - Create an empty file\n");
        kprint("  write <file> <text> - Write text content into a file\n");
        kprint("  cat <file> - Display the text content of a file\n");
        kprint("  meminfo   - Display kernel heap memory usage stats\n");
        kprint("  uptime    - Time since boot\n");
        kprint("  sleep <ms> - Sleep for N milliseconds\n");
        kprint("  exec <file> [args] - Run an app binary with arguments\n");
        kprint("  edit <file> - Open the text editor\n");
    } 
    else if (kstrcmp(cmd, "clear") == 0) {
        terminal_clear();
    } 
    else if (kstrcmp(cmd, "format") == 0) {
        neofs_format();
    }
    else if (kstrcmp(cmd, "ls") == 0) {
        neofs_ls();
    }
    else if (kstrncmp(cmd, "exec ", 5) == 0) {
        sys_exec(cmd + 5);
    }
    else if (kstrncmp(cmd, "mkdir ", 6) == 0) {
        const char* dir_name = cmd + 6;
        if (*dir_name == '\0') kprint("Usage: mkdir <name>\n");
        else neofs_mkdir(dir_name);
    }
    else if (kstrncmp(cmd, "cd ", 3) == 0) {
        const char* target_dir = cmd + 3;
        if (*target_dir == '\0') kprint("Usage: cd <directory_name>\n");
        else neofs_cd(target_dir);
    }
    else if (kstrncmp(cmd, "touch ", 6) == 0) {
        const char* filename = cmd + 6;
        if (*filename == '\0') kprint("Usage: touch <filename>\n");
        else neofs_touch(filename);
    }
    else if (kstrcmp(cmd, "uptime") == 0) {
        uint32_t ms = timer_uptime_ms();
        kprint("Uptime: "); kprint_dec(ms / 1000); kprint("."); 
        uint32_t frac = ms % 1000;
        if (frac < 100) kprint("0");
        if (frac < 10) kprint("0");
        kprint_dec(frac); kprint(" s (");
        kprint_dec(timer_ticks()); kprint(" ticks)\n");
    }
    else if (kstrncmp(cmd, "sleep ", 6) == 0) {
        timer_sleep_ms(katou(cmd + 6));
    }
    else if (kstrncmp(cmd, "meminfo", 7) == 0) {
        neodeck_meminfo();
    }
    else if (kstrncmp(cmd, "panic", 5) == 0) { // COMMAND TO TEST KERNEL PANIC
        int which = (cmd[5] == ' ') ? (int)katou(cmd + 6) : 0;
        if (which == 0) {            // #DE divide error
            volatile int zero = 0;
            asm volatile("div %0" : : "r"(zero), "a"(1), "d"(0));
        } else if (which == 1) {     // #UD invalid opcode
            asm volatile("ud2");
        } else if (which == 2) {     // #PF page fault (address above the 48 MB identity map)
            *(volatile int*)0x40000000 = 1;
        } else if (which == 3) {     // #GP general protection (bad segment selector)
            asm volatile("mov $0x1234, %%ax; mov %%ax, %%ds" : : : "ax");
        }
    }
    else if (kstrncmp(cmd, "write ", 6) == 0) {
        const char* params = cmd + 6; 
        
        int space_idx = kstrchr(params, ' ');
        if (space_idx == -1) {
            kprint("Usage: write <filename> <text_content>\n");
        } else {
            char filename[MAX_FILENAME];
            int i = 0;
            for (; i < space_idx && i < (MAX_FILENAME - 1); i++) {
                filename[i] = params[i];
            }
            filename[i] = '\0';

            const char* text_payload = params + space_idx + 1;
            
            neofs_write(filename, text_payload);
        }
    }
    else if (kstrncmp(cmd, "edit ", 5) == 0) {
        const char* target_file = cmd + 5;
        
        neofs_touch(target_file); 
        
        run_text_editor(target_file);
    }
    else if (kstrncmp(cmd, "cat ", 4) == 0) {
        const char* filename = cmd + 4;
        if (*filename == '\0') kprint("Usage: cat <filename>\n");
        else neofs_cat(filename);
    }
    else {
        kprint_error("NeoDeck Error: Command '");
        kprint(cmd);
        kprint_error("' not recognized.\n");
    }

    kprint("\n");
    if (current_directory_inode == 0) {
        kprint("/");
    } else {
        uint8_t root_dir_buffer[512];
        ata_read_sector(ROOT_DIR_SECTOR, root_dir_buffer);
        neofs_dir_entry* entries = (neofs_dir_entry*)root_dir_buffer;    

        int name_found = 0;

        for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
            if (entries[i].used == 1 && entries[i].inode_num == current_directory_inode) {
                kprint("/");
                kprint(entries[i].name);
                name_found = 1;
                break;
            }
        }
        
        if (!name_found) {
            kprint("/sub-dir");
        }

    }
    kprint("> ");
}

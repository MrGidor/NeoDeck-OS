typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ADDRESS 0xB8000
// Status Theme Palettes (Format: Foreground | (Background << 4))

#define DEFAULT_COLOR 0x0F // White text on black background
#define COLOR_SUCCESS  0x0A  // Bright Green on Black
#define COLOR_WARNING  0x0E  // Yellow on Black
#define COLOR_ERROR    0x0C  // Bright Red on Black
#define COLOR_INFO     0x0B  // Light Cyan on Black

// Global state for our terminal layout
volatile char* video_memory = (volatile char*)VGA_ADDRESS;
int terminal_column = 0;
int terminal_row = 0;
uint8_t terminal_color = 0x0F;

#define NULL 0

#define KERNEL_HEAP_START 0x100000   // 1MB
#define KERNEL_HEAP_SIZE  0x100000   // 1MB heap
void* dynamic_mem_loc = (void*)KERNEL_HEAP_START;

#define BLOCK_SIZE 16             
#define BITMAP_SIZE (KERNEL_HEAP_SIZE / BLOCK_SIZE)

uint8_t allocation_bitmap[BITMAP_SIZE / 8]; 
int allocation_bitmap_size = BITMAP_SIZE; 

// storage layout definitions for the simple NeoFS filesystem
#define INODE_TABLE_SECTOR 2
#define ROOT_DIR_SECTOR    3

#define MAX_INODES 16
#define MAX_DIR_ENTRIES 16
#define MAX_FILENAME 14

#define TYPE_FILE 1
#define TYPE_DIR  2

struct neofs_inode {
    uint8_t  type;          // 1 for file, 2 for directory
    uint32_t size;          // Bytes utilized
    uint32_t start_sector;  // First disk sector containing file content
    uint8_t  used;          // Active status marker
} __attribute__((packed));

struct neofs_dir_entry {
    uint32_t inode_num;            // Target index link
    char     name[MAX_FILENAME];   // Linux text label, pl., "bin", "home"
    uint8_t  used;                 // Active slot tracker
} __attribute__((packed));

// Active path trackers for navigating directories
int current_directory_inode = 0;

extern "C" void enable_paging(uint32_t page_directory_address);


void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
// filesystem and ATA driver functions


// Read a 16-bit word from an I/O port
uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}


// ----------------------
// Memory Management
// ----------------------

int get_bitmap(uint8_t* map, int index) {
    return (map[index / 8] >> (index % 8)) & 1;
}

void set_bitmap(uint8_t* map, int index) {
    map[index / 8] |= (1 << (index % 8));
}

void *kmalloc(int n_blocks) {
    int contiguous = 0;
    int start = 0;
    for(int i = 0; i < allocation_bitmap_size; i++) {
        if (contiguous == 0) start = i;
        if (get_bitmap(allocation_bitmap, i) == 0)
            contiguous++;
        else {
            contiguous = 0;
            continue;
        }
        if (contiguous >= n_blocks) {
            for (int j = start; j < start + n_blocks; j++)
                set_bitmap(allocation_bitmap, j);
            return (void*)((int)dynamic_mem_loc + start * BLOCK_SIZE);
        }
    }
    return NULL;
}

// ----------------------
// Terminal Screen Driver
// ----------------------

// Simple string comparison function for my CLI parsing needs
int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// Finds the first occurrence of a character in a string
int kstrchr(const char* str, char c) {
    int i = 0;
    while (str[i] != '\0') {
        if (str[i] == c) return i;
        i++;
    }
    return -1; // Not found
}

void update_hardware_cursor() {
    uint16_t position = (terminal_row * VGA_COLS) + terminal_column;

    // low
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF)); // Send bits 0-7

    // high
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF)); // Send bits 8-15
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

// Prints text in a specific color, then restores the system default
void kprint_color(const char* str, uint8_t color) {
    uint8_t old_color = terminal_color; // Save whatever color the terminal was using
    terminal_color = color;             // Switch to our contextual color
    kprint(str);                        // Print the string using existing logic
    terminal_color = old_color;         // Restore previous color immediately
}

// Context-specific wrapper functions for clean code reading
void kprint_success(const char* str) { kprint_color(str, COLOR_SUCCESS); }
void kprint_warning(const char* str) { kprint_color(str, COLOR_WARNING); }
void kprint_error(const char* str)   { kprint_color(str, COLOR_ERROR);   }
void kprint_info(const char* str)    { kprint_color(str, COLOR_INFO);    }

// ----------------------
// I/O Port Communication and PIC Remapping
// ----------------------

// Low-level ATA driver function to read a 512-byte sector from the Master HDD
void ata_read_sector(uint32_t target_sector, uint8_t* target_buffer) {
    outb(0x1F2, 1);                         // Sector count (1)
    outb(0x1F3, (uint8_t)target_sector);    // LBA Low
    outb(0x1F4, (uint8_t)(target_sector >> 8));   // LBA Mid
    outb(0x1F5, (uint8_t)(target_sector >> 16));  // LBA High
    outb(0x1F6, 0xE0 | ((target_sector >> 24) & 0x0F)); // Master Drive selection + LBA top
    outb(0x1F7, 0x20);                      // Command 0x20: Read Sectors

    while ((inb(0x1F7) & 0x80));            // Loop while controller is Busy
    while (!(inb(0x1F7) & 0x08));           // Loop until Data is Ready

    uint16_t* buffer16 = (uint16_t*)target_buffer;
    for (int i = 0; i < 256; i++) {
        buffer16[i] = inw(0x1F0);           // Pull data out 2 bytes at a time
    }
}

// Low-level ATA driver function to write a 512-byte sector back to the Master HDD
void ata_write_sector(uint32_t target_sector, uint8_t* source_buffer) {
    outb(0x1F2, 1);                         // Sector count (1)
    outb(0x1F3, (uint8_t)target_sector);    // LBA Low
    outb(0x1F4, (uint8_t)(target_sector >> 8));   // LBA Mid
    outb(0x1F5, (uint8_t)(target_sector >> 16));  // LBA High
    outb(0x1F6, 0xE0 | ((target_sector >> 24) & 0x0F)); // Master Drive selection + LBA top
    outb(0x1F7, 0x30);                      // Command 0x30: Write Sectors

    while ((inb(0x1F7) & 0x80));            // Wait until hardware is un-busy
    while (!(inb(0x1F7) & 0x08));           // Wait until hardware is ready to accept data

    uint16_t* buffer16 = (uint16_t*)source_buffer;
    for (int i = 0; i < 256; i++) {
        asm volatile ("outw %0, %1" : : "a"(buffer16[i]), "Nd"(0x1F0)); // Push words to data register
    }
    
    // Flush the drive cache so changes write permanently
    outb(0x1F7, 0xE7); 
    while ((inb(0x1F7) & 0x80));
}

// Helper utility to copy raw bytes 
void kstrncpy(char* dest, const char* src, int n) {
    for (int i = 0; i < n; i++) {
        dest[i] = src[i];
        if (src[i] == '\0') break;
    }
}

// Lists all files and directories inside the active directory frame
void neofs_ls() {
    uint8_t sector_buffer[512];
    uint8_t inode_buffer[512];
    
    // Read current directory entries
    ata_read_sector(INODE_TABLE_SECTOR, inode_buffer);
    neofs_inode* inodes = (neofs_inode*)inode_buffer;
    uint32_t dir_sector = inodes[current_directory_inode].start_sector;

    ata_read_sector(dir_sector, sector_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)sector_buffer;

    int items_found = 0;
    kprint("Directory listing:\n");

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1) {
            items_found++;
            
            // Look up the item's inode to see if it's a directory or a file
            uint32_t item_inode = entries[i].inode_num;
            
            if (inodes[item_inode].type == TYPE_DIR) {
                kprint("  [DIR]  ");
            } else {
                kprint("  [FILE] ");
            }
            
            kprint(entries[i].name);
            kprint("\n");
        }
    }

    if (items_found == 0) {
        kprint("  (empty directory)\n");
    }
}

// Changes the active shell directory state
void neofs_cd(const char* target_name) {
    uint8_t sector_buffer[512];

    // cd .. is a special case since it doesn't actually exist as a directory entry, but we still need to support it for basic navigation
    // (To keep code simple for now, we just jump back to Inode 0) im lazy as fuck
    if (kstrcmp(target_name, "..") == 0) {
        current_directory_inode = 0;
        return;
    }

    // 2. Read the current directory's entries
    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;
    uint32_t dir_sector = inodes[current_directory_inode].start_sector;

    ata_read_sector(dir_sector, sector_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)sector_buffer;

    // 3. Search for a matching directory name
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, target_name) == 0) {
            current_directory_inode = entries[i].inode_num;
            return;
        }
    }

    kprint("NeoFS Error: Directory '");
    kprint(target_name);
    kprint("' not found.\n");
}

// Creates a new Linux-style directory entry node
void neofs_mkdir(const char* dir_name) {
    uint8_t inode_buffer[512];
    uint8_t dir_buffer[512];

    // Load the Inode Table to find a free slot
    ata_read_sector(INODE_TABLE_SECTOR, inode_buffer);
    neofs_inode* inodes = (neofs_inode*)inode_buffer;

    int free_inode = -1;
    for (int i = 1; i < MAX_INODES; i++) { // Skip 0 (Root)
        if (inodes[i].used == 0) {
            free_inode = i;
            break;
        }
    }

    if (free_inode == -1) {
        kprint("NeoFS Error: Maximum Inode allocation limit hit!\n");
        return;
    }

    // Find where the current directory lists its name tags
    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    int free_entry_slot = -1;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 0) {
            free_entry_slot = i;
            break;
        }
    }

    if (free_entry_slot == -1) {
        kprint("NeoFS Error: Current directory is completely full!\n");
        return;
    }

    // Assign the new Inode's parameters
    // We give each folder its own unique storage block dynamically (Sector 10 + Inode number)
    inodes[free_inode].type = TYPE_DIR;
    inodes[free_inode].size = 0;
    inodes[free_inode].start_sector = 10 + free_inode; 
    inodes[free_inode].used = 1;

    // Update the Parent Directory Entry map
    entries[free_entry_slot].inode_num = free_inode;
    entries[free_entry_slot].used = 1;
    kstrncpy(entries[free_entry_slot].name, dir_name, MAX_FILENAME);

    //Commit both blocks back to the hard disk image permanently
    ata_write_sector(INODE_TABLE_SECTOR, inode_buffer);
    ata_write_sector(current_dir_sector, dir_buffer);

    // Clear out the newly allocated directory sector on disk so it contains zero garbage data
    uint8_t clear_buffer[512];
    for(int i=0; i<512; i++) clear_buffer[i] = 0;
    ata_write_sector(inodes[free_inode].start_sector, clear_buffer);

    kprint("Directory '");
    kprint(dir_name);
    kprint("' created.\n");
}

// Creates a empty file entry in the current directory
void neofs_touch(const char* filename) {
    uint8_t inode_buffer[512];
    uint8_t dir_buffer[512];

    // Load the Inode Table to find a free slot
    ata_read_sector(INODE_TABLE_SECTOR, inode_buffer);
    neofs_inode* inodes = (neofs_inode*)inode_buffer;

    int free_inode = -1;
    for (int i = 1; i < MAX_INODES; i++) {
        if (inodes[i].used == 0) {
            free_inode = i;
            break;
        }
    }

    if (free_inode == -1) {
        kprint("NeoFS Error: Maximum Inode limit reached!\n");
        return;
    }

    //  Find the current directory's disk sector
    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    int free_entry_slot = -1;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 0) {
            free_entry_slot = i;
            break;
        }
    }

    if (free_entry_slot == -1) {
        kprint("NeoFS Error: Current directory is full!\n");
        return;
    }

    // Assign the new Inode as a FILE
    inodes[free_inode].type = TYPE_FILE; // Type 1 = File
    inodes[free_inode].size = 0;         
    inodes[free_inode].start_sector = 20 + free_inode; // Data sector block
    inodes[free_inode].used = 1;

    // Update the Directory Entry name tag
    entries[free_entry_slot].inode_num = free_inode;
    entries[free_entry_slot].used = 1;
    kstrncpy(entries[free_entry_slot].name, filename, MAX_FILENAME);

    //  Commit changes back to disk
    ata_write_sector(INODE_TABLE_SECTOR, inode_buffer);
    ata_write_sector(current_dir_sector, dir_buffer);

    // Clear out the file's data sector so it's fresh
    uint8_t clear_buffer[512];
    for(int i = 0; i < 512; i++) clear_buffer[i] = 0;
    ata_write_sector(inodes[free_inode].start_sector, clear_buffer);

    kprint("File '");
    kprint(filename);
    kprint("' created.\n");
}

// Writes text content into an existing file entry using kmalloc
void neofs_write(const char* filename, const char* text) {
    uint8_t sector_buffer[512];

    // Read the Inode table to find our file tracking structures
    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

    // Read the current directory entries to match the filename string
    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    uint8_t dir_buffer[512];
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    int target_inode = -1;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, filename) == 0) {
            target_inode = entries[i].inode_num;
            break;
        }
    }

    // Ensure the file exists and is actually a file (not a directory)
    if (target_inode == -1 || inodes[target_inode].type != TYPE_FILE) {
        kprint("NeoFS Error: File not found.\n");
        return;
    }

    // Request 32 blocks from heap (32 blocks * 16 bytes = 512 bytes for a disk sector)
    uint8_t* data_block = (uint8_t*)kmalloc(32);
    
    if (data_block == NULL) {
        kprint("NeoFS Error: Out of heap memory! kmalloc failed.\n");
        return;
    }

    // Zero out the freshly allocated heap buffer
    for (int i = 0; i < 512; i++) data_block[i] = 0;

    // Count text size and copy text bytes into our dynamic heap block
    int size = 0;
    while (text[size] != '\0' && size < 512) {
        data_block[size] = text[size];
        size++;
    }

    // Update the file's metadata tracking size constraints
    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    inodes = (neofs_inode*)sector_buffer;
    inodes[target_inode].size = size;

    // Blast the buffers straight to the virtual disk hardware map via ATA
    ata_write_sector(INODE_TABLE_SECTOR, sector_buffer);              // Save file size update
    ata_write_sector(inodes[target_inode].start_sector, data_block);  // Save raw text data
    
    // Note to self: If I write a kfree block allocator later, I should release the data_block here (but for now we just leak it since the OS is single-use and will free all memory on reboot)
    // Just leak my hopes as well

    kprint("Committed data to disk storage via Kernel Heap.\n");
}

void neofs_cat(const char* filename) {
    uint8_t sector_buffer[512];

    ata_read_sector(INODE_TABLE_SECTOR, sector_buffer);
    neofs_inode* inodes = (neofs_inode*)sector_buffer;

    uint32_t current_dir_sector = inodes[current_directory_inode].start_sector;
    uint8_t dir_buffer[512];
    ata_read_sector(current_dir_sector, dir_buffer);
    neofs_dir_entry* entries = (neofs_dir_entry*)dir_buffer;

    int target_inode = -1;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].used == 1 && kstrcmp(entries[i].name, filename) == 0) {
            target_inode = entries[i].inode_num;
            break;
        }
    }

    if (target_inode == -1 || inodes[target_inode].type != TYPE_FILE) {
        kprint("NeoFS Error: File not found.\n");
        return;
    }

    // Read the raw disk sector back into a memory buffer
    uint8_t data_block[512];
    ata_read_sector(inodes[target_inode].start_sector, data_block);

    // Safeguard null termination string edge limits
    uint32_t file_size = inodes[target_inode].size;
    if (file_size > 511) file_size = 511;
    data_block[file_size] = '\0';

    kprint((const char*)data_block);
    kprint("\n");
}

// Low-level Format: Sets up the Root Directory (/) on an unformatted disk
void neofs_format() {
    uint8_t sector_buffer[512];

    // Clear the Inode Table Sector
    for (int i = 0; i < 512; i++) sector_buffer[i] = 0;
    
    neofs_inode* inodes = (neofs_inode*)sector_buffer;
    inodes[0].type = TYPE_DIR;
    inodes[0].size = 0;
    inodes[0].start_sector = ROOT_DIR_SECTOR;
    inodes[0].used = 1;

    ata_write_sector(INODE_TABLE_SECTOR, sector_buffer);

    // Clear the Root Directory Entry Sector
    for (int i = 0; i < 512; i++) sector_buffer[i] = 0;
    
    ata_write_sector(ROOT_DIR_SECTOR, sector_buffer);

    kprint("NeoFS filesystem initialized with root layout successfully.\n");

    neofs_mkdir("home");
    neofs_mkdir("bin");
    neofs_mkdir("etc");
    neofs_mkdir("var");
    neofs_mkdir("tmp");
    neofs_touch("readme.txt");
    neofs_write("readme.txt", "Welcome to NeoDeck OS! This is a simple text file created on the root directory of your NeoFS virtual disk. Feel free to explore the filesystem, create new directories and files, and write your own content. \n");
}


// pic remapping constants and functions to avoid IRQ conflicts with CPU exceptions

#define PIC1          0x20    
#define PIC2          0xA0    
#define PIC1_COMMAND  PIC1
#define PIC1_DATA     (PIC1+1)
#define PIC2_COMMAND  PIC2
#define PIC2_DATA     (PIC2+1)

void pic_remap() {
    uint8_t a1, a2;
    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);     // Master maps to 0x20 - 0x27
    outb(PIC2_DATA, 0x28);     // Slave maps to 0x28 - 0x2F
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

// Global EOI tool for PIC cleanup
void pic_send_eoi(uint8_t irq_no) {
    if (irq_no >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

// ----------------------
// Keyboard Mapping & Handling
// ----------------------

#define COMMAND_BUFFER_SIZE 256
char command_buffer[COMMAND_BUFFER_SIZE];
int command_len = 0;

// Forward declaration of our command parser
void parse_command(const char* cmd);

extern "C" void isr21(); 

// Standard US QWERTY keyboard scancode to ASCII mapping (ignoring Shift/Ctrl/Alt modifiers for simplicity)
// I should probably add Shift support later but for now this is fine for basic command input
// Also should support other stuff like other keyboard layouts but nah.
const char keyboard_map[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,   0,   0, ' '
};

// cdecl enforcement to prevent stack realignment crashes on raw ISR entry
extern "C" __attribute__((cdecl)) void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    if (scancode < sizeof(keyboard_map)) {
        if (!(scancode & 0x80)) { 
            char c = keyboard_map[scancode];
            
            if (c == '\n') {
                // Null-terminate the string buffer
                command_buffer[command_len] = '\0';
                
                terminal_put_char('\n'); 
                
                if (command_len > 0) {
                    parse_command(command_buffer); 
                } else {
                    kprint("> "); // Empty line, just reprint prompt
                }
                
                // Reset buffer length for the next command
                command_len = 0;

            } else if (c == '\b') {
                // Handle Backspace
                if (command_len > 0) {
                    command_len--;
                    
                    // Visually erase character from VGA memory
                    if (terminal_column > 0) {
                        terminal_column--;
                    } else if (terminal_row > 0) {
                        terminal_row--;
                        terminal_column = VGA_COLS - 1;
                    }
                    
                    int index = (terminal_row * VGA_COLS + terminal_column) * 2;
                    video_memory[index] = ' ';
                    video_memory[index + 1] = DEFAULT_COLOR;
                    update_hardware_cursor(); // Move cursor back to the correct position
                }
            } else if (c != 0 && command_len < COMMAND_BUFFER_SIZE - 1) {
                // Normal character typing
                command_buffer[command_len++] = c;
                terminal_put_char(c);
            }
        }
    }

    pic_send_eoi(1);
}

void neodeck_meminfo() {
    uint32_t used_blocks = 0;

    // Scan through exactly 65,536 bits tracked by the allocation bitmap
    for (int i = 0; i < allocation_bitmap_size; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;

        // Check if this specific 16-byte block is flipped to 1 (Allocated)
        if (allocation_bitmap[byte_index] & (1 << bit_index)) {
            used_blocks++;
        }
    }

    // Mathematical conversions based on the 16-byte block resolution
    uint32_t total_heap_bytes = KERNEL_HEAP_SIZE;
    uint32_t used_heap_bytes  = used_blocks * BLOCK_SIZE;
    uint32_t free_heap_bytes  = total_heap_bytes - used_heap_bytes;

    kprint_info("NeoDeck Heap Memory Diagnostics:\n");
    kprint_info("  Total Heap Space: ");
    
    // inline base-10 number string converter
    auto kprint_int = [](uint32_t num) {
        if (num == 0) { kprint("0"); return; }
        char buf[32];
        int idx = 0;
        while (num > 0) {
            buf[idx++] = (num % 10) + '0';
            num /= 10;
        }
        for (int j = idx - 1; j >= 0; j--) {
            char c[2] = { buf[j], '\0' };
            kprint(c);
        }
    };

    kprint_int(total_heap_bytes / 1024); kprint(" KB\n  Used Heap Space:  ");
    kprint_int(used_heap_bytes);         kprint(" Bytes (");
    kprint_int(used_blocks);             kprint(" blocks)\n  Free Heap Space:  ");
    kprint_int(free_heap_bytes / 1024);  kprint(" KB\n");
}

// CLI command parser

// Helper to check if a string starts with a specific prefix
int kstrncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == '\0') break;
    }
    return 0;
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
    else if (kstrncmp(cmd, "meminfo", 7) == 0) {
        neodeck_meminfo();
    }
    else if (kstrncmp(cmd, "write ", 6) == 0) {
        const char* params = cmd + 6; 
        
        int space_idx = kstrchr(params, ' ');
        if (space_idx == -1) {
            kprint("Usage: write <filename> <text_content>\n");
        } else {
            // Extract the filename out into a separate string buffer
            char filename[MAX_FILENAME];
            int i = 0;
            for (; i < space_idx && i < (MAX_FILENAME - 1); i++) {
                filename[i] = params[i];
            }
            filename[i] = '\0';

            // Everything after the dividing space character is the text payload
            const char* text_payload = params + space_idx + 1;
            
            neofs_write(filename, text_payload);
        }
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

    // --- REPRINT PROMPT (Dynamic Path Style) ---
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
        
        // Fallback safety catch
        if (!name_found) {
            kprint("/sub-dir");
        }

    }
    kprint("> ");
}


// ----------------------
// IDT and Interrupt Handling
// ----------------------

struct idt_entry_struct {
    uint16_t base_low;           
    uint16_t sel;                
    uint8_t  always0;            
    uint8_t  flags;              
    uint16_t base_high;          
} __attribute__((packed));

struct idt_ptr_struct {
    uint16_t limit;              
    uint32_t base;               
} __attribute__((packed));

idt_entry_struct idt[256];
idt_ptr_struct   idt_reg;

extern "C" void load_idt(uint32_t idt_ptr_address);
extern "C" void isr0(); 
extern "C" void default_stub_handler(); // Safe fallback wrapper

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags; 
}

void idt_init() {
    idt_reg.limit = (sizeof(idt_entry_struct) * 256) - 1;
    idt_reg.base  = (uint32_t)&idt;

    // Initialize all 256 gates to point to a safe stub handler
    // This stops spurious or unmapped interrupts from causing a crash loop
    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, (uint32_t)default_stub_handler, 0x08, 0x8E);
    }

    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(0x21, (uint32_t)isr21, 0x08, 0x8E);

    load_idt((uint32_t)&idt_reg);
}

extern "C" void isr0_handler() {
    kprint_error("\n========================================\n");
    kprint_error(" CRITICAL KERNEL EXCEPTION: DIVIDE BY 0 \n");
    kprint_error(" System Execution Halted to Protect Data.\n");
    kprint_error("========================================\n");
    asm volatile("cli; hlt");
}

// Fallback logic for unassigned interrupts to clear PIC hooks safely
extern "C" __attribute__((cdecl)) void fallback_handler() {
    pic_send_eoi(7); // Acknowledge generic line 
}


//----------------------
// Core Execution
//----------------------

// Paging and virtual memory management constants and functions
#define PAGE_DIRECTORY_ADDRESS  0x9C000
#define PAGE_TABLE_0_ADDRESS    0x9D000

// Sets up a simple 4MB identity paging map for the first 4MB of memory
void paging_init() {
    // Cast the raw memory addresses to volatile pointers
    uint32_t* page_directory = (uint32_t*)PAGE_DIRECTORY_ADDRESS;
    uint32_t* first_page_table = (uint32_t*)PAGE_TABLE_0_ADDRESS;

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002; 
    }

    for (unsigned int i = 0; i < 1024; i++) {
        first_page_table[i] = (i * 4096) | 3; 
    }

    page_directory[0] = (PAGE_TABLE_0_ADDRESS) | 3; 
}

// The main entry point for the kernel after bootloader hands off control
extern "C" void main() {
    terminal_clear();

    for (int i = 0; i < BITMAP_SIZE / 8; i++)
        allocation_bitmap[i] = 0;

    kprint("Booting NeoDeck...\n");
    kprint_info("Memory allocation bitmap safely initialized.\n");

    idt_init();
    pic_remap(); 
    kprint_info("PIC and hardware registers configured successfully.\n");

    kprint_info("Initializing 4MB identity paging map...\n");
    paging_init();

    kprint_info("Enabling CPU Memory Management Unit (MMU)...\n");
    enable_paging(PAGE_DIRECTORY_ADDRESS);
    kprint_info("Virtual memory architecture operational!\n");

    asm volatile("sti");
    kprint_info("System ready. Type 'help' for a list of supported commands.\n");
    kprint("\n/> ");

    while(1) { 
        asm volatile("hlt");
    }
}
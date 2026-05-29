#ifndef MEMORY_H
#define MEMORY_H

#define KERNEL_HEAP_START 0x1000000
#define KERNEL_HEAP_SIZE  0x2000000
#define BLOCK_SIZE 512
#define BITMAP_SIZE (KERNEL_HEAP_SIZE / BLOCK_SIZE)

void* kmalloc(int n_blocks);
int get_bitmap(unsigned char* map, int index);
void set_bitmap(unsigned char* map, int index);

extern unsigned char allocation_bitmap[BITMAP_SIZE / 8];
extern int allocation_bitmap_size;

#endif
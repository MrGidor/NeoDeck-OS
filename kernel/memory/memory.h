#ifndef MEMORY_H
#define MEMORY_H

#include "../include/layout.h"   /* KERNEL_HEAP_START / KERNEL_HEAP_SIZE */
#define BLOCK_SIZE 512
#define BITMAP_SIZE (KERNEL_HEAP_SIZE / BLOCK_SIZE)

void* kmalloc(int n_blocks);
void  kfree(void* ptr, int n_blocks);   /* n_blocks must match the kmalloc call */
int get_bitmap(unsigned char* map, int index);
void set_bitmap(unsigned char* map, int index);

extern unsigned char allocation_bitmap[BITMAP_SIZE / 8];
extern int allocation_bitmap_size;

#endif
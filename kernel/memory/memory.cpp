typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "memory.h"

void* dynamic_mem_loc = (void*)KERNEL_HEAP_START;
unsigned char allocation_bitmap[BITMAP_SIZE / 8];
int allocation_bitmap_size = BITMAP_SIZE;

int get_bitmap(unsigned char* map, int index) {
    return (map[index / 8] >> (index % 8)) & 1;
}

void set_bitmap(unsigned char* map, int index) {
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
    return (void*)0;
}
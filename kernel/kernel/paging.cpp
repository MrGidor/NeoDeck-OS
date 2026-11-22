#include "../include/types.h"
#include "../include/layout.h"
#include "../memory/memory.h"
#include "paging.h"

#define PAGE_PRESENT 0x1
#define PAGE_WRITE   0x2
#define PAGE_USER    0x4

void paging_init() {
    uint32_t* page_directory = (uint32_t*)PAGE_DIRECTORY_ADDRESS;

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
    }

    for (int t = 0; t < PAGING_TABLE_COUNT; t++) {
        uint32_t* current_table = (uint32_t*)(PAGE_TABLE_0_ADDRESS + (t * 0x1000));

        for (unsigned int i = 0; i < 1024; i++) {
            uint32_t physical_address = (t * 0x400000) + (i * 4096);
            current_table[i] = physical_address | (PAGE_PRESENT | PAGE_WRITE);
        }

        page_directory[t] = ((uint32_t)current_table) | (PAGE_PRESENT | PAGE_WRITE);
    }
}

uint32_t paging_kernel_directory() { return PAGE_DIRECTORY_ADDRESS; }

bool paging_create_user_space(uint32_t app_phys, uint32_t* pd_out, uint32_t* pt_out) {
    uint32_t* pd = (uint32_t*)kmalloc_aligned(4096 / BLOCK_SIZE, 4096 / BLOCK_SIZE);
    uint32_t* pt = (uint32_t*)kmalloc_aligned(4096 / BLOCK_SIZE, 4096 / BLOCK_SIZE);
    if (!pd || !pt) {
        if (pd) kfree(pd, 4096 / BLOCK_SIZE);
        if (pt) kfree(pt, 4096 / BLOCK_SIZE);
        return false;
    }

    const uint32_t* kernel_pd = (const uint32_t*)PAGE_DIRECTORY_ADDRESS;
    for (int i = 0; i < 1024; i++) pd[i] = kernel_pd[i];

    for (int i = 0; i < 1024; i++) pt[i] = 0;
    for (uint32_t i = 0; i < APP_MEM_SIZE / 4096; i++)
        pt[i] = (app_phys + i * 4096) | (PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

    pd[APP_VIRT_BASE >> 22] = ((uint32_t)pt) | (PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

    *pd_out = (uint32_t)pd;
    *pt_out = (uint32_t)pt;
    return true;
}

void paging_destroy_user_space(uint32_t pd, uint32_t pt) {
    if (pt) kfree((void*)pt, 4096 / BLOCK_SIZE);
    if (pd) kfree((void*)pd, 4096 / BLOCK_SIZE);
}

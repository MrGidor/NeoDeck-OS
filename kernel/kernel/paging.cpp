#include "../include/types.h"
#include "../include/layout.h"

void paging_init() {
    uint32_t* page_directory = (uint32_t*)PAGE_DIRECTORY_ADDRESS;

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;   /* not present, writable */
    }

    for (int t = 0; t < PAGING_TABLE_COUNT; t++) {
        uint32_t* current_table = (uint32_t*)(PAGE_TABLE_0_ADDRESS + (t * 0x1000));

        for (unsigned int i = 0; i < 1024; i++) {
            uint32_t physical_address = (t * 0x400000) + (i * 4096);
            current_table[i] = physical_address | 3;
        }

        page_directory[t] = ((uint32_t)current_table) | 3;
    }
}

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;


#define PAGE_DIRECTORY_ADDRESS  0xE00000  /* 14 MB Mark */
#define PAGE_TABLE_0_ADDRESS    0xE01000  /* 14.004 MB Mark */

void paging_init() {
    uint32_t* page_directory = (uint32_t*)PAGE_DIRECTORY_ADDRESS;
    
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002; 
    }

    for (int t = 0; t < 12; t++) {
        uint32_t* current_table = (uint32_t*)(PAGE_TABLE_0_ADDRESS + (t * 0x1000));
        
        for (unsigned int i = 0; i < 1024; i++) {
            uint32_t physical_address = (t * 0x400000) + (i * 4096);
            current_table[i] = physical_address | 3; 
        }

        page_directory[t] = ((uint32_t)current_table) | 3;
    }
}

extern "C" void enable_paging(uint32_t page_directory_address);
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define PAGE_DIRECTORY_ADDRESS  0x9C000
#define PAGE_TABLE_0_ADDRESS    0x9D000

void paging_init() {
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

extern "C" void enable_paging(uint32_t page_directory_address);
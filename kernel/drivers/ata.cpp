typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "ata.h"
#include "io.h"

void ata_read_sector(uint32_t target_sector, uint8_t* target_buffer) {
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)target_sector);
    outb(0x1F4, (uint8_t)(target_sector >> 8));
    outb(0x1F5, (uint8_t)(target_sector >> 16));
    outb(0x1F6, 0xE0 | ((target_sector >> 24) & 0x0F));
    outb(0x1F7, 0x20);

    while ((inb(0x1F7) & 0x80));
    while (!(inb(0x1F7) & 0x08));

    uint16_t* buffer16 = (uint16_t*)target_buffer;
    for (int i = 0; i < 256; i++) {
        buffer16[i] = inw(0x1F0);
    }
}

void ata_write_sector(uint32_t target_sector, uint8_t* source_buffer) {
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)target_sector);
    outb(0x1F4, (uint8_t)(target_sector >> 8));
    outb(0x1F5, (uint8_t)(target_sector >> 16));
    outb(0x1F6, 0xE0 | ((target_sector >> 24) & 0x0F));
    outb(0x1F7, 0x30);

    while ((inb(0x1F7) & 0x80));
    while (!(inb(0x1F7) & 0x08));

    uint16_t* buffer16 = (uint16_t*)source_buffer;
    for (int i = 0; i < 256; i++) {
        asm volatile ("outw %0, %1" : : "a"(buffer16[i]), "Nd"((uint16_t)0x1F0));
    }
    
    outb(0x1F7, 0xE7); 
    while ((inb(0x1F7) & 0x80));
}

uint32_t ata_total_sectors() {
    outb(0x1F6, 0xA0);
    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC);                         // IDENTIFY

    if (inb(0x1F7) == 0) return 0;

    int spin = 0;
    while ((inb(0x1F7) & 0x80) && ++spin < 1000000);
    if (inb(0x1F4) || inb(0x1F5)) return 0;    // ATAPI / SATA signature: not a plain disk

    uint8_t status;
    spin = 0;
    while (!((status = inb(0x1F7)) & 0x09) && ++spin < 1000000);
    if (!(status & 0x08)) return 0;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(0x1F0);

    return ((uint32_t)id[61] << 16) | id[60];  // words 60-61: LBA28 sector count
}

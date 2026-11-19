#ifndef ATA_H
#define ATA_H

#include "../include/types.h"

void ata_read_sector(uint32_t target_sector, uint8_t* target_buffer);
void ata_write_sector(uint32_t target_sector, uint8_t* source_buffer);

/* Size of the primary-master drive in 512-byte sectors (ATA IDENTIFY, LBA28),
 * or 0 if no ATA disk answers. */
uint32_t ata_total_sectors();

#endif

#ifndef ATA_H
#define ATA_H

void ata_read_sector(uint32_t target_sector, uint8_t* target_buffer);
void ata_write_sector(uint32_t target_sector, uint8_t* source_buffer);

#endif
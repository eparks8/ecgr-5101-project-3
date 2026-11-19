#ifndef FLASH_WRITER_H
#define FLASH_WRITER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t buffer;
    uint32_t current_address;
    uint8_t buffer_offset;
} FlashWriter;

bool in_bounds(uint32_t addr);

bool erase_region(unsigned int start_addr, unsigned int end_addr);

void flash_writer_init(FlashWriter *writer, uint32_t start_address);

void flash_writer_push(FlashWriter *writer, uint8_t byte);

void flash_writer_flush(FlashWriter *writer);

#endif /* FLASH_WRITER_H */
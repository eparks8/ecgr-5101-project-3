#include <stdbool.h>
#include <stdint.h>
#include "stm32l4xx_hal.h"
#include "flash_writer.h"
#include <stm32l496xx.h>

#define PAGE_SIZE 0x0800U
#define FLASH_END_ADDRESS (FLASH_BASE + 0x00100000U)

bool in_bounds(uint32_t addr) {
    return (addr >= FLASH_BASE) && (addr < FLASH_END_ADDRESS);
}

bool erase_region(unsigned int start_addr, unsigned int end_addr) { /* Start at page 32, end at page 512 */
    if (!in_bounds(start_addr) || !in_bounds(end_addr) ||
        start_addr > end_addr ||
        (start_addr % PAGE_SIZE) != 0U ||
        (end_addr % PAGE_SIZE) != (PAGE_SIZE - 1U)) {
        return false;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    bool success = true;
    for (unsigned int addr = start_addr; addr <= end_addr; addr += PAGE_SIZE) {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t bank_base;
        uint32_t page_error;

        if (addr < (FLASH_BASE + 0x00080000U)) {
            bank_base = FLASH_BASE;
        } else {
            bank_base = FLASH_BASE + 0x00080000U;
        }

        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.Banks = (addr < (FLASH_BASE + 0x00080000U))
                    ? FLASH_BANK_1 : FLASH_BANK_2;
        erase.Page = (addr - bank_base) / PAGE_SIZE;
        erase.NbPages = 1U;

        if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
            success = false;
            break;
        }
    }

    (void)HAL_FLASH_Lock();
    return success;
}

static void hardware_write_doubleword(uint32_t address, uint64_t data) {
    // e.g., HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data);
}

void flash_writer_init(FlashWriter *writer, uint32_t start_address) {
    writer->buffer = UINT64_MAX; // 0xFFFFFFFFFFFFFFFF (erased flash state)
    writer->buffer_offset = 0;
    writer->current_address = start_address;
}

void flash_writer_push_byte(FlashWriter *writer, uint8_t byte) {
    // Clear the target byte slot and insert the incoming byte (little-endian)
    uint64_t mask = ~(0xFFULL << (writer->buffer_offset * 8));
    writer->buffer = (writer->buffer & mask) | ((uint64_t)byte << (writer->buffer_offset * 8));
    
    writer->buffer_offset++;

    // Once 8 bytes are accumulated, write to flash and reset
    if (writer->buffer_offset >= 8) {
        hardware_write_doubleword(writer->current_address, writer->buffer);
        writer->current_address += 8;
        writer->buffer = UINT64_MAX;
        writer->buffer_offset = 0;
    }
}

void flash_writer_flush(FlashWriter *writer) {
    // Only write if there are leftover bytes waiting in the buffer
    if (writer->buffer_offset > 0) {
        hardware_write_doubleword(writer->current_address, writer->buffer);
        writer->current_address += 8;
        writer->buffer = UINT64_MAX;
        writer->buffer_offset = 0;
    }
}
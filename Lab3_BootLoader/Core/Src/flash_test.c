#include "flash_test.h"
#include "flash_writer.h"
#include <assert.h>

#define FLASH_START 0x08010000U
#define PAGE_SIZE 0x0800U

void erase_test(void) {
    const uint32_t start_addr = 0x08010000U;
    const uint32_t end_addr = start_addr + PAGE_SIZE * 8 - 1U;
    assert(erase_region(start_addr, end_addr)); /* Pages 32-39 */
    for (uint32_t addr = start_addr; addr < end_addr; addr += sizeof(uint32_t)) {
        uint32_t flash_word = *(volatile uint32_t*)addr;
        assert(flash_word == 0xFFFFFFFFU);
    }
}

void write_test(void) {
    const uint64_t data[8] = {
        0x0000000000000000U,
        0x1111111111111111U,
        0x2222222222222222U,
        0x3333333333333333U,
        0x4444444444444444U,
        0x5555555555555555U,
        0x6666666666666666U,
        0x7777777777777777U
    };
    uint32_t start_addr = FLASH_START;
    FlashWriter writer;
    flash_writer_init(&writer, start_addr);
    for (uint8_t i = 0; i < 8; i++) {
        uint32_t write_addr = writer.current_address;
        for (uint8_t j = 0; j < 8; j++) {
            flash_writer_push(&writer, (uint8_t)((data[i] >> (j * 8)) & 0xFFU));
        }
        uint64_t flash_page = *(volatile uint64_t*)write_addr;
        assert(flash_page == data[i]);
    }
}
#ifndef BOOT_PROTOCOL_H
#define BOOT_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

/* Main-loop API. Initialize before starting USB. */
void boot_protocol_init(void);
void boot_protocol_poll(uint32_t now_ms, bool configured, bool accept_start);
bool boot_protocol_blocks_jump(void);
uint32_t boot_protocol_received(void);

#endif

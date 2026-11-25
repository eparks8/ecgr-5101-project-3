#ifndef USB_RX_H
#define USB_RX_H

#include <stdbool.h>
#include <stdint.h>

/* Holds four protocol chunks; all 1024 slots are usable. */
#define USB_RX_CAPACITY 1024U

/* Main-loop API. Clear before starting USB, or when abandoning a transfer.
 * Clear also resets the sticky overflow indication. */
void usb_rx_clear(void);
bool usb_rx_pop(uint8_t *byte);
bool usb_rx_overflowed(void);

/* USB callback only: copies bytes before the USB receive buffer is reused.
 * On insufficient space, reject the entire packet and latch overflow.
 * The protocol parser must abort a transfer if overflow is reported. */
bool usb_rx_push_from_isr(const uint8_t *data, uint32_t length);

#endif

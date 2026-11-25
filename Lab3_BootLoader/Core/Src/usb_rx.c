#include "usb_rx.h"
#include "stm32l4xx.h"

static uint8_t buffer[USB_RX_CAPACITY];
static volatile uint32_t read_index;
static volatile uint32_t write_index;
static volatile uint32_t count;
static volatile bool overflow;

void usb_rx_clear(void)
{
  uint32_t saved_mask = __get_PRIMASK();
  __disable_irq();
  read_index = 0U;
  write_index = 0U;
  count = 0U;
  overflow = false;
  __set_PRIMASK(saved_mask);
}

bool usb_rx_push_from_isr(const uint8_t *data, uint32_t length)
{
  if ((length != 0U && data == 0) || length > USB_RX_CAPACITY - count)
  {
    overflow = true;
    return false;
  }

  for (uint32_t i = 0U; i < length; ++i)
  {
    buffer[write_index] = data[i];
    write_index = (write_index + 1U) % USB_RX_CAPACITY;
  }
  count += length;
  return true;
}

bool usb_rx_pop(uint8_t *byte)
{
  if (byte == 0)
  {
    return false;
  }

  /* Protect shared count and buffer access against the USB interrupt.
   * Restore the caller's interrupt state rather than blindly enabling IRQs. */
  uint32_t saved_mask = __get_PRIMASK();
  __disable_irq();
  bool available = count != 0U;
  if (available)
  {
    *byte = buffer[read_index];
    read_index = (read_index + 1U) % USB_RX_CAPACITY;
    --count;
  }
  __set_PRIMASK(saved_mask);
  return available;
}

bool usb_rx_overflowed(void)
{
  return overflow;
}

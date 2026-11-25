#include "boot_protocol.h"
#include "usb_rx.h"
#include "usbd_cdc_if.h"
#include "usbd_cdc.h"

#define MAX_IMAGE_SIZE 983040U
#define CHUNK_SIZE 256U
#define INPUT_TIMEOUT_MS 5000U
#define RESPONSE_TIMEOUT_MS 30000U

typedef enum { WAIT_START, READ_LENGTH, READ_DATA, COMPLETE, FAILED } State;
static State state;
static uint32_t marker_bytes, length_bytes, image_size, received, chunk_bytes;
static uint32_t last_input_ms, response_start_ms;
/* Persistent storage: USB retains this pointer until IN transfer completes. */
static uint8_t response[6];
static uint16_t response_length;
static bool response_submitted;
static bool input_timer_restart;
extern USBD_HandleTypeDef hUsbDeviceFS;

static void queue_response(uint8_t value, uint32_t now_ms)
{
  response[0] = value;
  response_length = 1U;
  response_submitted = false;
  response_start_ms = now_ms;
}

static void fail_transfer(uint32_t now_ms)
{
  state = FAILED;
  usb_rx_clear();
  /* Never overwrite a buffer owned by USB. A pending ACK is completed first;
   * host timeout still reports failure if the link cannot deliver a NAK. */
  if (response_length == 0U)
  {
    queue_response(0x15U, now_ms);
  }
}

void boot_protocol_init(void)
{
  state = WAIT_START;
  marker_bytes = length_bytes = image_size = received = chunk_bytes = 0U;
  last_input_ms = response_start_ms = 0U;
  response_length = 0U;
  response_submitted = input_timer_restart = false;
}

static void consume(uint8_t byte, uint32_t now_ms)
{
  static const uint8_t marker[] = { 'B', 'O', 'O', 'T' };
  last_input_ms = now_ms;
  if (state == WAIT_START)
  {
    if (byte == marker[marker_bytes])
    {
      ++marker_bytes;
    }
    else
    {
      marker_bytes = byte == marker[0] ? 1U : 0U;
    }
    if (marker_bytes == sizeof(marker))
    {
      state = READ_LENGTH;
    }
  }
  else if (state == READ_LENGTH)
  {
    image_size |= (uint32_t)byte << (8U * length_bytes++);
    if (length_bytes == 4U)
    {
      if (image_size == 0U || image_size > MAX_IMAGE_SIZE)
      {
        fail_transfer(now_ms);
      }
      else
      {
        state = READ_DATA;
        queue_response(0x06U, now_ms);
      }
    }
  }
  else if (state == READ_DATA)
  {
    /* Part 2 counts only. Part 5 will feed this byte to the flash writer. */
    ++received;
    ++chunk_bytes;
    if (received == image_size || chunk_bytes == CHUNK_SIZE)
    {
      chunk_bytes = 0U;
      queue_response(0x06U, now_ms);
      if (received == image_size)
      {
        state = COMPLETE;
        response[1] = 0x04U;
        for (uint32_t i = 0U; i < 4U; ++i)
        {
          response[2U + i] = (uint8_t)(received >> (8U * i));
        }
        response_length = 6U; /* Last ACK followed by DONE and count. */
      }
    }
  }
}

void boot_protocol_poll(uint32_t now_ms, bool configured, bool accept_start)
{
  if (!configured)
  {
    if (state == READ_LENGTH || state == READ_DATA || response_length != 0U)
    {
      state = FAILED;
    }
    /* Keep any USB-owned response untouched; this session requires reset. */
    usb_rx_clear();
    marker_bytes = 0U;
    return;
  }

  if (usb_rx_overflowed())
  {
    fail_transfer(now_ms);
  }

  if (response_length != 0U)
  {
    USBD_CDC_HandleTypeDef *cdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (cdc != 0)
    {
      if (!response_submitted)
      {
        response_submitted = CDC_Transmit_FS(response, response_length) == USBD_OK;
      }
      else if (cdc->TxState == 0U)
      {
        response_length = 0U;
        input_timer_restart = true;
        if (state == FAILED && response[0] != 0x15U)
        {
          queue_response(0x15U, now_ms);
        }
      }
    }
    if (now_ms - response_start_ms >= RESPONSE_TIMEOUT_MS)
    {
      state = FAILED;
    }
    return; /* No input timeout while responding, no buffer reuse. */
  }

  if (state == FAILED || state == COMPLETE)
  {
    usb_rx_clear();
    return;
  }
  if (state == WAIT_START && !accept_start)
  {
    marker_bytes = 0U;
    usb_rx_clear();
    return;
  }
  if (input_timer_restart)
  {
    last_input_ms = now_ms;
    input_timer_restart = false;
  }

  uint8_t byte;
  /* Bounded work per poll so button/LED servicing is not starved. */
  for (uint32_t i = 0U; i < USB_RX_CAPACITY && usb_rx_pop(&byte); ++i)
  {
    consume(byte, now_ms);
    if (response_length != 0U || state == FAILED)
    {
      break;
    }
  }
  if ((state == READ_LENGTH || state == READ_DATA) &&
      response_length == 0U && now_ms - last_input_ms >= INPUT_TIMEOUT_MS)
  {
    fail_transfer(now_ms);
  }
}

bool boot_protocol_blocks_jump(void)
{
  return state == READ_LENGTH || state == READ_DATA || state == FAILED ||
         response_length != 0U;
}

uint32_t boot_protocol_received(void)
{
  return received;
}

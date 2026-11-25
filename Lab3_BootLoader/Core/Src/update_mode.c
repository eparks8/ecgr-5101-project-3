#include "update_mode.h"

static bool window_started;
static bool window_open;
static bool led_on;
static uint32_t window_start_ms;

void update_mode_init(void)
{
  window_started = false;
  window_open = false;
  led_on = false;
  window_start_ms = 0U;
}

void update_mode_poll(uint32_t now_ms, bool usb_configured)
{
  if (!window_started && usb_configured)
  {
    window_started = true;
    window_open = true;
    window_start_ms = now_ms;
  }

  if (window_open)
  {
    /* Unsigned subtraction also handles HAL_GetTick() wrapping around. */
    uint32_t elapsed = now_ms - window_start_ms;
    if (elapsed >= UPDATE_WINDOW_MS)
    {
      window_open = false;
    }
    led_on = window_open && usb_configured &&
             ((elapsed / UPDATE_LED_INTERVAL_MS) % 2U == 0U);
  }
  else
  {
    led_on = false;
  }
}

bool update_mode_accepting_start(void)
{
  return window_open;
}

bool update_mode_led_on(void)
{
  return led_on;
}

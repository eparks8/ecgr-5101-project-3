#ifndef UPDATE_MODE_H
#define UPDATE_MODE_H

#include <stdbool.h>
#include <stdint.h>

#define UPDATE_WINDOW_MS 10000U
#define UPDATE_LED_INTERVAL_MS 250U

/* Main-loop only. The first configured USB connection opens one window
 * per reset. USB reconnection must not silently reopen an expired window. */
void update_mode_init(void);
void update_mode_poll(uint32_t now_ms, bool usb_configured);
bool update_mode_accepting_start(void);
bool update_mode_led_on(void);

#endif

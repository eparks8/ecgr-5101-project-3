#include "main.h"
#include "flash_writer.h"
#include "flash_test.h"
#include "usb_device.h"
#include "usbd_core.h"
#include "usb_rx.h"
#include "update_mode.h"
#include "boot_protocol.h"

/* ---- Requirement 4: jump to the application ------------------------------ */
#define APP_ADDRESS  0x08010000UL   /* Must match ORIGIN in Lab3_Application's linker script */

extern USBD_HandleTypeDef hUsbDeviceFS;
void JumpToApplication(void);
void SystemClock_Config(void);
void MX_GPIO_Init(void);
/* ------------------------------------------------------------------------- */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  usb_rx_clear();
  update_mode_init();
  boot_protocol_init();
  MX_USB_DEVICE_Init();   /* Starts USB enumeration - your laptop will show a new
                              virtual COM port once this call returns. */

  /* The update window starts after USB configuration, not before enumeration.
     LD3 blinks while a start marker may be accepted. */
   erase_test();
   write_test();
  while (1)
  {
    update_mode_poll(HAL_GetTick(),
                     hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED);
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin,
                      update_mode_led_on() ? GPIO_PIN_SET : GPIO_PIN_RESET);

    boot_protocol_poll(HAL_GetTick(),
                       hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED,
                       update_mode_accepting_start());

    /* TODO (Requirement 2): Once you have erased the application's flash
       region and accumulated a full aligned double word of incoming bytes,
       unlock the flash controller and program that double word starting at
       0x08010000. Repeat until the entire image has been written, then lock
       the flash controller again. */

    /* Requirement 4: User button (B1, PC13) leaves the bootloader and starts
       the application. B1 reads high while pressed on the Nucleo-144. */
    if (!update_mode_accepting_start() &&
        !boot_protocol_blocks_jump() &&
        HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_SET)
    {
      JumpToApplication();
    }
  }
}

/* Requirement 4: hand the CPU over to the application at APP_ADDRESS.
   Puts the chip back close to its reset state first, so the application starts
   the same way it would after a real reset. Returns only if no valid
   application is found (e.g. that flash region is erased). */
void JumpToApplication(void)
{
  /* static, not local: these must survive the stack pointer being moved below. */
  static uint32_t appStack;
  static void (*appResetHandler)(void);

  /* An application's vector table starts with its initial stack pointer
     (word 0) followed by the address of its reset handler (word 1). */
  appStack        = *(volatile uint32_t *)APP_ADDRESS;
  appResetHandler = (void (*)(void))(*(volatile uint32_t *)(APP_ADDRESS + 4U));

  /* Erased flash reads 0xFFFFFFFF - only jump if the stack points into SRAM
     (0x20000000 .. 0x20050000 covers SRAM1 + SRAM2 on the STM32L496). */
  if (appStack < 0x20000000UL || appStack > 0x20050000UL)
  {
    return;
  }

  /* Shut down what the bootloader started, while SysTick still runs
     (the HAL de-init functions use it for timeouts). */
  USBD_DeInit(&hUsbDeviceFS);   /* Stop USB so its interrupt can't fire in the app */
  HAL_RCC_DeInit();             /* Back to the 4 MHz MSI reset clock the app expects */
  HAL_DeInit();

  __disable_irq();

  /* Stop SysTick and clear anything still pending. */
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

  for (uint32_t i = 0; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFUL;   /* disable */
    NVIC->ICPR[i] = 0xFFFFFFFFUL;   /* clear pending */
  }

  /* Relocate the vector table and load the application's stack pointer. */
  SCB->VTOR = APP_ADDRESS;
  __set_MSP(appStack);
  __DSB();
  __ISB();

  /* The application never re-enables interrupts itself, so do it here -
     nothing is enabled in the NVIC now, so nothing can fire early. */
  __enable_irq();

  appResetHandler();   /* Branch to the application - does not return. */

  while (1) { }
}

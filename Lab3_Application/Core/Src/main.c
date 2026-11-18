#include "main.h"

#define FAST_BLINK 0   /* 0 = slow blink (original firmware), 1 = fast blink (updated firmware) */

#if FAST_BLINK
#define BLINK_DELAY_MS 100
#else
#define BLINK_DELAY_MS 500
#endif

void SystemClock_Config(void);
static void MX_GPIO_Init(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  while (1)
  {
    HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
    HAL_Delay(BLINK_DELAY_MS);
  }
}

void SystemClock_Config(void)
{
  /* This minimal application intentionally skips configuring the PLL and
     simply runs on the MCU's default reset clock (MSI). HAL_Delay() is
     still timed correctly against whatever clock is actually running, so
     the blink rate above is accurate regardless. */
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin   = LD1_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD1_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
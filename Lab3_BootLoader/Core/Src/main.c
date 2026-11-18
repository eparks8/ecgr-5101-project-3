int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USB_DEVICE_Init();   /* Starts USB enumeration - your laptop will show a new
                              virtual COM port once this call returns. */

  /* TODO (Requirement 1): Decide here how the bootloader knows a new firmware
     image is about to arrive, versus just sitting idle until the user button
     is pressed (Requirement 4). A timeout window measured from this point,
     or a flag left behind by the application, are both reasonable options. */

  while (1)
  {
    /* TODO (Requirement 1, continued): Incoming USB bytes do not show up here
       automatically - they arrive in CDC_Receive_FS(), inside
       USB_DEVICE/App/usbd_cdc_if.c, which CubeMX already generated for you.
       You will need a way to get bytes from that callback into this loop
       (for example, a shared buffer and a flag), or do your flash
       programming directly from within that callback instead. Either way,
       figure out how you'll recognize when the full image has arrived. */

    /* TODO (Requirement 2): Once you have erased the application's flash
       region and accumulated a full aligned double word of incoming bytes,
       unlock the flash controller and program that double word starting at
       0x08010000. Repeat until the entire image has been written, then lock
       the flash controller again. */

    /* TODO (Requirement 4): Check whether the User button (B1_Pin on GPIOC)
       is pressed. If it is, relocate the vector table with SCB->VTOR, reset
       the stack pointer using the application's initial MSP value, and
       branch to the application's reset handler at 0x08010000 - this does
       not require a completed firmware transfer first. */
  }
}
# Riverdi RVT70HSSNWC00-B

This board target runs on the STM32H757XI Cortex-M7 and uses the first bank
of internal flash. The hardware configuration comes from the working
`Riverdi70_Test` TouchGFX project. Its CubeMX file selects the H757,
the Cortex-M7 startup code selects the LDO supply and a 16 MHz HSE in
bypass mode, and the GPIO setup maps `USR_LED_1` to PJ10. Zephyr Blinky
drives PJ10 directly through the `led0` alias. The devicetree assumes
active-high LED polarity.

From the west workspace root, build Zephyr's Blinky sample with the
STM32CubeCLT GNU Arm toolchain.

```powershell
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'gnuarmemb'
$env:GNUARMEMB_TOOLCHAIN_PATH = 'C:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32'
.\.venv\Scripts\west.exe build -b riverdi_70stm32h7/stm32h757xx/m7 -d build/riverdi_blinky zephyr/samples/basic/blinky
```

Connect the Riverdi SWD header to a J-Link. Substitute the serial number
of the probe attached to the Riverdi if it differs from the one below.
Flashing writes the Blinky image over the Cortex-M7 application in
internal flash.

```powershell
.\.venv\Scripts\west.exe flash -d build/riverdi_blinky -r jlink --dev-id 941000024 --no-erase
```

USART1 provides a 115200 baud console on PA9 (TX) and PA10 (RX).

The next bring-up milestones are ARDEP UDS over the Riverdi CAN
transceiver, the LVGL display and touch stack, and an MCUboot partition
layout and image-signing flow.

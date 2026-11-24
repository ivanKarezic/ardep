# SLHx Advanced Charger

The LVGL dashboard shows battery data received over UDS. Use **Start
charge**, **Pause charge**, **Resume charge**, or **Reset** to control the
bench session. The desktop truck simulator supplies SoC, voltage, current,
temperature, warnings, and faults.

The 1024 × 600 MIPI DSI panel, SDRAM framebuffer, and I²C touchscreen use the
hardware configuration from the local `Riverdi70_Test` TouchGFX project.
The display configuration is loaded by this sample's overlay.
The LTDC scans out an RGB888 framebuffer at 41.666 MHz, matching the pixel
clock used for DSI burst timing with a 62 MHz lane byte clock. TIM15 drives
the backlight at 48 kHz.
FMC selects the 224 MHz PLL2-R output and divides it by two for the 112 MHz
SDRAM clock.

Run from the west workspace root. Adjust the compiler path and J-Link serial
if your setup differs.

```powershell
.\.venv\Scripts\west.exe update
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'gnuarmemb'
$env:GNUARMEMB_TOOLCHAIN_PATH = 'C:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32'
.\.venv\Scripts\west.exe build -b riverdi_70stm32h7/stm32h757xx/m7 -d build/riverdi_charger ardep/samples/riverdi_charger
.\.venv\Scripts\west.exe flash -d build/riverdi_charger -r jlink --dev-id 941000024 --no-erase
```

The embedded `jungheinrich.rgb565` image is a 256 × 39 pixel conversion of the
company's wordmark. [Jungheinrich official website](https://www.jungheinrich.com/en)

## STM32H7B3I-EVAL with MB1370-LCD-B02

Build the same application with board target `stm32h7b3i_eval`. This target
uses the 800 × 480 Rocktech RGB panel, the B02 Goodix touchscreen on I²C2,
an SDRAM framebuffer, and the board's FDCAN1 transceiver. The app scales the
same dashboard and overlays to the smaller display. The board target assumes
the factory direct-SMPS configuration, the 24 MHz HSE, and the default
backlight and touch wiring. Connect the display FFC at CN23 and the touch
and display power cable at CN30.

For the first signed SWD installation, create a separate ECDSA P-256
development key and sysbuild configuration for this board. Use the same
three MCUboot settings as below and point the key setting to the EVAL key.

```powershell
.\.venv\Scripts\imgtool.exe keygen -k build/h7b3i_eval_dev.pem -t ecdsa-p256
.\.venv\Scripts\imgtool.exe getpub -e pem -k build/h7b3i_eval_dev.pem -o build/h7b3i_eval_dev_public.pem
$conf = (Resolve-Path 'build/h7b3i_eval_signing.conf').Path.Replace('\','/')
.\.venv\Scripts\west.exe build --sysbuild -b stm32h7b3i_eval -d build/h7b3i_eval_charger ardep/samples/riverdi_charger -- "-DSB_CONF_FILE=$conf" "-Driverdi_charger_EXTRA_CONF_FILE=ota.conf"
.\.venv\Scripts\west.exe flash -d build/h7b3i_eval_charger -r stm32cubeprogrammer --no-erase
```

Use the onboard ST-LINK USB connector CN21, or select `-r jlink` when an
external J-Link is connected to the EVAL. The EVAL and Riverdi share the
UDS profile and update slot addresses. Select the EVAL signed binary and its
public key when updating that board through Charger Tool. Use one target
per bench bus with the default CAN IDs.

On EVAL CAN connector CN22, use pin 7 for CAN-H, pin 2 for CAN-L, and pin 3
for GND. Check that the CAN-routing bridges SB35 and SB36 are fitted, and
use a terminated 500 kbit/s bus.
[ST UM2662](https://www.st.com/resource/en/user_manual/um2662-evaluation-board-with-stm32h7b3li-mcu-stmicroelectronics.pdf) ·
[MB1370-LCD-B02 schematic](https://www.st.com/resource/en/schematic_pack/mb1370-lcd-b02-schematic.pdf) ·
[STM32H7B3I-EVAL BSP](https://github.com/STMicroelectronics/stm32h7b3i-eval-bsp)

## STM32F769I-DISCO with B-LCD40-DSI1

Build the application for the STM32F769I Discovery board and the on-board
800 x 480 DSI LCD:

```bash
west build -p always -b stm32f769i_disco -d build/riverdi_f769 ardep/samples/riverdi_charger
```

The build uses the `st_b_lcd40_dsi1_mb1166` shield and CAN1 at 500 kbit/s.
Use CAN1 pins PB8 (RX) and PB9 (TX).
The F769 MCUboot layout uses a 256 KiB bootloader, 768 KiB per image slot,
and a 256 KiB scratch partition, filling the 2 MiB internal flash.

For an unsigned MCUboot build with the scratch swap layout:

```bash
west build --sysbuild -p always -b stm32f769i_disco \
  -d build/riverdi_f769_mcuboot ardep/samples/riverdi_charger \
  -- "-DSB_CONF_FILE=/absolute/path/to/ardep/samples/riverdi_charger/sysbuild/f769_mcuboot.conf" \
     "-DSHIELD=st_b_lcd40_dsi1_mb1166" \
     "-Driverdi_charger_EXTRA_CONF_FILE=ota.conf"
```

Flash the unsigned MCUboot build with J-Link after connecting the board:

```bash
west flash -d build/riverdi_f769_mcuboot -r jlink --no-erase
```

## CAN and desktop console

Connect the desktop adapter to **CAN1 (P11)** using pin 1 for GND, pin 2
for CAN-L, and pin 3 for CAN-H. Leave pin 4 (the display power input)
open when powering the Riverdi separately. Use a correctly terminated bench
bus. The Riverdi uses classic
CAN at 500 kbit/s and 11-bit ISO-TP addressing (request `0x7E0`, response
`0x7E8`). The Electron console lives in the separate
`slhx-charger-tool` repository. Follow its README to set up the app
and the PEAK PCAN, Vector XL, or SocketCAN backend.
[Riverdi datasheet](https://download.riverdi.com/Archive/RVT70HSSNWC00-B/DS_RVT70HSSNWC00-B_Rev.1.2.pdf)

**Read target** checks the diagnostic profile and reads the current sample.
**Send sample** writes the six battery fields. **Start simulation** sends
samples periodically and advances SoC when current is positive and no fault
is set. The Riverdi shows the values with fault/warning priority and marks
the data stale after five seconds. This bench interface controls simulated
values only.

| DID | Access | Payload |
| --- | --- | --- |
| F200 | Read and write in default session | Six big-endian 16-bit values. SoC in permille, voltage in decivolts, signed current in deciamps, signed temperature in deci-degrees Celsius, warning mask, fault mask. |
| F201 | Read | ASCII `RVC1` followed by one byte. 0 means telemetry only and 1 means firmware update support. |
| F189 | Read | ASCII charger firmware version. |
| F202 | Read and write | One byte. 0 disconnects, 1 selects EJC 216, 2 EFG 216, 3 ETV 216i, 4 EFG 425, and 5 Custom. |
| F203 | Read and write | Read one byte of simulated indicator states. Bit 0 is Power stage and bit 1 is PM Enable. Write two bytes, a change mask and new values for the masked bits. Both LEDs start off after reset. |
| F204 | Read and write | Read one byte (0 ready, 1 charging, 2 paused, 3 complete). Write 1 to start or resume, 0 to pause. The target rejects Start without a selected truck and ends the session when the truck changes or disconnects. |

The dashboard shows the simulated truck selected in Charger Tool. Incoming
warning and error bits open an overlay. Acknowledge it by touch or open
**View details**. The **Warnings and errors** page keeps active and cleared
flags for the current boot, with one description per diagnostic code.

## Signed CAN update

Provision MCUboot and the initial signed application over SWD once. The
layout reserves 128 KiB for MCUboot, 640 KiB per image slot and 128 KiB
for swap scratch. The secondary slot uses the start of internal bank 2.
Save any Cortex-M4 firmware stored there before using the updater. External
QSPI is outside this layout.

Use a private ECDSA P-256 key under your control. Keep the key and absolute
path in an untracked sysbuild configuration file, for example
`build/riverdi_signing.conf`.

```kconfig
SB_CONFIG_BOOTLOADER_MCUBOOT=y
SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y
SB_CONFIG_MCUBOOT_MODE_SWAP_SCRATCH=y
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="D:/path/to/private-key.pem"
```

From the workspace root, use the compiler environment shown above.

```powershell
.\.venv\Scripts\west.exe build --sysbuild -b riverdi_70stm32h7/stm32h757xx/m7 -d build/riverdi_signed ardep/samples/riverdi_charger -- "-DSB_CONF_FILE=$((Resolve-Path build/riverdi_signing.conf).Path.Replace('\','/'))" -Driverdi_charger_EXTRA_CONF_FILE=ota.conf
.\.venv\Scripts\west.exe flash -d build/riverdi_signed -r jlink --dev-id 941000024 --no-erase
```

For a CAN update, bump the sample `VERSION` and rebuild with the same
signing key. In the console choose the new `zephyr.signed.bin` from
`build/riverdi_signed/riverdi_charger/zephyr` and the signing public key.
Export the public key with `imgtool getpub -e pem -k private-key.pem -o public-key.pem`.

The console follows the pre-flash, programming, and post-flash structure
used by vFlash projects. This Riverdi profile verifies the signed file and
target identity, enters session `0x10 02`, erases only the secondary slot
(`0x31 FF00`), then uses `0x34`, `0x36`, and `0x37` to transfer the
image. It checks the staged CRC (`0x31 FF01`), resets with `0x11 01`, and
checks the reported application version. MCUboot verifies the signature
before activation. The app confirms the trial image after display and CAN
initialization. Use this profile on an isolated bench CAN bus.
[Vector vFlash](https://www.vector.com/en/product/vflash/)

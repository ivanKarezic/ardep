# Riverdi forklift charger prototype

The LVGL dashboard shows a simulated 48 V battery charging session. Use
**Start charge**, **Pause charge**, **Resume charge**, or **Reset demo**.
The demo starts at 64% and advances by one percentage point every 1.5 seconds
while charging. The voltage, current, and time remaining are simulated.
The prototype does not send charger commands.

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

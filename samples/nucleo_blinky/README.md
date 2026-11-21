# NUCLEO-F302R8 Blinky

The sample uses Zephyr's `led0` devicetree alias to toggle the Nucleo's green
user LED (LD2 on PB13) every 500 ms.

From the west workspace root, build with the local Python environment and
the GNU Arm compiler shipped with STM32CubeCLT. Adjust the toolchain path
if STM32CubeCLT is installed elsewhere.

```powershell
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'gnuarmemb'
$env:GNUARMEMB_TOOLCHAIN_PATH = 'C:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32'
.\.venv\Scripts\west.exe build -b nucleo_f302r8 -d build/nucleo_blinky ardep/samples/nucleo_blinky
```

Connect the board's USB debug port. For the onboard probe configured as
J-Link, use the J-Link runner and select the probe by serial number.
Replace `773971752` if using a different probe.

```powershell
.\.venv\Scripts\west.exe flash -d build/nucleo_blinky -r jlink --dev-id 773971752
```

For a probe running the standard ST-LINK firmware, use the default
STM32CubeProgrammer runner.

```powershell
.\.venv\Scripts\west.exe flash -d build/nucleo_blinky
```

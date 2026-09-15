# How to build and flash the boards (plan section 2)

You never run a separate "rebuild" step. One command compiles the firmware and writes it to the board over USB. PlatformIO works out by itself what needs compiling again.

Test results: [`hwtest/test-report.md`](hwtest/test-report.md). Test plan: [`Wio-SX1262-hardware-test-plan.md`](Wio-SX1262-hardware-test-plan.md).

## Current state (2026-09-15, after the first hardware tests)

The hardware tests found two driver bugs. Both are fixed in the working tree but **not committed yet**:

| File | Fix |
|---|---|
| `src/lib/SX126xDriver/SX126x_hal.cpp` | `WaitOnBusyLong()` waits 10 µs before it first checks BUSY. It used to return too early after the image calibration, so the next commands were lost (among them the fallback mode, so the TCXO switched off after every packet). |
| `src/lib/SX126xDriver/SX126x.cpp` | `SetFrequencyReg()` changes the frequency from STDBY_XOSC. In FS or RX mode the SX1262 kept the old frequency, so the link lost every packet after the first hop. |
| `src/python/build_env_setup.py` | ESP32-S3 uploads use PlatformIO's esptool v4.9.0. The old v4.2.1 is unreliable on the XIAO's USB port. |

Temporary diagnostics, only in builds with `-DDEBUG_LOG`, to be removed before the final commit:

- A PLL lock test at boot: `FS test DCDC ...` and `FS test LDO ...`.
- BUSY timeouts wait up to 30 ms and report the command: `SX126x BUSY <us> us after cmd 0x..`.
- `Timeout! #n status .. irq .. err .. dio1 .. freq ..` on the TX.

Both boards now run a build with these flags:

```powershell
$env:PLATFORMIO_BUILD_FLAGS = '-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RX_SCOREBOARD'
```

`DEBUG_RX_SCOREBOARD` makes the RX print one character per packet (`R` received, `_` missed, `.` CRC error, `s` sync, `T` telemetry sent) and turns off its CRSF output to a flight controller. For the standard debug build, set the flags from step 3 and run steps 5 and 6 again.

The TX keeps its packet rate, telemetry ratio and power in flash, also after a new upload. Check them with `& $py $mon --port "TX=$TXSER" --params --exit`.

## What one build command means

```powershell
& $pio run -e Unified_ESP32S3_SX126X_TX_via_UART -t upload --upload-port $TX
```

| Part | Meaning |
|---|---|
| `& $pio` | Runs PlatformIO. It isn't on your PATH, so `$pio` holds its full path, and `&` runs a program named by a variable. |
| `run -e Unified_ESP32S3_SX126X_TX_via_UART` | Builds the TX firmware for an ESP32-S3 with an SX126x radio. For the RX, use `..._RX_via_UART`. "via_UART" just means "flash with esptool over the serial port", which on the XIAO is the USB-C cable. |
| `-t upload` | Compiles first if needed, then writes the firmware to the board. Without it, the command only builds. |
| `--upload-port $TX` | The board to flash. It's needed because both XIAOs have the same USB ID. |

Three settings decide what ends up inside the firmware:

- `$env:ELRS_UNIFIED_CONFIG` picks the board definition from `src/hardware/targets.json`: the pins, the name, TX or RX. The build adds it to the end of `firmware.bin`.
- `src/user_defines.txt` holds the permanent options, such as the EU868 domain. `src/super_defines.txt` holds your local additions, here the binding phrase. Git ignores it.
- `$env:PLATFORMIO_BUILD_FLAGS` adds extra flags for this PowerShell window only, here the debug flags.

Anything set with `$env:...` exists only in the current PowerShell window. If you close the window, set it again.

## Step by step, first time

### Step 1: Open PowerShell and set the variables

Run the block from plan section 1.4, so `$pio`, `$py`, `$mon`, `$TX` and the others exist. Run it in every PowerShell window you use.

```powershell
Set-Location C:\Work\RF-RC\ExpressLRS\ExpressLRS\src
$pio  = 'C:\Users\bonev\.platformio\penv\Scripts\pio.exe'
$py   = 'C:\Users\bonev\.platformio\penv\Scripts\python.exe'
$mon  = 'C:\Work\RF-RC\ExpressLRS\ExpressLRS\Docs\hwtest\elrs_usbmon.py'
$logs = 'C:\Work\RF-RC\ExpressLRS\ExpressLRS\Docs\hwtest\logs'
$scan = 'C:\Work\RF-RC\ExpressLRS\ExpressLRS\Docs\hwtest\rtl_power_scan.py'
$rtl  = 'C:\Tools\rtl-sdr'   # folder with rtl_power.exe (section 1.7)
$esptool = 'C:\Users\bonev\.platformio\packages\tool-esptoolpy\esptool.py'   # esptool v4.9.0 (section 2.3)
New-Item -ItemType Directory -Force $logs | Out-Null
$TX = 'COM6'; $TXSER = '47:A0'   # board A (serial ...47:A0) is the TX
$RX = 'COM9'; $RXSER = '47:B0'   # board B (serial ...47:B0) is the RX
```

### Step 2: Create the binding phrase file (once)

This writes `src\super_defines.txt`, which gives both boards the same ID:

```powershell
Set-Content -Path super_defines.txt -Encoding ascii -Value '# local test settings, not committed', '-DMY_BINDING_PHRASE="wio-sx1262-test"'
```

### Step 3: Set the debug flags for this window

```powershell
$env:PLATFORMIO_BUILD_FLAGS = '-DDEBUG_LOG -DDEBUG_TX_FREERUN'
```

### Step 4: Erase both boards (first time only)

This removes old settings stored in flash:

```powershell
& $py $esptool --chip esp32s3 --port $TX erase_flash
& $py $esptool --chip esp32s3 --port $RX erase_flash
```

Each command should end with `Chip erase completed successfully`.

### Step 5: Build and flash the TX

```powershell
$env:ELRS_UNIFIED_CONFIG = 'diy.tx_900.xiao_s3_wio_sx1262'
& $pio run -e Unified_ESP32S3_SX126X_TX_via_UART -t upload --upload-port $TX
```

It takes about 2 minutes. You'll see, in order:

- `UID bytes: a,b,c,d,e,f`. Note the numbers.
- Many `Compiling ...` lines.
- `RAM: ... 20.5%` and `Flash: ... 75.5%`.
- esptool: `esptool.py v4.9.0`, `Connecting...`, `Chip is ESP32-S3`, `Writing at 0x...` with a percentage, `Hash of data verified.`, and `Hard resetting via RTS pin...`.
- At the end, `[SUCCESS]`. The board restarts with the new firmware.

### Step 6: Build and flash the RX

Change the board definition first; this is the step people most often forget:

```powershell
$env:ELRS_UNIFIED_CONFIG = 'diy.rx_900.xiao_s3_wio_sx1262'
& $pio run -e Unified_ESP32S3_SX126X_RX_via_UART -t upload --upload-port $RX
```

The `UID bytes:` numbers must be the same as for the TX.

## When to run it again

| Situation | What to do |
|---|---|
| You changed code or pulled new commits | Run steps 5 and 6 again. PlatformIO compiles only what changed. |
| You changed `PLATFORMIO_BUILD_FLAGS` | Steps 5 and 6. Everything recompiles, about 2 minutes per board. |
| RX packet scoreboard | Set the flags to `-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RX_SCOREBOARD`, then steps 5 and 6. |
| Release build (T12) | Run `Remove-Item Env:PLATFORMIO_BUILD_FLAGS`, then steps 5 and 6. |
| You closed PowerShell | Repeat steps 1 and 3 before building. |
| Build only, no flashing | Leave out `-t upload --upload-port ...`. The result is `.pio\build\<env>\firmware.bin`. |

## Common problems

| Problem | Fix |
|---|---|
| A numbered product menu appears (`0) Leave bare ...`) | `ELRS_UNIFIED_CONFIG` isn't set. Press Ctrl+C, set it, and run again. |
| `could not open port 'COM6' ... Access is denied` | The monitor tool (or another program) has the port open. Stop it with Ctrl+C. |
| `Failed to connect to ESP32-S3` | Unplug the board, hold BOOT, plug it in, release BOOT, and run again. The COM number may change, so check with `& $py $mon --list`. |
| esptool stops with `StopIteration` after `Stub running...` | The old esptool v4.2.1. Use `$esptool` (v4.9.0) for erasing. Uploads now use v4.9.0 by themselves. |
| `[FAILED]` with `.sconsign311.dblite: No such file` | A one-time glitch after changing flags. Run the same command again. |
| `'pio' is not recognized` | Use `& $pio`, not `pio`, and make sure step 1 ran in this window. |
| `unknown option --port` from Python | `$mon` is empty in this window. Run the block from step 1. |

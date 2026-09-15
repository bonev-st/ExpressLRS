# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

- `src/` is the PlatformIO firmware project. Run every `pio` command from `src/`.
  - `src/src/`: the TX and RX entry points (`tx_main.cpp`, `rx_main.cpp`) and shared code.
  - `src/lib/`: one PlatformIO library per subsystem (radio drivers, OTA, FHSS, CONFIG, DEVICE, ...).
  - `src/include/`: shared headers. `src/targets/*.ini`: the build environments.
  - `src/html/`: web UI (Vite + Lit). `src/lua/elrs.lua`: handset Lua script. `src/python/`: build, upload and flasher scripts.
- `src/hardware/` is a separate git checkout, ignored by this repo. On the first ESP build, `python/build_env_setup.py` clones it from the fork `github.com/bonev-st/ExpressLRS_hardware` (remote `upstream` = `ExpressLRS/targets`). It holds `targets.json` and the per-board pin layouts. Commit layout changes inside `src/hardware` and push them to the fork. Delete the directory to clone it again.
- Git: branch `Wio-SX1262` sits on upstream ExpressLRS `master`. `origin/main` is an unrelated one-commit snapshot with no shared history, so never diff against it or use it as a PR base.

## Build, flash, test

On this PC PlatformIO is not on `PATH`. Use `C:\Users\bonev\.platformio\penv\Scripts\pio.exe`, and that folder's `python.exe` for the scripts in `src/python` and `Docs/hwtest`.

```powershell
cd src
pio run -e Unified_ESP32S3_SX126X_TX_via_UART                               # build only
pio run -e Unified_ESP32S3_SX126X_TX_via_UART -t upload --upload-port COM6  # build and flash
pio run -e <env> -t uploadforce   # flash even if the target name does not match
pio run -e <env> -t clean         # also forgets the remembered product
```

Unit tests, as CI runs them (bash syntax):

```sh
PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" pio test -e native               # all suites
PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" pio test -e native -f test_fhss  # one suite
```

- Environment names are `Unified_<ESP32|ESP32S3|ESP32C3|ESP8285>_<2400|900|SX126X|LR1121|LR2021>_<TX|RX>_via_<UART|WIFI|ETX|BetaflightPassthrough>`. The `_via_*` suffix only selects the upload method; the firmware is the same.
- Native tests need a host `g++`. This PC has none, so here every test reports ERRORED at the build step; that is not a code failure. CI (`.github/workflows/build.yml`) runs them on Ubuntu, builds every `*_UART` env, and fails if the committed web headers differ from a fresh `npm run build:all`.
- Suites live in `src/test/test_*` (Unity). `[env:native]` builds with `UNIT_TEST`, `TARGET_NATIVE` and C++11. `include/native.h` fakes Arduino and an SX1280. `test_embedded` runs only on hardware.

### Choosing the board (unified targets)

One binary serves every board of an MCU and radio family. After linking, `python/UnifiedConfiguration.py` appends the product name, the device name, the options JSON and the hardware layout JSON to `firmware.bin`.

- Set `ELRS_UNIFIED_CONFIG` to a path into `src/hardware/targets.json` to select the board without a prompt, for example `diy.tx_900.xiao_s3_wio_sx1262` or `diy.rx_900.xiao_s3_wio_sx1262`. Set it before every build. Nothing checks it against the env, so a TX key on an RX build silently flashes the TX layout.
- Without it an interactive menu appears. Without a TTY the firmware stays "bare" (WiFi only, configured from the web UI). The last choice is remembered in `src/.pio/default_target_config.json`.

### Build flags

- The build reads them in this order:
  1. `src/user_defines.txt` (committed; here it defaults to `-DRegulatory_Domain_EU_868`).
  2. `src/super_defines.txt` (gitignored, local, for example `-DMY_BINDING_PHRASE="..."`). Write it with `-Encoding ascii`: a byte-order mark breaks the first flag.
  3. The `PLATFORMIO_BUILD_FLAGS` environment variable.

  `!-DFOO` removes `-DFOO`.
- `python/build_flags.py` turns some defines into the appended options JSON instead of C defines: the binding phrase (as UID), the WiFi SSID and password, baud rates, `LOCK_ON_FIRST_CONNECTION` and the regulatory domain. Except for the domain, these come only from the two `.txt` files, not from `PLATFORMIO_BUILD_FLAGS`.
- 900 MHz, SX126x and LR builds need a `Regulatory_Domain_*`. With two 900 MHz domains EU868 wins over FCC915, so for FCC use `!-DRegulatory_Domain_EU_868 -DRegulatory_Domain_FCC_915`.
- Debug flags are documented in `src/user_defines.txt` (`DEBUG_LOG`, `DEBUG_LOG_VERBOSE`, `DEBUG_RX_SCOREBOARD`, `DEBUG_RCVR_LINKSTATS`, ...). `DEBUG_TX_FREERUN` makes the TX transmit without a handset. Use the same flags for TX and RX.
- A change of `PLATFORMIO_BUILD_FLAGS` rebuilds everything (about 2 minutes) and can delete other envs' output under `.pio/build`. A one-off `.sconsign311.dblite: No such file` failure goes away on a rerun. Sessions that build at the same time need their own `PLATFORMIO_BUILD_DIR`, such as `.pio/build-<session>`.

### Web UI

```sh
cd src/html
npm ci
npm run lint
npm run build:sx126x-tx   # writes headers/web-sx126x-tx.h; build:<chip>-<tx|rx>[-8285]
npm run build:all         # every header
```

- The headers in `src/html/headers/` are committed. Rebuild and commit them whenever `src/html/src` changes, or CI fails.
- The firmware build copies the matching header to `src/include/WebContent.h` (`python/copy_html.py`). That file is generated and gitignored.
- `npm run dev:tx` or `npm run dev:rx` starts a dev server. `VITE_ELRS_PROXY_TARGET` in `src/html/.env` proxies it to a device. The `dev:*` scripts use POSIX `VAR=value` syntax, so run them from Git Bash.

### Style

C and C++ follow `src/.clang-format` (Microsoft base, 4-space indent, no column limit) and `src/.editorconfig` (LF). `tx_main.cpp` uses a 2-space indent.

## Architecture

- **TX or RX at compile time.** The build sets `-DTARGET_TX` or `-DTARGET_RX`, `build_src_filter` drops `rx_*`/`rx-*/` or `tx_*`, and the pin-map header `include/target/Unified_*.h` is force-included. Shared code is in `common.cpp` (the global `Radio`, the air-rate tables, link state) and `rxtx_common.cpp`. Device code calls into the two mains only through `include/rxtx_intf.h`.
- **Packet timing.** `lib/HWTIMER` fires tick and tock, each half a packet interval apart.
  - TX: `timerCallback` calls `SendRCdataToRF`, which picks a SYNC, DATA or RC packet, packs it, adds the CRC and calls `Radio.TXnb`. `TXdoneISR` hops FHSS and switches to RX for telemetry slots.
  - RX: `RXdoneISR` calls `ProcessRFPacket` (CRC, phase detector, dispatch). `HWtimerCallbackTock` hops (`HandleFHSS`), sends telemetry (`HandleSendDataDl`) and trims the timer to the TX (`updatePhaseLock`). `loop()` runs the connection state machine and `cycleRfMode`.
- **Radio drivers.** There is no virtual interface. `include/common.h` includes the driver and declares `extern <Driver> Radio;` based on `RADIO_SX127X`, `RADIO_SX128X`, `RADIO_SX126X`, `RADIO_LR1121` or `RADIO_LR2021`. `Begin()` and `Config()` differ per chip, so the two mains use `#if`. Each `lib/<X>Driver` has:
  - `X.cpp`: `Begin`, `Config`, `SetFrequencyReg`, `TXnb`, `RXnb` and the ISR chain.
  - `X_hal.cpp`: SPIEx, the BUSY wait, the DIO interrupt and the dual-radio chip selects.
  - `X_Regs.h`: registers and opcodes.

  The base class is `lib/SX12xxDriverCommon`.
- **Library exclusion.** PlatformIO's dependency finder ignores `#if`, so `lib_ignore` excludes the unused drivers. The exclusions are in the `[radio_*]` sections of `targets/common.ini`, in `[env:native]`, and in `python/lib_exclusions.py` (per TX/RX and MCU).
- **Adding a radio type** touches:
  - `common.h/.cpp`, `targets.h` and FHSS
  - the `#if`s in the two mains
  - `lib/DEVICE/device.cpp`
  - every `[radio_*]` section and the native `lib_ignore`
  - `build_flags.py` and `copy_html.py`
  - the web UI build scripts and headers
- **Air rates and OTA.** `ExpressLRS_AirRateConfig[]` and `ExpressLRS_AirRateRFperf[]` live in `common.cpp`. Config stores the table index; the air carries `enum_rate`. `lib/OTA` has OTA4 (8 bytes, CRC14) and OTA8 (13 bytes, CRC16) serializers, bound by `OtaUpdateSerializers` at each rate change. Bump `OTA_VERSION_ID` (`include/targets.h`) when an OTA struct changes; `static_assert`s check the sizes.
- **FHSS.** `lib/FHSS` holds the per-domain tables. The domain index comes at runtime from the options JSON (`firmwareOptions.domain`). The `Regulatory_Domain_*` define is only a compile-time guard and gates LBT. The UID seeds the hop sequence.
- **Hardware layout at runtime.** `lib/OPTIONS` (`options_init`, `hardware_init`) reads the appended JSON. `/options.json` and `/hardware.json` on LittleFS, written by the web UI, override it. `GPIO_PIN_*` and `OPT_*` are runtime lookups (`hardware_pin()`, `hardware_flag()`, `hardware_int()`), so they cannot be used in `#if`. A new layout key needs three changes, then a rebuild of the web headers:
  - the enum in `include/hardware.h`
  - `fields[]` in `lib/OPTIONS/hardware.cpp`
  - `html/src/utils/hardware-schema.js`
- **Devices.** `lib/DEVICE` defines `device_t {initialize, start, event, timeout, subscribe}`. The two mains register arrays of devices, each with a core affinity. On dual-core ESP32 the core-0 devices run in a FreeRTOS task, concurrently with `loop()`. `devicesTriggerEvent()` sets event bits, and subscribed devices handle them on their next update.
- **Config.** `lib/CONFIG` stores TxConfig in NVS on ESP32, including 64 per-model configs keyed by the CRSF model ID, and in EEPROM on ESP8266. RxConfig is a packed blob. When the layout changes, bump `TX_CONFIG_VERSION` or `RX_CONFIG_VERSION` (`config.h`) and add an upgrade step. Commits are deferred to `loop()`. Stored settings survive a new upload; `esptool erase_flash` resets them.
- **CRSF, handset, Lua.** In `lib/CrsfProtocol`, `CRSFRouter` links connectors (transports) and endpoints. The TX talks to the handset in `lib/Handset` (half duplex when the TX and RX pins are the same). The Lua menus are defined in firmware (`lib/tx-crsf/TXModuleParameters.cpp`, `lib/rx-crsf/RXParameters.cpp`); `src/lua/elrs.lua` only browses them. RX serial protocols are in `src/src/rx-serial/`.
- **Logging.** In `lib/logging`, `DBG`/`DBGLN` compile only with `DEBUG_LOG` (`DBGV*` also needs `DEBUG_LOG_VERBOSE`); `ERRLN` is usually on. `debugPrintf` supports only `%s %d %u %x %f`. On ESP32-S3 the log goes to USB CDC (`USBSerial`). On the TX it is interleaved with binary CRSF.

### Rules for timing-critical code

- Anything reachable from the hardware timer or the radio DIO interrupt must be `ICACHE_RAM_ATTR` (mapped to `IRAM_ATTR`). Flash writes disable the cache, so non-IRAM code in an ISR crashes.
- No logging inside ISRs: USB CDC logging is not interrupt-safe.
- OTA buffers need `WORD_ALIGNED_ATTR`. Snapshot ISR-updated variables in `loop()`.
- ESP8285 builds are tight on IRAM and flash (`elrs.flash.1m.ld`).

## Branch Wio-SX1262: the SX1262 port

- Goal: an SX1262 LoRa-900 driver (`src/lib/SX126xDriver`) for two Seeed XIAO ESP32-S3 + Wio-SX1262 boards in EU868. Envs `Unified_ESP32S3_SX126X_{TX,RX}_via_UART`, configs `diy.{tx,rx}_900.xiao_s3_wio_sx1262`.
- Documents:
  - `Docs/prepare-plan-how-to-tranquil-truffle.md`: the driver plan. Its notes on `src/.git` and the Meshtastic pins are out of date; the layout in `src/hardware` is authoritative.
  - `Docs/Wio-SX1262-hardware-test-plan.md`: tests T1 to T14 and the bench setup.
  - `Docs/hw_test_build.md`: build and flash walkthrough.
  - `Docs/hwtest/test-report.md`: results.
- Tools in `Docs/hwtest/`: `elrs_usbmon.py` (USB CDC monitor, link-statistics decoder, CRSF parameter client), `linkstats_summary.py`, `rtl_power_scan.py`. Logs go to `Docs/hwtest/logs` (gitignored).
- Commit `e51bcf5f` holds only the debug USB logging, so it can be reverted on its own. Remove code marked `TEMPORARY` before the driver is committed: bring-up diagnostics, `DEBUG_CW_*`, `DEBUG_PLL_SCAN`, `SX126X_BISECT`.
- SX126x HAL: `SPIEx::write()` returns while the frame is still being clocked out, and BUSY rises only up to 600 ns after NSS goes high. So `SX126xHal::WaitOnBusy()` and `WaitOnBusyLong()` call `SPIEx.waitIdle()` and wait at least 1 µs before they sample BUSY. Without that, commands reached a busy chip and were dropped or garbled: the CW was silent and SetTx/SetRx failed with PLL_LOCK (fixed on 2026-09-16, see `Docs/hwtest/test-report.md`). Keep this when you change the HAL.
- The SX126x rate table has an extra 150Hz row (the last one, SF6 at 6.67 ms). No other 900 MHz radio table has it, so only SX126x devices can link at it. At 200Hz and D50 the downlink fails, because the telemetry turnaround leaves only about 0.2 ms; at 150Hz it works, even at telemetry 1:2. It is therefore the SX126x default in `TxConfig::SetDefaults` and the RX's initial rate, where the other radios use 200Hz or 250Hz.

### Bench

- Boards, both with USB ID 303A:1001, so always pass `--upload-port` and address the monitor by the end of the serial (`--port "TX=47:A0"`):
  - Board A: USB serial `E0:72:A1:F9:47:A0`, usually COM6, the TX.
  - Board B: USB serial `E0:72:A1:F9:47:B0`, usually COM9, the RX.
- XIAO ESP32-S3 pins:
  - SX1262: DIO1 GPIO2, RST GPIO3, BUSY GPIO4, NSS GPIO5, RF_SW (RXEN) GPIO6, SCK/MISO/MOSI GPIO7/8/9.
  - Button GPIO1, serial RX 44 and TX 43, LED GPIO21 (active low).

  The module's PE4259 RF switch gets CTRL from the SX1262's DIO2 and /CTRL from RF_SW.
- Only one program can open a COM port. Don't open the ports with PuTTY or the Arduino monitor, which toggle DTR/RTS. Don't type into the TX port, which parses CRSF.
- For erasing, use PlatformIO's esptool v4.9.0 (`~/.platformio/packages/tool-esptoolpy/esptool.py`); the bundled v4.2.1 fails on the XIAO's USB port.
- RF rules: EU868 only on air; never flash an FCC915 build here. Use 10 mW (at most 25 mW) on air, keep the boards 1 to 3 m apart, and keep carriers under a minute.
- RTL-SDR Blog V4:
  - The tools are in `C:\Users\bonev\Downloads\Release\x64`; run them from Git Bash. Check the device with `rtl_test.exe -t`.
  - Only one program can hold the SDR, and that includes the user's SDR#.
  - It has its own spur at 864.004 MHz, and an outside LoRa signal sits at 869.49 to 869.57 MHz.
  - Use gain 0 near the boards, or it overloads.
- Claude cannot join the boards' WiFi (that cuts the PC's network) or see the LEDs.

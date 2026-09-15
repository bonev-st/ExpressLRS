# Wio-SX1262 hardware test plan

This plan tests the SX1262 driver port on the `Wio-SX1262` branch with two Seeed XIAO ESP32-S3 + Wio-SX1262 boards, in the EU868 band.

Both boards connect to the PC only by their USB-C cable. The TX runs without a handset. Both boards print their log over USB CDC. The PC tool [`hwtest/elrs_usbmon.py`](hwtest/elrs_usbmon.py) shows both logs, decodes the link statistics the TX sends, and changes TX settings the way the ExpressLRS Lua script does on a handset.

The first hardware run was on 2026-09-15 and 16; its results are in [`hwtest/test-report.md`](hwtest/test-report.md). For a new run, write the results in [section 5](#5-results-sheet).

## 1. Before you start

### 1.1 Safety and radio rules

- Connect the antenna to each Wio-SX1262 before you power the board. The RX transmits too (telemetry), so both boards need an antenna.
- Test only with the EU868 build (863.275 to 869.575 MHz). Never flash or power an FCC915 build here.
- On air, use 10 mW (the default) or 25 mW. The EU limit of 25 mW is e.r.p., so an antenna with gain lowers the allowed setting. Use 50 mW and 100 mW only with the antenna ports connected through attenuators (40 dB or more in total) or into dummy loads, or skip those steps.
- ExpressLRS transmits almost all the time and has no listen-before-talk at 868 MHz. On-air runs therefore exceed the EU SRD duty-cycle limits of 863 to 870 MHz, which are 0.1 % to 10 % depending on the sub-band (general knowledge; see ERC/REC 70-03 and ETSI EN 300 220). Keep on-air runs as short as each test needs. For the long runs (T5 and T11), connect the two antenna ports by cable through at least 40 dB of attenuation if you can, or shorten them.
- Keep the boards 1 to 3 m apart. At less than about 0.5 m the receivers can overload, and the TX lowers its power by itself when the uplink RSSI reaches -5 dBm.
- The continuous-wave (CW) test in T10 sends a constant carrier. Keep each carrier to about 20 s, and less than a minute in total per board.
- Never connect a board's antenna port to the RTL-SDR by cable unless there are at least 40 dB of attenuators in between. 10 mW straight into the SDR can damage it. Over the air, keep the SDR antenna at least 1 m from the board.

### 1.2 What you need

- Both board stacks with antennas, and two USB-C data cables.
- An RTL-SDR Blog V4 with its antenna, for the frequency tests T10 and T14 (setup in section 1.7).
- Optional: attenuators or dummy loads for power steps above 25 mW, and a USB power meter.

### 1.3 Identify the boards

Both boards have the same USB ID (303A:1001). Each board has its own USB serial number, which never changes. The COM number can change.

| Board | USB serial | COM port now | USB location | Role (fill in) |
|---|---|---|---|---|
| A | E0:72:A1:F9:47:A0 | COM6 | 1-1 | |
| B | E0:72:A1:F9:47:B0 | COM9 | 1-5 | |

1. Choose one board as TX and one as RX, and label them.
2. Run the block in section 1.4 once, so that `$py` and `$mon` exist. Then unplug the TX board and run `& $py $mon --list`. The port that is missing belongs to the TX.
3. Set `$TX`, `$TXSER`, `$RX` and `$RXSER` in section 1.4 to match.

### 1.4 PowerShell session

Run this in every PowerShell window you use. Use one window for building and flashing, and a second window for the monitor.

```powershell
Set-Location C:\Work\RF-RC\ExpressLRS\ExpressLRS\src
$pio  = 'C:\Users\bonev\.platformio\penv\Scripts\pio.exe'
$py   = 'C:\Users\bonev\.platformio\penv\Scripts\python.exe'
$mon  = 'C:\Work\RF-RC\ExpressLRS\ExpressLRS\Docs\hwtest\elrs_usbmon.py'
$logs = 'C:\Work\RF-RC\ExpressLRS\ExpressLRS\Docs\hwtest\logs'
$scan = 'C:\Work\RF-RC\ExpressLRS\ExpressLRS\Docs\hwtest\rtl_power_scan.py'
$lss  = 'C:\Work\RF-RC\ExpressLRS\ExpressLRS\Docs\hwtest\linkstats_summary.py'   # T13
$rtl  = 'C:\Users\bonev\Downloads\Release\x64'   # folder with rtl_power.exe (section 1.7)
$esptool = 'C:\Users\bonev\.platformio\packages\tool-esptoolpy\esptool.py'   # esptool v4.9.0 (section 2.3)
New-Item -ItemType Directory -Force $logs | Out-Null
$TX = 'COM6'; $TXSER = '47:A0'   # change both lines to match your labels
$RX = 'COM9'; $RXSER = '47:B0'
```

### 1.5 Debug changes on the branch

The branch has five small changes for these tests. They only affect builds with `DEBUG_*` flags; release builds are unchanged. Both debug builds compile (TX: RAM 20.5 %, flash 75.5 %). The first hardware run also found driver bugs; see [section 1.8](#18-driver-fixes-from-the-first-hardware-run).

| File | Change | Why |
|---|---|---|
| `src/lib/logging/logging.h` | The S3 TX sends its debug log to USB CDC (`USBSerial`) | The TX log went to the Arduino `Serial` object, which the S3 TX ends at startup (UART0 belongs to the handset port), so it was lost |
| `src/src/tx_main.cpp` | 4 KB USB transmit buffer in debug builds | Keeps the boot log until the PC reads it |
| `src/src/rx_main.cpp` (2 places) | The same 4 KB buffer | The same reason |
| `src/src/rx_main.cpp`, `debugRcvrLinkstats()` | The `DEBUG_RCVR_LINKSTATS` CSV goes to the log stream | It went to the UART0 pins, not to USB |
| `src/lib/SX126xDriver/SX126x.cpp` | Removed the "status at first RX_DONE" log line | It ran inside the DIO1 interrupt, and USB logging is not interrupt-safe |

These changes are in their own commit, `e51bcf5f`. If you decide after the tests not to keep them, `git revert e51bcf5f` removes them.

Some log lines are still printed from inside interrupts: `tentative conn`, `New UID = ...` and `New TLMrate ...` on the RX, and the driver's `SX126x BUSY timeout` and `Timeout!`. USB logging is not interrupt-safe, so in a debug build a restart right after one of these lines points to the logging, not the driver. Repeat such a test with the release build (T12).

### 1.6 The monitor tool

The tool opens a board's USB port without resetting it. It prints each text line with a timestamp and a label, and it prints a `LINK` line about once per second from the link statistics the TX sends over USB. With `--log NAME` it saves the output to `NAME.log` and every link-statistics frame to `NAME_linkstats.csv`. When you stop it with Ctrl+C, it prints a summary that starts 5 s after the link came up: average, minimum and maximum LQ, RSSI and SNR, and how many frames had LQ below 100.

| Task | Command |
|---|---|
| List the boards | `& $py $mon --list` |
| Watch both boards and save a log | `& $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --log "$logs\T3"` |
| List all TX settings | `& $py $mon --port "TX=$TXSER" --params --exit` |
| Set a setting, then keep watching | `& $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --set "Packet Rate=50Hz"` |
| Run a TX command | `& $py $mon --port "TX=$TXSER" --cmd Bind --exit` |
| Restart a board and capture its boot log | `& $py $mon --port "RX=$RXSER" --reset RX --log "$logs\T1"` |
| Stop after a fixed time and print the summary | Add `--duration 60` to any monitor command |
| Reset a board several times, 1.2 s apart (3 resets put a bound RX in bind mode, T8) | `& $py $mon --port "RX=$RXSER" --reset RX --reset-count 3` |

Setting names and values:

- `Packet Rate`: `200Hz`, `100Hz Full`, `100Hz`, `50Hz`, `25Hz`, `D50`
- `Telem Ratio`: `Std`, `Off`, `1:128`, `1:64`, `1:32`, `1:16`, `1:8`, `1:4`, `1:2`, `Race`
- `Max Power`: `10`, `25`, `50`, `100` (mW)
- Commands: `Bind`, `Enable WiFi`

Rules:

- A COM port can be open in only one program. Stop the monitor with Ctrl+C before you flash, or before you start the next tool command on the same board. `--set` and `--cmd` use the first `--port`, which must be the TX.
- Do not open the ports with PuTTY or the Arduino serial monitor. They switch DTR and RTS, which resets the board or starts its bootloader.
- Do not type into the TX port. The TX reads every byte as CRSF, and a valid MAVLink frame would switch it to MAVLink mode.
- `& $pio device monitor -p $RX --dtr 0 --rts 0` also works for the RX. For the TX use the tool, because its text log is mixed with binary CRSF frames.

RX packet scoreboard: build both boards with `-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RX_SCOREBOARD`. The RX then prints one character per packet slot: `R` received, `_` missed, `.` CRC error, `s` sync packet, `T` telemetry sent. In this mode the RX sends nothing to a flight controller.

### 1.7 RTL-SDR V4 setup (for T10 and T14)

1. Install the USB driver. Plug in the RTL-SDR and run Zadig. Select Options, List All Devices, choose `Bulk-In, Interface (Interface 0)`, select WinUSB and click Replace Driver (or Install Driver).
2. Download the Windows release of the RTL-SDR Blog drivers (GitHub `rtlsdrblog/rtl-sdr-blog`, Releases, the Windows x64 zip) and unpack it. On this bench it is in `C:\Users\bonev\Downloads\Release\x64`; set `$rtl` (section 1.4) to your folder. The V4 needs these drivers; older RTL-SDR builds do not handle its tuner correctly.
3. Test it: `& "$rtl\rtl_test.exe" -t`. It must list the device and print `Found Rafael Micro R828D tuner` and `RTL-SDR Blog V4 Detected`. The line `No E4000 tuner found, aborting.` after it is normal. The driver recognises the V4 by the strings `RTLSDRBlog` and `Blog V4` in its EEPROM, so never change them with `rtl_eeprom`.
4. For a visual check, install SDR++ or SDR# in a version that supports the V4 (2023 or later). This is optional.

Rules for rtl_power:

- Only one program can use the SDR. Close SDR++ or SDR# first, and run one rtl_power at a time.
- Always give a fixed gain; start with `-g 0`, the lowest. In rtl_power `-g 0` is 0 dB, and without `-g` the tuner's automatic gain follows the hopping signal. (In `rtl_sdr` and `rtl_tcp`, `-g 0` means automatic.) rtl_power rounds to the tuner's table: `-g 10` gives 8.7 dB, `-g 20` gives 19.7 dB.
- Give the frequencies in Hz, exactly as in T10 and T14, and keep each tuning window 1 MHz or wider. Below 1 MHz rtl_power adds samples together before its FFT, and a strong carrier then overflows it and lands kHz off. `& $py $scan plan <-f value> --crop <-c value>` prints the windows a command will use, without opening the SDR.
- Keep the bias tee off: no `-T`, no `-O` (on the V4, "offset tuning" switches the bias tee), no `rtl_biast -b 1`. rtl_power switches it off at every start.
- `-p` takes whole ppm only; do not use it.
- The dB values are relative, not dBm. Compare only recordings made with the same command, gain and SDR position.
- If rtl_power has not returned 15 s after its `-e` time, it hangs in a USB read (the driver waits for ever) and `-e` never fires. Run `Stop-Process -Name rtl_power`, and check that the CSV's last row is recent.

The V4 has a 1 ppm reference oscillator (TCXO), so its own error at 868 MHz is up to about 0.9 kHz. The steps of its tuner's PLL add up to about 0.4 kHz. That is small enough for T10 without calibration, and it is the same for both boards when both are measured with the same command.

rtl_power overwrites the centre bin of each tuning window with its neighbour, and the T10 and T14 commands keep every signal away from those centres; the script [`hwtest/rtl_power_scan.py`](hwtest/rtl_power_scan.py) also ignores the bins next to them. The V4 has its own narrow spur at 864.000 MHz, 30 times its 28.8 MHz reference. A strong signal also appears weakly at other frequencies (tuner images, aliases at the window edges); the script ignores anything 20 dB or more below the strongest channel.

### 1.8 Driver fixes from the first hardware run

The first run on 2026-09-15 found these problems. The fixes are in the working tree and not committed yet. Results: [`hwtest/test-report.md`](hwtest/test-report.md).

| Problem | Symptom | Fix |
|---|---|---|
| `WaitOnBusyLong()` checked BUSY too early after the image calibration. The next commands were lost, among them the fallback mode, so the TCXO switched off after every packet | TX `Timeout!` on every second packet, many BUSY timeouts, no link | `SX126x_hal.cpp`: wait 10 µs before the first BUSY check |
| The SX1262 kept its old frequency when the frequency was changed in FS or RX mode | RX `tentative conn`, then `Bad sync, aborting`; scoreboard `RR____...` | `SX126x.cpp`: `SetFrequencyReg()` goes through STDBY_XOSC |
| A hop that arrives while the radio transmits would retune it during the transmission | Suspected behind the 100Hz `T_T_T_` and `TLM crc error` of 2026-09-15, wrongly: a counter run on 2026-09-16 shows that hops never overlap a transmission | `SX126x.cpp`: the hop waits until TX_DONE. Kept as a safeguard against a lost TX_DONE; the 100Hz failure was most likely cured by the HAL BUSY fix |
| The old esptool v4.2.1 | `StopIteration` after `Stub running...` | `build_env_setup.py`: ESP32-S3 uploads use esptool v4.9.0 |

Known limitation: at 200Hz and D50 the TX receives no telemetry. The RX's telemetry packet ends only about 0.2 ms before the TX's next packet, so the downlink is lost, while the uplink LQ is 100. This needs a timing or rate-table change.

Temporary diagnostics in builds with `-DDEBUG_LOG`, to be removed before the final commit:

- `FS test DCDC, ...` and `FS test LDO, ...` after boot: a PLL lock test. Both must show `err 0x0`.
- `SX126x BUSY <n> us after cmd 0x..`: BUSY stayed high longer than 1 ms. The driver then waits up to 30 ms.
- `Timeout! #n status 0x.. irq 0x.. err 0x.. dio1 .. freq ..` on the TX.

## 2. Build and flash

### 2.1 Binding phrase (once)

Both boards get the same UID from a binding phrase. Put it in `src/super_defines.txt`, which git ignores and the build reads like `user_defines.txt`:

```powershell
Set-Content -Path super_defines.txt -Encoding ascii -Value '# local test settings, not committed', '-DMY_BINDING_PHRASE="wio-sx1262-test"'
```

Use `-Encoding ascii`, which writes plain text with no byte-order mark. A byte-order mark would break a `-D` flag on the first line.

### 2.2 Build flags

```powershell
$env:PLATFORMIO_BUILD_FLAGS = '-DDEBUG_LOG -DDEBUG_TX_FREERUN'
```

- `DEBUG_LOG` turns the log on for both boards.
- `DEBUG_TX_FREERUN` makes the TX transmit without a handset. It also stops the TX from starting WiFi by itself. The RX build ignores it.

Use the same flags for both boards. When the flags change, PlatformIO deletes the whole `.pio\build` folder and rebuilds everything (about 2 minutes per board). If a build then stops with `.sconsign311.dblite: No such file or directory`, run the same command again.

### 2.3 Erase both boards (first time only)

Old data in flash would override or mix with the new build: a `hardware.json` saved from the web UI, or old TX and RX settings.

Use the esptool that comes with PlatformIO (v4.9.0, `$esptool` from section 1.4). The older esptool in `src\python\external` (v4.2.1) is unreliable on the XIAO's USB port: it can stop with `StopIteration` right after `Stub running...`.

```powershell
& $py $esptool --chip esp32s3 --port $TX erase_flash
& $py $esptool --chip esp32s3 --port $RX erase_flash
```

Each command ends with `Chip erase completed successfully` and `Hard resetting via RTS pin...`.

If esptool cannot connect: unplug the board, hold the XIAO BOOT button, plug it in, release BOOT, check the COM port with `--list`, and run the command again.

### 2.4 Flash the TX

```powershell
$env:ELRS_UNIFIED_CONFIG = 'diy.tx_900.xiao_s3_wio_sx1262'
& $pio run -e Unified_ESP32S3_SX126X_TX_via_UART -t upload --upload-port $TX
```

Check the output:

- A `UID bytes:` line. Write the six numbers down.
- The build flags contain `-DRegulatory_Domain_EU_868`, `-DDEBUG_LOG` and `-DDEBUG_TX_FREERUN`.
- No product menu (`0) Leave bare ...`) and no `Warning: configuration ... was not found`.
- The upload starts with `esptool.py v4.9.0`. For ESP32-S3 targets the build uses PlatformIO's esptool, not the old v4.2.1 (`src/python/build_env_setup.py`).
- esptool prints `Hash of data verified` for each image and ends with `Hard resetting via RTS pin`.

### 2.5 Flash the RX

```powershell
$env:ELRS_UNIFIED_CONFIG = 'diy.rx_900.xiao_s3_wio_sx1262'
& $pio run -e Unified_ESP32S3_SX126X_RX_via_UART -t upload --upload-port $RX
```

Check the same points. The `UID bytes:` must be the same as for the TX.

Always set `ELRS_UNIFIED_CONFIG` just before each build. The build does not check it against the environment, so a TX key on the RX build silently flashes the TX layout.

### 2.6 Optional: check what the firmware contains

Run this right after a flash. It prints the product name, the Lua name, the options (`"domain": 2` is EU868, and `"uid"` is present when a binding phrase is set) and the pin layout.

```powershell
& $py -c "import sys;sys.path.insert(0,'python');import UnifiedConfiguration as U;f=open(sys.argv[1],'rb');p=U.findFirmwareEnd(f);f.seek(p);b=f.read(2704);[print(x.rstrip(b'\0').decode()) for x in (b[:128],b[128:144],b[144:656],b[656:])]" .pio\build\Unified_ESP32S3_SX126X_TX_via_UART\firmware.bin
```

Use the `..._RX_via_UART` path for the RX. The layout must show `radio_nss 5`, `radio_busy 4`, `radio_dio1 2`, `radio_rst 3`, `power_rxen 6`, `serial_rx 44`, `serial_tx 43` and `button 1`.

## 3. Tests

### T1: RX boot and radio start

1. Unplug the TX board. The TX transmits as soon as it has power, and the RX would link at once.
2. Start the monitor for the RX only: `& $py $mon --port "RX=$RXSER" --log "$logs\T1"`
3. Unplug the RX USB cable and plug it in again, press the XIAO RESET button, or add `--reset RX` to the monitor command. The tool reconnects and prints the boot log.

Pass:

- These lines appear, in about this order:

  ```text
  UID=(a, b, c, d, e, f) ModelId=...
  Primary Domain EU868, 13 channels, sync=6
  Hal Init
  SX126x Reset
  SX126x Ready!
  SX126x Begin
  RFAMP_hal Init
  Use RX pin: 6
  SX126x #1 found
  SX126x TCXO voltage 2, delay 320
  Enabling DCDC regulator
  SX126x DIO2 RF switch on
  SetPower: 14
  SX126x #1 device errors 0x0
  SetPower: 10
  ```

- No line contains `BUSY`.
- Debug builds with the current diagnostics also print `FS test DCDC, 866425000 Hz, reg 0x3626cccc: status 0x42 err 0x0`, and the same for `LDO`, after `device errors`. Both must show `err 0x0`.
- The LED blinks 500 ms on, 500 ms off (bound, no link yet).

Without a TX, the RX starts WiFi after 60 s and its LED flickers fast. That is normal. Unplug and plug the RX to start again.

### T2: TX boot and radio start

1. Unplug the RX board, so that the TX has nothing to link with.
2. Start the monitor for the TX only: `& $py $mon --port "TX=$TXSER" --log "$logs\T2"`
3. Unplug the TX USB cable and plug it in again.

Pass:

- The same driver lines as in T1, from `Hal Init` to `SX126x #1 device errors 0x0`, plus `UID=(...)` (the same six numbers as the RX) and `About to start CRSF task...`.
- No line contains `BUSY`.
- The LED stays off for about 3 s (the TX waits for a model ID), then blinks 500 ms on, 500 ms off: the TX is transmitting and waits for the RX.

### T3: First link at 200 Hz

1. Place both boards 1 to 3 m apart, with the antennas vertical.
2. Start the monitor for both boards: `& $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --log "$logs\T3"`
3. Plug in the RX, then the TX (or unplug and plug in both).
4. Let it run for 2 minutes, then press Ctrl+C.

Pass:

- Within 10 s the RX log shows `tentative conn` and then `got conn`, and the TX log shows `got downlink conn`.
- The TX prints a `LINK` line every second with `rate 200Hz`, `lq 100` up and down, RSSI between -15 and -55 dBm, and SNR of +5 dB or more (usually +8 to +13).
- The uplink and downlink RSSI differ by 6 dB or less. Both ends send at 10 mW, so a difference of more than 10 dB points to the RF switch path of one board.
- Both LEDs are solid.
- No `lost conn`, `Timeout!` or `TLM crc error` line, and no restart (no new boot lines).
- The summary at Ctrl+C starts 5 s after the link came up. It shows a minimum LQ of 100, or close to it, and few frames with LQ below 100.

Known issue from the first run: at 200 Hz the TX receives no telemetry (`lq 0` down), although the uplink LQ is 100. Check the whole link at 50Hz as well (`--set "Packet Rate=50Hz"`). See [section 1.8](#18-driver-fixes-from-the-first-hardware-run).

### T4: Packet-rate sweep

Run each rate for at least 60 s. Start with the monitor stopped, then:

```powershell
& $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --set "Packet Rate=100Hz Full" --log "$logs\T4_100full"
```

Repeat with `100Hz`, `50Hz`, `25Hz`, `D50` and `200Hz`.

| Rate | LoRa | Interval | Time on air | Sensitivity | Default telemetry |
|---|---|---|---|---|---|
| 200Hz | SF6, CR 4/7, 8 bytes | 5 ms | 4.64 ms | -112 dBm | 1:64 |
| 100Hz Full | SF6, CR 4/8, 13 bytes | 10 ms | 6.94 ms | -112 dBm | 1:32 |
| 100Hz | SF7, CR 4/7, 8 bytes | 10 ms | 8.77 ms | -117 dBm | 1:32 |
| 50Hz | SF8, CR 4/7, 8 bytes | 20 ms | 18.56 ms | -120 dBm | 1:16 |
| 25Hz | SF9, CR 4/7, 8 bytes | 40 ms | 29.95 ms | -123 dBm | 1:8 |
| D50 | SF6, CR 4/7, 8 bytes, each packet sent 4 times | 5 ms | 4.64 ms | -112 dBm | 1:64 |

All rates use 500 kHz bandwidth.

Pass for each rate:

- The tool prints `set: Packet Rate = ...` with the new rate.
- Within 5 s the `LINK` lines show the new rate, and the link comes back. The RX log shows `Req air rate change` and one `lost conn` for the change; that is expected.
- After that, `lq` stays at 98 or more up and down, and there is no further `lost conn`.

If the RX does not reconnect within 10 s, unplug and plug in the RX, and note it in the results. `LOCK_ON_FIRST_CONNECTION` keeps a disconnected RX on the last rate it was connected on, without searching the other rates, until it restarts or enters bind mode.

After a rate change the RX logs `New TLMrate 1:<n>` with the rate's default ratio. Uplink data, for example a device ping forwarded to the RX, makes the TX boost the ratio to 1:2 until the RX replies. Without a downlink the boost stays, and the RX then sends telemetry (`T` in the scoreboard) in every second slot. The monitor tool pings only the TX, so it does not cause the boost.

First-run result: 25Hz, 50Hz and 100Hz Full pass; 100Hz was poor; D50 and 200Hz have no downlink. Details are in the test report.

### T5: Telemetry turnaround stress

At 200 Hz and D50 only 0.36 ms are free between the end of one packet and the start of the next. Telemetry ratio 1:2 switches between sending and receiving every second packet.

```powershell
& $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --set "Packet Rate=200Hz" --set "Telem Ratio=1:2" --log "$logs\T5_200"
```

Run it for 2 minutes. Repeat with `Packet Rate=D50`. Finish with `--set "Telem Ratio=Std"`.

Pass: downlink LQ 95 or more, uplink LQ 98 or more, and no `lost conn`, `Timeout!` or `TLM crc error` line. The RX logs `New TLMrate 1:2` once when the ratio changes; that is expected.

### T6: Power steps

1. Move the boards apart, or put a wall between them, until the uplink RSSI is between -60 and -90 dBm.
2. Run with `--set "Max Power=10"` for 20 s, then with `--set "Max Power=25"` for 20 s.

Pass: the `LINK` lines show power 10 mW and then 25 mW, and the uplink RSSI rises by 4 dB (plus or minus 2 dB).

With attenuators or dummy loads only: continue with `Max Power=50` and `100`. Each step should add about 3 dB.

On air, do not use the TX button for this. A long press steps the power through 10, 25, 50 and 100 mW.

Finish with `& $py $mon --port "TX=$TXSER" --set "Max Power=10" --exit`. The TX keeps the power setting in flash, even after a new upload.

### T7: Range check (optional)

Run the monitor with `--set "Max Power=10"`. Increase the path loss (distance, walls or attenuators) until LQ starts to drop. The RSSI at that point should be within about 5 dB of the sensitivity in T4. Lower rates must hold the link longer (25Hz the longest).

### T8: Bind

With the button:

1. Unplug the TX. Start the monitor for the RX only.
2. Put the RX in bind mode: hold the Wio button for 1.5 to 4 s and release it. At 5 s it starts WiFi instead, and at 12 s it resets all RX settings. The LED double-blinks, and the log shows `Entered binding mode at freq = ...`.
3. Stop the monitor, plug in the TX, and run:

   ```powershell
   & $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --cmd Bind --duration 45 --log "$logs\T8"
   ```

Without touching the boards (both stay plugged in): the RX enters bind mode after 3 boots in a row that each end within 2 s. It clears that count as soon as it links, so the TX must not transmit meanwhile.

1. Stop the TX by putting it in the ROM download mode. esptool ends with `Staying in bootloader.`

   ```powershell
   & $py $esptool --chip esp32s3 --port $TX --before default_reset --after no_reset read_mac
   ```

2. Reset the RX 3 times, 1.2 s apart. The log shows `Power on counter >=3, enter binding mode` and `Entered binding mode at freq = 866425000`.

   ```powershell
   & $py $mon --port "RX=$RXSER" --reset RX --reset-count 3 --duration 10 --log "$logs\T8_rx"
   ```

3. Restart the TX and bind:

   ```powershell
   & $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --reset TX --cmd Bind --duration 45 --log "$logs\T8"
   ```

Pass:

- The tool prints `cmd: Bind (command, executing) Binding...` and later `idle`.
- The RX log shows `New UID = 0, 0, ...` and `Exiting binding mode`. The bind packet carries only the last four UID bytes, so the RX sets the first two to 0. The last four must equal the TX UID.
- The RX logs `tentative conn` within about 10 s: after a bind it searches through all packet rates. The TX link statistics show up LQ 100 at once. The down LQ rises over about 30 s at 50Hz while its 100-packet window fills, and then stays at 100.
- Both LEDs are solid.

Without the tool: press the TX Wio button 3 times quickly instead of running `--cmd Bind`.

### T9: WiFi and web UI

RX:

1. Unplug both boards. Plug in only the RX and wait 60 s. The LED flickers fast (WiFi mode, radio off). The RX starts WiFi only if it has not linked or entered bind mode since it booted.
2. Connect the PC WiFi to `ExpressLRS RX`, password `expresslrs`. The PC has no internet while it is connected.
3. Run:

   ```powershell
   curl.exe -s http://10.0.0.1/config
   curl.exe -s http://10.0.0.1/options.json
   curl.exe -s http://10.0.0.1/hardware.json
   ```

4. Open `http://10.0.0.1` in a browser and look at the Hardware Layout page.
5. Run `curl.exe -s http://10.0.0.1/reboot`, or unplug the board.

TX:

1. Run `& $py $mon --port "TX=$TXSER" --cmd "Enable WiFi" --exit`. The tool prints `WiFi Running...`. The `DEBUG_TX_FREERUN` build does not start WiFi by itself.
2. Connect the PC WiFi to `ExpressLRS TX`, password `expresslrs`, and repeat steps 3 to 5.

Pass:

- `/config` contains `"module-type":"RX"` (or `"TX"`), `"radio-type":"SX126X"`, `"reg_domain_low":"EU868"` and `"target":"UNIFIED_ESP32S3_SX126X_RX"` (or `_TX`).
- `/options.json` contains `"domain":2`.
- `/hardware.json` shows `radio_nss 5`, `radio_busy 4`, `radio_dio1 2`, `radio_rst 3`, `power_rxen 6`, `radio_tcxo 2`, `radio_tcxo_delay 320` and `radio_dio2_rfsw true`.
- The Hardware Layout page shows the TCXO voltage, the TCXO delay and the "DIO2 RF switch" fields.

### T10: CW frequency check with the RTL-SDR V4

The board sends an unmodulated carrier at 868.000 MHz and 10 mW. The RTL-SDR measures its frequency to about 1.5 kHz, and the difference between the two boards to about 0.1 kHz.

The recording covers 866.0 to 868.4 MHz in 293 Hz bins, so it holds the nominal 868.000 MHz and also the sync channel, 866.425 MHz. A carrier on the sync channel means the chip ignored the CW command's SetRfFrequency and kept the frequency `Config()` had set. The script prints a time line, one line per second, so the carrier can be seen switching on and off with the board.

1. Place the RTL-SDR antenna 1 to 2 m from the board. Power only the board under test, so that no link packets are on the air.
2. Choose how to start the carrier:
   - Web page (T9): put the board in WiFi mode and connect to it. `curl.exe -s http://10.0.0.1/cw` must show `{"radios": 1, "center": 868000000}`; if the center value is different, stop. In step 3, start the carrier with `curl.exe -s -o NUL -w '%{http_code}' -F radio=1 -F subGHz=1 http://10.0.0.1/cw` (prints `204`) or the page's Continuous Wave, Start button. The firmware has no stop command: unplug the board after 20 s.
   - Without WiFi: a TX debug build with `-DDEBUG_LOG -DDEBUG_CW_TEST` (a temporary flag in `tx_main.cpp`) sends a 20 s carrier 3 s after every reset (`-DDEBUG_CW_SECONDS=60` for 60 s) and logs `CW start #0 status 0x62 ...`. In step 3, reset it with `& $py $mon --port "TX=$TXSER" --reset TX --duration 30 --log "$logs\T10_A_log"`.
3. Start the recording, wait about 5 s, then start the carrier. The recording must cover the whole carrier and a few seconds before and after it:

   ```powershell
   & "$rtl\rtl_power.exe" -f 866000000:868400000:500 -g 0 -w blackman-harris -i 1 -e 45s "$logs\T10_A.csv"
   & $py $scan peak "$logs\T10_A.csv"
   ```

   rtl_power records one 2.4 MHz window centred on 867.2 MHz, without downsampling. The carrier is 800 kHz from the window centre. The script prints, per second, the strongest signal, how far it is above that second's median, and the level at 868.000 and at 866.425 MHz; lines with a carrier (20 dB or more above the median) are marked `*`. Then it prints the carrier frequency (the mean of the marked lines), its offset from 868.000 MHz in kHz and ppm, and the nearest EU868 channel.

4. Optional: type the measured frequency into the web page field "Measured Center Frequency". The page shows the offset of the board's 32 MHz TCXO in kHz and ppm.
5. Keep the carrier on for less than a minute in total.

Repeat with the other board right after the first, with the same command, and save to `T10_B.csv`. The SDR's own error then drops out of the difference.

If the script says `NO CARRIER`:

- Check that the SDR sees the board: at the same position and gain, T14 must show the link's packets.
- Record once more with `-g 29.7` instead of `-g 0`. A weak carrier at 868.000 MHz that shows up only at this gain means the synthesiser runs but little power reaches the antenna (PA or RF switch). Nothing at all means no RF.
- Optional wide search: record the T14 command while the carrier is on and run `peak` on that file. It covers 862.5 to 870.9 MHz.

If the script warns about another strong signal, the SDR is overloaded or another transmitter is on: move the SDR further away and measure again. A signal in only one or two lines is not the carrier.

Visual check with SDR++ or SDR# (optional): sample rate 2.4 MHz, gain 0 to 10 dB, AGC off, offset tuning off and bias tee off, centre 867.200 MHz, so that 868.000 MHz (right) and 866.425 MHz (left) are both in view. Set the FFT size to 65536 or more, zoom in on the carrier, and read its frequency under the mouse pointer.

Pass:

- The script finds a carrier (lines marked `*`) while the board sends it, and none before or after.
- Each board is within 10 kHz of 868.000 MHz. A working TCXO is usually within 3 kHz; the SDR adds up to about 1.3 kHz of its own.
- The two boards differ by 10 kHz or less.

No carrier at all, or `device errors 0x20` or `0x40` in T1 or T2, means the TCXO does not run: it is the only 32 MHz reference on the Wio-SX1262. If the chip reports TX mode (`status 0x62`) without a device error and the SDR still sees nothing, while it sees the link's packets at the same position and gain, the carrier is not radiated: check the driver's CW path (`startCWTest()`) and the RF switch. An offset of more than 10 kHz points to a wrong reference or frequency setting.

### T11: Soak test

Run 30 minutes at 50Hz and 10 mW. If you can, connect the two antenna ports by cable through at least 40 dB of attenuation (section 1.1). On air, shorten the run to 5 minutes (`--duration 300`). Use 50Hz until the 200Hz downlink works (section 1.8): at 200Hz the downlink LQ stays 0.

```powershell
& $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --set "Packet Rate=50Hz" --set "Telem Ratio=Std" --set "Max Power=10" --duration 1800 --ls-every 30 --log "$logs\T11_soak"
```

Pass: after the `set:` lines, no `lost conn`, `Timeout!`, `TLM crc error` or `BUSY` line and no new boot lines (the boards did not restart). In the summary, an average LQ of 99 or more up and down. Lines printed before the `set:` lines are old output the RX kept while no monitor was open (for example a `lost conn` from the previous test's rate change) and do not count.

### T12: Release build (no debug flags)

The final firmware has no debug log and no `DEBUG_TX_FREERUN`.

1. Run `Remove-Item Env:PLATFORMIO_BUILD_FLAGS`.
2. Flash both boards as in 2.4 and 2.5. The binding phrase is still in `super_defines.txt`.
3. Plug in both boards at about the same time. The TX LED blinks 200 ms on, 1 s off (no handset).
4. Within 30 s, press the TX Wio button 3 times quickly. The TX sends a bind for about 1 s and then keeps transmitting normal packets: the bind takes it out of the no-handset state. `--cmd Bind` from the tool does the same.
5. Watch the TX: `& $py $mon --port "TX=$TXSER" --log "$logs\T12"`. After the bind, the TX sends link statistics over USB in release builds too, but no text log.

Pass: both LEDs solid, `lq 100`, `power 10 mW`, and RSSI in the same range as in T3.

Both boards start WiFi 60 s after boot if they have no link by then (fast flicker). If that happens, unplug both and try again.

### T13: Per-packet link statistics (optional)

Set these flags, then flash both boards as in 2.4 and 2.5 (set `ELRS_UNIFIED_CONFIG` before each board):

```powershell
$env:PLATFORMIO_BUILD_FLAGS = '-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RCVR_LINKSTATS'
```

Both boards need this flag, because it changes the packet contents. The RX then prints one line per received packet with these fields:

- packet id
- antenna
- RSSI (dBm)
- LQ
- SNR (raw; divide by 4 for dB)
- power (CRSF code: 1 = 10 mW, 2 = 25 mW, 8 = 50 mW, 3 = 100 mW)
- channel (0 to 12)
- timer offset

Gaps in the packet id show lost packets. In this mode the RX sends nothing to a flight controller.

Record 2 minutes, then summarise the per-packet lines with [`hwtest/linkstats_summary.py`](hwtest/linkstats_summary.py):

```powershell
& $py $mon --port "TX=$TXSER" --port "RX=$RXSER" --set "Packet Rate=50Hz" --set "Max Power=10" --duration 120 --ls-every 30 --log "$logs\T13"
& $py $lss "$logs\T13.log"
```

The script prints:

- the number of packets
- the missing packet ids and where the gaps are
- the RSSI, LQ, SNR, power and timer-offset ranges
- the packets per channel, also as a fraction of the mean

Pass, at 1 to 3 m and 10 mW:

- The packet id counts up without gaps. Only RC packets carry an id, so sync packets and telemetry slots leave no gap. A packet lost on air also lowers the LQ to 99 in the lines that follow, so a gap while the LQ stays 100 is not a lost packet. This can happen in the RX's backlog from before the monitor opened the port (the lines before `set:`).
- All 13 channels (0 to 12) appear about equally often, within about 10 % of the mean. The sync channel (`sync=6` in the boot log) has about 5 % fewer, because a sync packet there sometimes takes the place of an RC packet. A channel that is missing or rare points to a wrong frequency at that hop.

### T14: Hopping channels with the RTL-SDR V4 (optional)

This checks that the link hops over exactly the 13 EU868 channels and does not sit on one frequency. On the SX1262 the hopping frequencies are calculated in Hz, which is new in this port. If the radios ignored SetRfFrequency at each hop, both would stay on the frequency `Config()` sets, the sync channel 6 (866.425 MHz), and the link would still work. T13 cannot show that: its channel column is the RX's hop table, not a measurement.

rtl_power sees 2.4 MHz at a time. With the command below it tunes to four windows of 2.1 MHz in turn (centres 863.54, 865.64, 867.74 and 869.84 MHz, 9.4 kHz bins) and looks at each for 3.4 ms, 2 to 4 times a second. Every window centre and edge falls in the 25 kHz gap between two channels. So a hopping link lights up each channel in some of the 2 s rows, and a link on one frequency lights up the same channel in every row.

1. Put the RTL-SDR 2 to 3 m from the boards. At 1 to 2 m and gain 20 the 10 mW signal overloaded it: the whole band rose by about 33 dB and all channels looked alike.
2. With both boards off, record a 20 s baseline:

   ```powershell
   & "$rtl\rtl_power.exe" -f 862487500:870887500:10000 -c 0.125 -g 0 -i 2 -e 20s "$logs\T14_base.csv"
   ```

3. Start the link as in T3 (200 Hz). Record 2 minutes and analyse it:

   ```powershell
   & "$rtl\rtl_power.exe" -f 862487500:870887500:10000 -c 0.125 -g 0 -i 2 -e 120s "$logs\T14_hop.csv"
   & $py $scan channels "$logs\T14_hop.csv" --baseline "$logs\T14_base.csv"
   ```

   Keep the frequencies in Hz and the crop exactly as shown; other values move window centres or edges onto channels. `& $py $scan plan 862487500:870887500:10000 --crop 0.125` shows the layout without the SDR.

For each channel the script prints the rows in which it carried signal, its mean level against the baseline and its share of the energy. A channel's level is the lowest quarter of its central 400 kHz, which a LoRa BW500 channel fills completely. So the SDR's spur at 864.000 MHz and narrower outside signals, such as the LoRa device on 869.525 MHz seen on this bench, do not count. The verdict is one of:

- `HOPPING over all 13 channels`: every channel carried signal, and each share lies between a quarter and four times the median share. Even use gives 7.7 % each.
- `HOPPING over all 13 channels, but uneven`: all 13 are used, some much more or less than the others. Reflections in the room can do this: move the SDR by half a metre and repeat. If the same channels stay odd, the link favours or skips them.
- `STUCK on channel N`: one channel has 80 % or more of the energy. On channel 6 (866.425 MHz) the radios never left the frequency set at start-up.
- `PARTIAL`: some channels never carried signal; the script lists them.
- `NO SIGNAL`: no channel rose 10 dB above the baseline. The link is off, or the SDR is too far away.
- `NO VERDICT`: the band outside 863 to 870 MHz rose more than 10 dB above the baseline, or the channels of one window rose and fell together: the SDR is overloaded. Move it further away, turn its antenna across, or take the antenna off, and record both files again. Even then, the line `strongest channel per row` and the `Hint:` tell a link on one frequency (the same channel in every row) from a hopping one.

Pass:

- Verdict `HOPPING over all 13 channels` (`but uneven` still passes for the channel set; note it in the results).
- No `WARNING: signal at ... outside the band`.

Visual check with SDR++ or SDR# (optional): gain 0 to 10 dB with AGC off. The waterfall shows 2.4 MHz at a time. Tune to 864.3, 866.4 and 868.5 MHz in turn. Each view shows four or five channels lighting up one after the other; a link on one frequency shows one steady channel.

FCC915 is tested only by building it, and it built in the earlier session. Do not flash it here.

## 4. Troubleshooting

| Symptom | Likely cause | What to check |
|---|---|---|
| `SX126x #1 not found, sync word reads 0xff 0xff` (the RX also prints `Failed to detect RF chipset!!!`) | The chip does not answer on SPI: NSS or SCK wrong, or the module is not powered or seated | Pins D4 (NSS) and D8 (SCK); 3.3 V on the Wio board |
| `... sync word reads 0x0 0x0` | MISO held low or on the wrong pin | Pin D9 (MISO); solder bridges |
| Repeated `SX126x BUSY timeout`, `BUSY still high` or, in debug builds, `SX126x BUSY <n> us after cmd 0x..` | BUSY on the wrong pin, the chip held in reset or unpowered, or a build without the `WaitOnBusyLong()` fix | Pin D3 (BUSY); D2 (RST) must be high after boot; [section 1.8](#18-driver-fixes-from-the-first-hardware-run) |
| `device errors 0x20` (sometimes with 0x04, 0x08, 0x10 or 0x40) | The TCXO did not start: wrong voltage or too short a delay | `SX126x TCXO voltage 2, delay 320` (2 = 1.8 V); try `radio_tcxo_delay` 640 |
| `device errors 0x10` or `0x40` | Image calibration or PLL | `Primary Domain EU868` in the log; the TCXO checks above |
| Driver log is correct, but the RX never connects | DIO1 on the wrong pin, or the TX is not transmitting | Pin D1 (DIO1); TX LED 500 ms on, 500 ms off means it transmits |
| Link only at very short range; uplink RSSI 20 to 40 dB below the downlink | TX board RF switch (DIO2) or RX board RXEN (D5) | `SX126x DIO2 RF switch on` and `Use RX pin: 6` on both boards; swap the boards to find the bad one |
| Downlink weak, uplink fine | The same parts, the other way round | As above |
| Downlink LQ poor only at 200Hz or D50 with 1:2 | Turnaround timing (0.36 ms free) | Compare with 100Hz; note it in the results |
| A debug build restarts right after a log line | A log call from an interrupt (USB logging is not interrupt-safe) | Repeat the test with the release build (T12) |
| TX LED 200 ms on, 1 s off, and the WiFi `ExpressLRS TX` appears at once | The radio did not start (radioFailed) | TX log (T2), or flash the RX debug build on that board and read T1 |
| T10 shows no carrier | The TCXO does not run, so the radio has no reference. With `status 0x62` and no device error: the carrier is not radiated | `device errors` 0x20 or 0x40 and `SX126x TCXO voltage 2, delay 320` in T1 or T2; otherwise the `NO CARRIER` checks in T10, the RF switch and `startCWTest()` |
| T10 carrier on 866.425 MHz instead of 868.000 MHz | The chip ignored the CW command's SetRfFrequency (for example sent while BUSY was high) and kept the `Config()` frequency | The BUSY waits before `SET_RFFREQUENCY` in `SetFrequencyReg()` |
| T10 carrier more than 10 kHz from 868.000 MHz | Wrong reference or frequency setting | Compare the two boards; note the value in the results |
| T14 `STUCK on channel 6` | The radios never change frequency: the chip ignores SetRfFrequency at each hop, for example because it comes while BUSY is still high after SetStandby(XOSC). T13 still shows all 13 channels | The BUSY wait before `SET_RFFREQUENCY` in `SetFrequencyReg()` |
| T14 `NO VERDICT` | SDR overloaded, or the baseline was recorded with other settings | More distance, antenna across or off; record the baseline again with the same command |
| T14 `PARTIAL`, or signal outside 863 to 870 MHz | Some hops ignored or wrong hopping frequencies, or SDR overload (images) | Repeat further away; if it stays, check `Primary Domain EU868` in the log and the hop path in the driver |
| `tentative conn`, then `Bad sync, aborting`; RX scoreboard `RR____...` | The radios are on different frequencies after a hop | A build without the `SetFrequencyReg()` fix ([1.8](#18-driver-fixes-from-the-first-hardware-run)) |
| 200Hz or D50: uplink fine, but TX `lq 0` down | Telemetry turnaround too tight (known issue) | Use 100Hz Full or slower for downlink tests |
| Build ends with `[FAILED]` and `.sconsign311.dblite: No such file` | PlatformIO cleaned the build folder after a flag change | Run the same command again |
| esptool stops with `StopIteration` right after `Stub running...` | The old esptool v4.2.1 in `src\python\external` | Use `$esptool` (PlatformIO's v4.9.0), as in 2.3 |
| esptool cannot connect | Automatic reset into the bootloader failed | The BOOT button steps in 2.3 |

With a multimeter, oscilloscope or logic analyser:

- RST (D2) is low for about 50 ms at boot, then high.
- BUSY (D3) is low when idle, with short high pulses after SPI commands.
- DIO1 (D1) pulses at the packet rate while the link runs.
- RXEN (D5) is about 3.3 V on the RX board, with short low dips when it sends telemetry. On the TX board it is about 0 V, with short high windows for telemetry. In WiFi mode it is 0 V on both.

## 5. Results sheet

| Test | Result (pass / fail) | Notes (values, log file) |
|---|---|---|
| T1 RX boot and radio start | | |
| T2 TX boot and radio start | | |
| T3 First link at 200 Hz | | |
| T4 200Hz | | |
| T4 100Hz Full | | |
| T4 100Hz | | |
| T4 50Hz | | |
| T4 25Hz | | |
| T4 D50 | | |
| T5 Telemetry 1:2 at 200Hz and D50 | | |
| T6 Power steps | | |
| T7 Range check | | |
| T8 Bind | | |
| T9 WiFi and web UI (RX, TX) | | |
| T10 CW frequency, board A (offset in kHz) | | |
| T10 CW frequency, board B (offset in kHz) | | |
| T11 Soak test | | |
| T12 Release build | | |
| T13 Per-packet link statistics | | |
| T14 Hopping channels | | |

## 6. Reference

### LED (GPIO21, both boards)

| LED | Meaning |
|---|---|
| Solid | Connected |
| 500 ms on, 500 ms off | Disconnected. On the TX: transmitting, waiting for telemetry |
| 200 ms on, 1 s off | TX without a handset, or radio start failed (then WiFi starts at once) |
| Double blink, 1 s pause | RX in bind mode |
| Triple blink, 1 s pause | Connected, but the model does not match |
| Fast flicker | WiFi mode (radio off) |
| Stops blinking for a moment | RX tentative connection, or TX waiting for a model ID |
| Off | No hardware layout (web setup mode) |

### EU868 frequencies

13 channels from 863.275 to 869.575 MHz, 525 kHz apart. The sync channel is 866.425 MHz, and the CW test uses 868.000 MHz.

### Known limits and traps

- The RX starts WiFi 60 s after boot if it has not linked or entered bind mode since it booted. A release TX without a handset, and without a bind before then, does the same. WiFi turns the radio off until the next restart.
- Holding the RX button for 12 s or more resets all RX settings.
- The SX126x has no frequency correction (`FrequencyErrorAvailable()` is false). The link relies on both TCXOs, and T10 checks them.
- The first packet after boot goes out at 14 dBm, and then the configured power applies.
- Some existing ExpressLRS log lines run inside interrupts. With USB logging they can rarely crash a debug build. If a debug build misbehaves, repeat the test with the release build.
- Do not use `DEBUG_INIT` on this board. It drives GPIO1 (button) and GPIO3 (radio reset) as a UART.
- `power_high` in the layout files is ignored by the firmware. It is harmless.

### Not covered by this plan

- Absolute output power at each level. That needs a calibrated power meter; the RTL-SDR is not calibrated.
- RX telemetry power settings (including MatchTX).
- A TX driven by a real CRSF handset on GPIO44 and GPIO43.
- Dual-radio code paths. This hardware has one radio per board.
- Over-the-air compatibility with SX127x-based ExpressLRS 900 MHz hardware.

## 7. After testing

```powershell
Remove-Item super_defines.txt
Remove-Item Env:PLATFORMIO_BUILD_FLAGS, Env:ELRS_UNIFIED_CONFIG -ErrorAction SilentlyContinue
```

Keep the log files in `Docs\hwtest\logs` for the results. Then decide whether the debug changes in section 1.5 stay (see the `git revert` note there).

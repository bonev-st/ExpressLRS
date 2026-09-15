# Wio-SX1262 hardware test report

Test plan: [`../Wio-SX1262-hardware-test-plan.md`](../Wio-SX1262-hardware-test-plan.md). Logs: [`logs/`](logs/).

- Date: 2026-09-15
- Firmware: branch `Wio-SX1262`, debug build (`-DDEBUG_LOG -DDEBUG_TX_FREERUN`, from the scoreboard runs on also `-DDEBUG_RX_SCOREBOARD`; T13 with `-DDEBUG_RCVR_LINKSTATS` instead of the scoreboard), binding phrase `wio-sx1262-test`, EU868
- Fixes made during the run (not committed yet): `WaitOnBusyLong()` race (root cause 1), frequency change through STDBY_XOSC (root cause 2), and a hop that comes during a transmission applied after TX_DONE (a guard; it was called root cause 3, but the [hop-deferral check](#hop-deferral-check-2026-09-16) shows it never runs); see [Diagnosis](#diagnosis-in-progress). Root cause 4, commands sent while BUSY is still high (found in T10), was fixed on 2026-09-16, and the suite was run again: see [Retest after the HAL fix](#retest-after-the-hal-fix-2026-09-16)
- Run by: Claude over USB. Claude did not move the boards and could not observe the LEDs.

## Boards

| Board | USB serial | COM | Role |
|---|---|---|---|
| A | E0:72:A1:F9:47:A0 | COM6 | TX |
| B | E0:72:A1:F9:47:B0 | COM9 | RX |

Both boards report UID `114, 26, 129, 203, 82, 62`.

## Results

The results of the retest after the HAL fix are in [their own section](#retest-after-the-hal-fix-2026-09-16).

| Test | Result | Evidence |
|---|---|---|
| T1 RX boot and radio start | Pass. Before the fixes, BUSY timeouts followed the boot | [`logs/T1T2_boot2.log`](logs/T1T2_boot2.log), [`logs/score200.log`](logs/score200.log) |
| T2 TX boot and radio start | Pass after the fixes (before: `Timeout!` on every second packet) | [`logs/T1T2_boot2.log`](logs/T1T2_boot2.log), [`logs/hopfix200.log`](logs/hopfix200.log) |
| T3 First link at 200 Hz | Partial: uplink 100 %, no downlink (telemetry). At 25 Hz both directions pass. The same after the HAL fix. The new 150Hz rate passes both ways | [`logs/hopfix200.log`](logs/hopfix200.log), [`logs/hopfix25.log`](logs/hopfix25.log) |
| T4 Packet-rate sweep | 25Hz, 50Hz, 100Hz, 100Hz Full pass; 100Hz failed only in the first run, with the telemetry ratio stuck at 1:2; D50 and 200Hz: uplink 100 %, no downlink | [T4 details](#t4-packet-rate-sweep) |
| T5 Telemetry 1:2 | Pass at 100Hz Full (down LQ 100, up 99.7); fail at 200Hz (no downlink, known timing limit). After the fix, 150Hz at 1:2 passes (100/100) | [T5 details](#t5-telemetry-turnaround-stress) |
| T6 Power steps | Pass: 10 to 25 mW raises the uplink RSSI by 3.7 dB (expected 4 ± 2) | [T6 details](#t6-power-steps) |
| T7 Range check | Not run (needs moving the boards) | |
| T8 Bind | Pass: RX put in bind mode by 3 resets, `--cmd Bind` on the TX, link back with LQ 100 up and down | [T8 details](#t8-bind) |
| T9 WiFi and web UI | Partial: RX WiFi AP appears after 60 s without a link; web checks not run | WiFi scan |
| T10 CW frequency | **Pass (2026-09-16)**: board A at -0.04 kHz, board B at +0.65 kHz from 868.000 MHz (rtl_power); the other board receives the carrier 40 to 60 dB above its noise floor. It failed at first because the HAL's SPI/BUSY race dropped or garbled commands. After the HAL fix the web UI's `startCWTest()` path radiates as well: A +0.28 kHz, B +0.52 kHz | [T10 details](#t10-cw-frequency) |
| T11 Soak test | Pass (5 min on air at 50Hz, 10 mW): LQ 100 up and down, no errors | [T11 details](#t11-soak-test) |
| T12 Release build | **Pass after the HAL fix (2026-09-16)**: release TX and RX bound with `--cmd Bind` keep LQ 100 up and down (279 frames, none below 100). Before the fix the release build did not link (items 1 and 2); the cause was the SPI/BUSY race (item 19, root cause 4) | [T12 details](#t12-release-build), [retest](#retest-after-the-hal-fix-2026-09-16) |
| T13 Per-packet link statistics | Pass: all 13 channels used evenly (0.95 to 1.01 of the mean), no packet lost in 2 min, LQ 100. The channels are the RX's hop-table entries, not measured on air (see T14) | [T13 details](#t13-per-packet-link-statistics) |
| T14 Hopping channels | **Pass (2026-09-16, after the HAL fix)**: with the SDR antenna removed and gain 20 (no overload: nothing outside the band), the link's energy sits on all 13 EU868 channels (5 to 10 % each) and nowhere else, with 10 different channels the strongest in turn: verdict HOPPING. The signal is weak there, so the script's thresholds were lowered to 3 dB above the floor and 6 dB contrast. With the antenna on, or at gain 28 and more without it, the SDR overloads | [T14 details](#t14-hopping-channels), [retest](#retest-after-the-hal-fix-2026-09-16) |

## Retest after the HAL fix (2026-09-16)

**The fix for root cause 4** (see T10 and T12 item 19), not committed yet:

- `SPIEx` gets `waitIdle()`, which waits until the SPI module has finished its transfer.
- `SX126xHal::WaitOnBusy()` calls it, waits at least 1 µs more (BUSY rises up to 600 ns after NSS goes high), and only then polls BUSY.
- `WaitOnBusyLong()` calls `waitIdle()` first as well.
- The TEMPORARY bisect block `SX126X_BISECT == 1` was removed.

**Also new: a 150Hz packet rate for the SX126x only**, added at the user's request because 200Hz leaves no time for the telemetry turnaround.

- It is the last row of the rate table: SF6, BW500, CR 4/7, 6666 µs, telemetry 1:32. `RATE_MAX` becomes 7.
- It is the first entry of the Lua list.
- No other ExpressLRS 900 MHz radio has this rate, so an SX127x or LR1121 receiver cannot link at it.

**Builds:**

- The link tests used the debug link builds, with `-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RCVR_LINKSTATS` (A = TX, B = RX).
- T10 used `-DDEBUG_CW_TEST`, the unchanged `startCWTest()` path that the web UI's Continuous Wave button uses.
- T12 used the release builds, without flags.

No log of this retest has a BUSY, `Timeout!` or `TLM crc error` line.

| Test | Result after the fix | Log |
|---|---|---|
| T1/T2 boot | Pass: device errors 0x0 on both boards, no BUSY line; the link comes up 4.4 s after the reset | [`logs/T1T2_fix.log`](logs/T1T2_fix.log) |
| T3 200Hz | The TX's uplink LQ stays 0 (no link statistics from the RX); downlink LQ average 22, up to 87 (before the fix: 0 and 0). The telemetry turnaround limit remains | [`logs/T3_fix.log`](logs/T3_fix.log) |
| T4 100Hz Full | Pass: up 99.9, down up to 100 | [`logs/T4_fix_100HzFull.log`](logs/T4_fix_100HzFull.log) |
| T4 100Hz | Pass: up 100, down 96 to 100 | [`logs/T4_fix_100Hz.log`](logs/T4_fix_100Hz.log) |
| T4 50Hz | Pass: up 100, down 94 to 100 | [`logs/T4_fix_50Hz.log`](logs/T4_fix_50Hz.log) |
| T4 25Hz | Pass: up 98.6 average (52 right after the rate change), down 94 to 100 | [`logs/T4_fix_25Hz.log`](logs/T4_fix_25Hz.log) |
| T4 D50 | Uplink LQ 0, downlink average 29: the turnaround limit | [`logs/T4_fix_D50.log`](logs/T4_fix_D50.log) |
| T4 200Hz | Uplink LQ 0 throughout: the turnaround limit | [`logs/T4_fix_200Hz.log`](logs/T4_fix_200Hz.log) |
| T5 1:2 | 100Hz Full: up 100, down 100, pass. 200Hz and D50: no downlink | [`logs/T5_fix_100full.log`](logs/T5_fix_100full.log), [`logs/T5_fix_200.log`](logs/T5_fix_200.log), [`logs/T5_fix_D50.log`](logs/T5_fix_D50.log) |
| T6 power steps | Pass: uplink RSSI -19.3 dBm at 10 mW, -15.9 dBm at 25 mW (+3.4 dB, expected 4 ± 2) | [`logs/T6_fix_10mW.log`](logs/T6_fix_10mW.log), [`logs/T6_fix_25mW.log`](logs/T6_fix_25mW.log) |
| T8 bind | Pass: the RX enters bind mode after 3 resets and gets `New UID = 0, 0, 129, 203, 82, 62` 40 ms after the TX's bind. The link is back 6 s later, up LQ 100 | [`logs/T8_fix.log`](logs/T8_fix.log) |
| T10 board A | Pass with `startCWTest()`: 868.000283 MHz (+0.28 kHz, +0.33 ppm), 64 dB above the median; chip status 0x62, err 0x0 | [`logs/T10_fix_A.csv`](logs/T10_fix_A.csv) |
| T10 board B | Pass with `startCWTest()`: 868.000521 MHz (+0.52 kHz, +0.60 ppm), 63 dB above the median. The boards differ by 0.24 kHz | [`logs/T10_fix_B.csv`](logs/T10_fix_B.csv) |
| T11 soak | Pass: 5 min at 50Hz and 10 mW, up LQ 100 (min 100), down LQ 99.8 average; RSSI -19.6/-20.3 dBm, SNR 12.8/12.4 | [`logs/T11_fix.log`](logs/T11_fix.log) |
| T12 release | Pass: after `--cmd Bind`, LQ 100 up and down for 75 s (279 frames, none below 100), RSSI -19/-19 dBm, SNR 13 | [`logs/T12_fix.log`](logs/T12_fix.log) |
| T13 statistics | Pass: 14093 packets in 5 min, none lost; all 13 channels 0.95 to 1.01 of the mean (0.95 is the sync channel) | [`logs/T11_fix.log`](logs/T11_fix.log) |
| T14 hopping | Pass: without its antenna, at gain 20, the SDR does not overload (nothing outside the band). The link's energy sits on all 13 channels (5 to 10 % each), and 10 different channels are the strongest in turn. Verdict: HOPPING over all 13 channels. The thresholds were lowered to 3 dB above the floor and 6 dB contrast (`channels --min-above 3 --min-contrast 6`), because the signal is weak. Gain 0 shows no signal; gain 28, 33 and 40 overload the SDR (+24 to +26 dB outside the band) | [`logs/T14_fix_g20_hop.csv`](logs/T14_fix_g20_hop.csv), [`logs/T14_fix_g20_base.csv`](logs/T14_fix_g20_base.csv) |
| 150Hz, telemetry Std (1:32) | Pass: up 100, down 97 average (the LQ window fills after the rate change). 8666 packets on all 13 channels (0.98 to 1.01 of the mean); the only gap, 160 ids, is the rate change itself | [`logs/T3_fix_150.log`](logs/T3_fix_150.log) |
| 150Hz, telemetry 1:2 | Pass: up 100, down 100, no frame below 100 | [`logs/T5_fix_150.log`](logs/T5_fix_150.log) |

Result:

- The fix breaks nothing, and it makes the release build and the web UI's CW test work.
- T14 confirms on air that the link hops over all 13 channels.
- 200Hz and D50 still lose the downlink, because their telemetry turnaround leaves only about 0.2 ms. 150Hz has about 2 ms and passes in both directions, even at 1:2.

Afterwards, at the user's request, 150Hz became the SX126x default and the temporary test code was removed:

- `TxConfig::SetDefaults` selects 150Hz for the SX126x, where the SX127x keeps 200Hz, and `RxConfig::SetDefaults` starts its rate cycling at the same row.
- Verified on hardware: board A was erased with `erase_flash`, so it built a fresh config, and it came up at `Packet Rate = 150Hz`, Telem Ratio Std (1:32), 10 mW. The link reached LQ 100 in both directions, with no BUSY or `Timeout!` line ([`logs/T150_default.log`](logs/T150_default.log)).
- `SPIExClass::waitIdle()` is compiled only for `RADIO_SX126X` builds, in the header and the source.
- Removed: the `DEBUG_PLL_SCAN`, `DEBUG_CW_*` and `SX126X_BISECT` blocks, `DebugPrintState` and its callers, the boot-time FS test, and the HAL's `noteCommand` tracking with the 30 ms BUSY report. The plain BUSY timeout message stays.
- Kept, because they are fixes rather than diagnostics: the BUSY waits, the retune through STDBY_XOSC, and the `pendingFreq` deferral, which guards a lost TX_DONE.
- All four builds compile after the cleanup: debug and release, TX and RX.

## Hop-deferral check (2026-09-16)

Session `Hopping-channels`, 02:07 to 02:12, at the user's request: does the hop deferral in `SetFrequencyReg()` (`pendingFreq`) ever run? A TEMPORARY `DEBUG_HOP_DEFER` build counted, on both boards, the hops that found the radio in TX, the hops applied at TX_DONE, and the TX_DONEs that never came. `loop()` printed the counters every 5 s. The counter code was removed again after the run.

- Build: `-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RX_SCOREBOARD -DDEBUG_HOP_DEFER`, 10 mW, A = TX, B = RX.
- The first run repeats the failing T4 case: 100Hz with Telem Ratio 1:2, the ratio the monitor tool's ping had forced on 2026-09-15.
- The counter column is deferred / applied / TX timeouts, and holds for both boards. Logs: `logs/hc_*.log`.

| Run | RX scoreboard | Missed after a `T` | Counters | TX link statistics |
|---|---|---|---|---|
| 100Hz, 1:2, 60 s | 2982 `R`, 2934 `T`, 1 `_` | 0 | 0 / 0 / 0 | up 100, down 100 (234 frames) |
| 100Hz, Std, 45 s | 4320 `R`, 157 `T`, 1 `_` | 0 | 0 / 0 / 0 | up 100, down 100 |
| 150Hz, 1:2, 30 s | 2152 `R`, 2128 `T`, 1 `_` | 0 | 0 / 0 / 0 | up 100, down 100 |
| 100Hz Full, 1:2, 30 s | 1455 `R`, 1460 `T`, 1 `_` | 0 | 0 / 0 / 0 | up 100, down 100 |
| 50Hz, 1:2, 30 s | 730 `R`, 736 `T`, 1 `_` | 0 | 0 / 0 / 0 | up 99.9, down 99.9 |
| 25Hz, 1:2, 30 s | 350 `R`, 357 `T`, 1 `_` | 0 | 0 / 0 / 0 | up 94.9, down 97.5 |
| 200Hz, 1:2, 30 s | 2853 `R`, 2889 `T`, 1 `_` | 0 | 0 / 0 / 0 | no downlink |
| D50, 1:2, 30 s | 2751 `R`, 2793 `T`, 1 `_` | 0 | 0 / 0 / 0 | no downlink |

Result:

- In 13454 telemetry packets and about 9400 hops, no hop found the radio still in TX. `pendingFreq` never runs in normal operation, on either board.
- That follows from the schedule. The TX hops in `TXdoneISR()`, which runs after TX_DONE. The RX hops in the tock before it sends telemetry (`rx_main.cpp`: `OtaNonce++`, then `HandleFHSS()`, then `HandleSendDataDl()`). Telemetry falls on nonces divisible by the ratio and hops on nonces divisible by the hop interval (4, or 2 at 25Hz and D50), so the next hop comes at least 2 slots after a telemetry packet starts, while every rate's packet is shorter than one slot.
- A hop can therefore only find the radio in TX when a TX_DONE was lost, as in the single RX `Timeout!` in [`logs/T4_100Hz.log`](logs/T4_100Hz.log).
- The T4 `T_T_T_` failure is gone: the same rate and ratio now give 0 misses after telemetry. The guard did not do that, because it never ran. The HAL fix for root cause 4 is the likely cure, which was not proven by a separate run.
- The guard stays in the driver as a safeguard against a lost TX_DONE, not as the fix for T4.
- The 25Hz and 50Hz averages below 100 are the LQ window refilling after the rate change. The uplink LQ climbs in even steps from 2 (25Hz: +3 per frame, full after 8 s; 50Hz: +6 per frame, full after 4 s), with no packet lost.

The debug build crashed the RX once, at the rate change to D50: `Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)`, followed by a reboot. The decoded backtrace points at debug logging, not at the radio code:

1. `loop()` → `LostConnection()` → `SetRFLinkRate()` → `hwTimer::updateInterval()` was writing a `DBGLN` through `HWCDC::write()` and held the USB CDC lock.
2. The DIO1 interrupt preempted it: `IsrCallback()` → `TXnbISR()` → `TXdoneISR()` → `DBGW('T')` for the scoreboard.
3. That call took the same lock from ISR context (`xQueueSemaphoreTake`), which corrupted the FreeRTOS lists and spun until the interrupt watchdog fired.

Builds with `-DDEBUG_RX_SCOREBOARD`, or with any other logging from an ISR, can hit this at a busy moment such as a rate change. Release builds do not log.

## Left for a manual run

These tests need someone at the bench, or the PC's WiFi. Claude did not join the boards' access points, because that would cut the PC's network connection.

- T7 range check: the boards must be moved apart.
- T9 web UI: join `ExpressLRS RX` or `ExpressLRS TX` and run the `curl.exe` checks.
- The LED checks in T1, T3, T8 and T12: Claude could not see the LEDs.

## Details

### T1: RX boot and radio start

Every expected line appears, in order: `UID=(114, 26, 129, 203, 82, 62) ModelId=255`, `Primary Domain EU868, 13 channels, sync=6`, `Hal Init`, `SX126x Reset`, `SX126x Ready!`, `SX126x Begin`, `RFAMP_hal Init`, `Use RX pin: 6`, `SX126x #1 found`, `SX126x TCXO voltage 2, delay 320`, `Enabling DCDC regulator`, `SX126x DIO2 RF switch on`, `SetPower: 14`, `SX126x #1 device errors 0x0`, `SetPower: 10`, `hwTimer Init`.

After boot the RX searched through the packet rates (`hwTimer interval: 5000`, `10000`, `20000`, `40000`) and printed 6 `SX126x BUSY timeout` lines in 25 s.

The TX was not unplugged. It was powered but never produced a link, so the RX saw no packets either way.

### T2: TX boot and radio start

The boot lines match the RX, including `SX126x #1 device errors 0x0`, plus `About to start CRSF task...` and `hwTimer resume`.

Once the TX starts transmitting, it prints `Timeout!` for about every second packet (1882 in 25 s) and `SX126x BUSY timeout` 50 times. `Timeout!` means the driver started a transmission and never got the TX_DONE interrupt on DIO1.

### T3: First link at 200 Hz

No link. The TX link statistics stay at `lq 0`, and the RX never logs `tentative conn`. After 60 s without a link the RX starts its WiFi access point (`ExpressLRS RX` appears in the PC's WiFi scan).

After root causes 1 and 2 were fixed (see [Diagnosis](#diagnosis-in-progress)), the uplink at 200 Hz is 100 %, but the TX receives no telemetry. At 25 Hz both directions pass.

### T4: Packet-rate sweep

Each rate ran for 45 s after `--set "Packet Rate=..."`, without restarting the RX (25 Hz: 40 s, from [`logs/hopfix25.log`](logs/hopfix25.log)). Logs: `logs/T4_<rate>.log`.

| Rate | RX scoreboard | TX link statistics at the end | Result |
|---|---|---|---|
| 25Hz | 694 `R`, 100 `T`, 6 `s`, 0 `_` | up LQ 100, down LQ 100, -18/-20 dBm, SNR 11 | Pass |
| 50Hz | 1693 `R`, 300 `T`, 7 `s`, 0 `_` | up LQ 100, down LQ 100, -34/-33 dBm, SNR 12 | Pass |
| 100Hz Full | 5745 `R`, 825 `T`, 27 `s`, 3 `.` | up LQ 100, down LQ 100, -34/-35 dBm, SNR 10 | Pass |
| 100Hz | 270 `R`, 2000 `T`, 1683 `_`, 47 `.` | up LQ 21, down LQ 15, -49/-39 dBm | Fail |
| D50 | 4333 `R`, 4400 `T`, 67 `s` | up LQ 0, down LQ 0 (no telemetry) | Fail: no downlink |
| 200Hz | 4339 `R`, 4400 `T`, 60 `s`, 1 `_` | up LQ 0, down LQ 0 (no telemetry) | Fail: no downlink |

- The TX's "up LQ" comes from the RX through telemetry. Without telemetry it reads 0, although the RX scoreboard shows the uplink working (D50, 200Hz).
- After each rate change the RX logged `New TLMrate 1:2`. The monitor tool caused it. Its `--set` started with a broadcast device ping, which the TX also forwards to the RX as uplink data. Uplink data boosts the telemetry ratio to 1:2 until the RX replies (`tx_main.cpp:594-602`). Where the downlink worked (50Hz, 100Hz Full), the reply arrived and the ratio returned to the default (1:16, 1:32). Where it did not (100Hz, D50, 200Hz), it stayed at 1:2, so the RX sent telemetry (`T`) in every second slot. The tool now pings only the TX.
- Every rate change caused the expected `Req air rate change` and 1 or 2 `lost conn`, and the RX followed the TX without a restart.
- 100Hz: after each telemetry packet the RX misses the next RC packet (`T_T_T_`: 1777 of the 1778 misses follow a `T`), and the TX logs 18 `TLM crc error`. At SF7 a packet takes 8.77 ms of the 10 ms slot, so the turnaround is tight. This was first read as a hop arriving during the telemetry transmission, and called root cause 3. That explanation is wrong. A hop comes at least 2 slots after a telemetry packet starts, and every rate's packet is shorter than one slot, so a normal telemetry packet never overlaps a hop; half of these misses follow a telemetry packet with no hop in that slot or the next. The [hop-deferral check](#hop-deferral-check-2026-09-16) of 2026-09-16 confirms it. The likely cause is [root cause 4](#root-cause-4-commands-sent-while-busy-is-still-high), which was still unfixed here.

Re-tests with the hop-deferral guard and the TX-only ping. The ping change also put the telemetry ratio back to its default, so these runs do not show what the guard did:

| Rate | RX scoreboard | TX link statistics at the end | Result | Log |
|---|---|---|---|---|
| 100Hz | 3674 `R`, 119 `T`, 7 `s`, 0 `_`, 0 `.` | up LQ 100, down LQ 100, -33/-33 dBm, SNR 12/13; 0 `TLM crc error` | Pass | [`logs/pend100.log`](logs/pend100.log) |
| 200Hz | 7433 `R`, 119 `T`, 48 `s`, 0 `_` | up LQ 0, down LQ 0: no telemetry received | Fail: no downlink | [`logs/pend200.log`](logs/pend200.log) |

The 100 Hz run uses the default ratio 1:32 and the 200 Hz run 1:64 (`New TLMrate`), so neither repeats the 1:2 case that failed. The downlink limit at 200Hz and D50 is the telemetry timing described under problem 3.

### T5: Telemetry turnaround stress

Telemetry ratio 1:2, so the radios switch between TX and RX in every slot. Logs: [`logs/T5_100full.log`](logs/T5_100full.log), [`logs/T5_200.log`](logs/T5_200.log).

| Rate | Time | RX scoreboard | TX link statistics | Result |
|---|---|---|---|---|
| 100Hz Full | 45 s | 2186 `R`, 2200 `T`, 8 `s`, 6 `.` | up LQ 99.7 (min 98), down LQ 100 (min 100), -33/-33 dBm, SNR 10 | Pass |
| 200Hz | 30 s | 2863 `R`, 2900 `T`, 37 `s` | up LQ 0, down LQ 0 | Fail: no downlink |

There were no BUSY, `Timeout!` or `TLM crc error` lines. At 100Hz Full (3 ms of slack per slot) the turnaround works in every slot. At 200Hz no telemetry arrives even at 1:2, which confirms the timing limit. The ratio was set back to Std afterwards.

### T6: Power steps

At 50Hz, 30 s at each power level. The boards were close together, so the operating point was stronger than the plan asks for. Logs: [`logs/T6_10mW.log`](logs/T6_10mW.log), [`logs/T6_25mW.log`](logs/T6_25mW.log).

| Max Power | Uplink RSSI (at the RX) | Downlink RSSI (at the TX) | LQ up / down | Power in link statistics |
|---|---|---|---|---|
| 10 mW | -32.2 dBm average | -31.8 dBm | 100 / rising to 80 | 10 mW |
| 25 mW | -28.5 dBm average | -31.8 dBm | 100 / 100 | 25 mW |

- The uplink RSSI rises by 3.7 dB (expected 4 ± 2). The downlink RSSI does not change, because the RX sends telemetry at a fixed 10 mW. Pass.
- The TX was set back to `Max Power = 10mW` afterwards.
- This run also shows the full link at 50Hz: LQ 100 both ways, SNR 12 to 14 dB.

### T8: Bind

Done without touching the boards. Logs: [`logs/T8_rx.log`](logs/T8_rx.log), [`logs/T8.log`](logs/T8.log), [`logs/T8_after.log`](logs/T8_after.log).

1. The TX was put in the ROM download mode (esptool `--after no_reset read_mac`, which ends with `Staying in bootloader.`), so it stopped transmitting. The RX clears its power-on count as soon as it links (`rx_main.cpp:859-862`). With the TX running, 3 resets would never reach bind mode.
2. The RX was reset 3 times, 1.2 s apart, with the new `--reset-count 3`. On the third boot it logged `Power on counter >=3, enter binding mode` and `Entered binding mode at freq = 866425000`.
3. `--reset TX --cmd Bind`: the TX booted (`device errors 0x0`) and sent the bind.

| Time | Board | Line |
|---|---|---|
| 16:50:02.468 | TX | `Entered binding mode at freq = 866425000`, `cmd: Bind (command, executing) Binding...` |
| 16:50:02.507 | RX | `New UID = 0, 0, 129, 203, 82, 62` |
| 16:50:02.515 | RX | `lost conn`, `Exiting binding mode` |
| 16:50:03.488 | TX | `Exiting binding mode` |
| 16:50:04.987 | TX | `cmd: Bind (command, idle)` |
| 16:50:08.567 | RX | `tentative conn` |

- The RX received the bind packet 39 ms after the TX entered bind mode.
- The last four UID bytes equal the TX UID (`114, 26, 129, 203, 82, 62`). The bind packet carries only 4 bytes, so the RX sets the first two to 0 (`OnELRSBindMSP()`, `rx_main.cpp:924-932`).
- The RX linked again 6 s after it left bind mode. After a bind it searches through all packet rates.
- In the first 20 s the down LQ rose 24, 45, 57, 65 while its window filled. A 40 s run a minute later ([`logs/T8_after.log`](logs/T8_after.log)) showed up LQ avg 100.0 and down LQ avg 100.0 (both min 100), -32/-32 dBm and SNR 12.8, with no `lost conn`, BUSY, `Timeout!` or `TLM crc error`.

Result: pass. The LEDs were not checked.

### T10: CW frequency

The plan's method needs the board's WiFi web page, and joining its access point would cut the PC's network. Instead, a temporary TX build flag, `DEBUG_CW_TEST` in `tx_main.cpp` `setup()`, runs the same steps as the web page's Continuous Wave button. 3 s after every reset it sends a 20 s carrier at `FHSSconfig->freq_center` (868.000 MHz) at the minimum power (10 mW), then stays silent. The RTL-SDR antenna was 1 to 2 m from the board, at gain 0.

| Board | Recording | Result |
|---|---|---|
| A | 867.95 to 868.15 MHz, 97.66 Hz bins, right after the flash | No carrier: the strongest bin is 3.3 dB above the median |
| A | The same after a reset. The log shows `CW test: 868000000 Hz at the minimum power for 20 s` and `carrier off` 20 s later | No carrier (1.1 dB) |
| B | 867.95 to 868.15 MHz, right after the flash | No carrier (0.9 dB) |
| A | 850 to 880 MHz, 5.3 kHz bins, after a reset (`CW test` logged again) | Nothing: the strongest bins are 2.5 dB above the median, and no bin switched on and off by more than the noise |
| B | 850 to 880 MHz, the same way (`CW test` logged at 22:57:59, `carrier off` at 22:58:19) | Nothing: the strongest bins are 2.1 dB above the median |
| A | CW build that logs the chip status 100 ms after SetTxContinuousWave and at the end; 862 to 871 MHz, 8.8 kHz bins, after a reset | The chip reports `CW start #0 status 0x62 irq 0x0 err 0x0 … freq 868000000` and the same at `CW end` 20 s later: TX mode, no device error, so the PLL locked. The SDR still shows nothing (the strongest bin is 1.5 dB above the median). Log: [`logs/T10_status_A_log.log`](logs/T10_status_A_log.log) |
| A | The same at SDR gain 20 (sensitivity check) | The chip again reports TX mode without error for 20 s. The SDR sees the outside LoRa signal at 869.48 to 869.58 MHz, 13 dB above the median and switching on and off, so it is sensitive. There is nothing at 868.000 MHz. Log: [`logs/T10_search_A_g20.csv`](logs/T10_search_A_g20.csv) |
| A | 60 s carrier (`-DDEBUG_CW_SECONDS=60`) for a check by the user with their own SDR software, 23:07:55 to 23:08:55. Claude's tools left the SDR alone | The chip reports `status 0x62 err 0x0 … freq 868000000` at the start and at the end. The user's check came later (next row). Log: [`logs/T10_manual_A.log`](logs/T10_manual_A.log) |
| A | Four 60 s carriers restarted by USB resets, 23:16:28 to 23:20:35, for the user's check with their own SDR software. Claude's tools left the SDR alone | The chip reports `CW start #0 status 0x62 irq 0x0 err 0x0 … freq 868000000` in all four. **The user saw no carrier.** Their SDR showed other transmitters, weak and not at 868 MHz. Logs: [`logs/T10_manual_A1.log`](logs/T10_manual_A1.log) to [`logs/T10_manual_A4.log`](logs/T10_manual_A4.log) |
| A + B | 23:35:44 to 23:36:45. Board A: 60 s carrier at 10 dBm, the first 30 s with RF_SW1 (GPIO6) LOW, then 30 s HIGH (`-DDEBUG_CW_RFSW_AB`). Board B: a receive-only build (`-DDEBUG_CW_LISTEN`) that listens at 868.000 MHz and logs the RSSI every 500 ms. The user watched with their own SDR | Board A reports `status 0x62 err 0x0` (TX) at the start and the end. **The user saw no carrier in either half.** **Board B could not receive at all:** right after its SetRx it reports `status 0x3a irq 0x0 err 0x40`, a failed command with PLL_LOCK, the same error as the T12 SetTx failures. Its RSSI reads −127 dBm throughout (the reading of a receiver that is not running), before, during both halves and after. The same boot's FS test at 866.425 MHz locks. Log: [`logs/T10_ab1.log`](logs/T10_ab1.log) |

Logs: [`logs/T10_A.csv`](logs/T10_A.csv), [`logs/T10_A2.csv`](logs/T10_A2.csv), [`logs/T10_A2_log.log`](logs/T10_A2_log.log), [`logs/T10_B.csv`](logs/T10_B.csv), [`logs/T10_search_A.csv`](logs/T10_search_A.csv), [`logs/T10_search_A_log.log`](logs/T10_search_A_log.log), [`logs/T10_search_B.csv`](logs/T10_search_B.csv), [`logs/T10_search_B_log.log`](logs/T10_search_B_log.log).

No recording showed a carrier, and neither did the user's own check. The chip reports TX mode without error at the start and at the end of every carrier. At gain 20 the SDR sees a weak outside LoRa signal at 869.5 MHz, so it would see a 10 mW carrier 1 to 2 m away. In T14 the link's packets overloaded the same SDR at gain 20.

So the chip is in TX mode, but no carrier reaches the air, or it is at least 30 dB weaker than the link's packets. Between the two status readouts the firmware only waits (`delay()`), so nothing else touches the radio.

The narrow recordings from 867.95 to 868.15 MHz (`T10_A`, `T10_A2`, `T10_B`) are not valid evidence anyway. For spans under 1 MHz, rtl_power sums samples into int16 before the FFT, with no scaling, so a strong carrier wraps. The wide recordings are valid, and they were empty.

#### Why T10 failed at first

The fault was in the SX126x HAL, not in the radio, the TCXO, the PLL or the RF switch.

1. `SPIEx::write()` starts an SPI transfer and returns while the frame is still being clocked out. `SPIEx.cpp` waits for the previous transfer, not for its own.
2. So `WaitOnBusy()` before the next command samples BUSY while the previous frame is still on the bus. BUSY only rises after NSS goes high, so the check reads low.
3. The next frame then starts straight after the previous one, while the chip is still busy with it. The SX1262 drops or garbles a command that arrives while BUSY is high.

What this did here:

- In `startCWTest()`, SetRfFrequency, SetTxParams and SetTxContinuousWave go out back to back this way. The chip ends up in TX mode (status 0x62, no error), but with a setting lost or garbled, and no carrier leaves the antenna. It stayed silent even at 866.425 MHz, where no retune is needed (round 1 below).
- Board B's listener failed the same way: its SetRx after SetRfFrequency ended in PLL_LOCK (0x3a/0x40). This is also the mechanism behind the T12 SetTx failures.
- The FS "lock test" at boot looked fine only because FS mode does not check the PLL lock, so status 0x42 never proved a lock.

#### Resolution (2026-09-16, session `carrier-frequency-check`)

New TEMPORARY diagnostics in `SX126x.cpp` wait for BUSY after every command: `DebugLockRx`, `DebugPllScan` and `DebugTxMode`, behind the build flag `DEBUG_PLL_SCAN`.

| Step | Setup | Result | Log |
|---|---|---|---|
| Lock scan | Both boards. SetRx at 13 frequencies from 863.275 to 869.575 MHz, 868.000 first, with 4 sequences: STDBY_XOSC, STDBY_RC, STDBY_RC + Calibrate(PLL), LDO | Status 0x52 (RX), err 0x0 everywhere: 104 of 104 attempts lock | [`logs/T10_4d_r1.log`](logs/T10_4d_r1.log) |
| Round 1 | A: the old `startCWTest()` at 866.425 MHz, no retune. B: listens at 866.425 MHz (locked) | B's RSSI stays at -85 dBm: no carrier | [`logs/T10_4d_r1.log`](logs/T10_4d_r1.log) |
| Round 2 | A: `DebugTxMode` at 10 dBm, 8 s each: CW at 866.425, CW at 868.000, infinite preamble at 868.000, packets at 868.000. B: alternates 866.425 and 868.000, peak of 5 readings | B (floor -86 dBm): CW 866.425 gives -25 to -43 dBm, CW 868.000 gives -27 to -37 dBm, preamble -19 dBm, packets -18 dBm (427 sent). Every mode radiates | [`logs/T10_4d_r2.log`](logs/T10_4d_r2.log) |
| B detects A | A: 45 s CW at 868.000 MHz, 10 dBm | B: -32 to -53 dBm at 868.000 MHz, floor -86 dBm | [`logs/T10_4d_sdr.log`](logs/T10_4d_sdr.log) |
| 5 min, A | A: 5 min CW at 868.000 MHz, LED at 1 s. B: listener, LED at 0.1 s | A: status 0x62 err 0x0 every 30 s. B: 568 readings from -24 to -52 dBm (average -40). The user saw the carrier in SDR# after a settings change | [`logs/T10_4d_5min.log`](logs/T10_4d_5min.log) |
| 5 min, B | Roles swapped | A received -21 to -25 dBm. The user saw B's carrier on frequency | [`logs/T10_4d_5min_B.log`](logs/T10_4d_5min_B.log) |
| rtl_power, B | `rtl_power -f 866000000:868400000:500 -g 0 -w blackman-harris -i 1 -e 45s`: one window, 293 Hz bins | 868.000649 MHz: **+0.65 kHz (+0.75 ppm)**, 63 dB above the median | [`logs/T10_B_sdr.csv`](logs/T10_B_sdr.csv) |
| rtl_power, A | The same, with the 40 s carrier | 867.999959 MHz: **-0.04 kHz (-0.05 ppm)**, 64 dB above the median | [`logs/T10_A_sdr.csv`](logs/T10_A_sdr.csv) |

- The boards differ by 0.69 kHz. The SDR's own error is the same for both: up to about 0.9 kHz from its 1 ppm TCXO, plus 0.4 kHz from its tuner's PLL steps.
- The user read board A as more than 5 kHz off in SDR#. The rtl_power measurement does not bear this out.
- In the user's first check (00:26), SDR# showed nothing, although board B received the carrier at -35 dBm. After a settings change in SDR#, the carrier was visible.
- Parking the ESP32 in its ROM bootloader does not stop a running SX1262 carrier. Board B stayed on air that way for about 2 minutes (00:53:51 to 00:55:38), until its firmware reset the radio. The 40 s CW build now ends the carrier itself.

Result: **pass**. Both boards are within 1 kHz of 868.000 MHz (limit 10 kHz) and differ by 0.69 kHz (limit 10 kHz). The web UI's Continuous Wave button uses `startCWTest()`, which will keep hitting the HAL race until the HAL is fixed.

Next: fix the race in `SX126x_hal.cpp`. Before `WaitOnBusy()` samples BUSY, it must wait until the SPI transfer has finished and then allow for the BUSY rise time (up to 600 ns). `WaitOnBusyLong()` needs the same. Then repeat T3, T4, T12 and T14, because the link's hops go through the same path.

### T11: Soak test

The run was 5 minutes on air at 50Hz, Telem Ratio Std (1:16 at 50Hz) and 10 mW. It used 50Hz instead of the 200Hz the plan first gave, because the 200Hz downlink does not work yet (T5). Log: [`logs/T11_soak.log`](logs/T11_soak.log).

| Check | Result |
|---|---|
| RX scoreboard, 300 s | 14006 `R`, 937 `T`, 57 `s`: all 15000 slots, no `_` or `.`, so no packet was lost |
| TX summary, 1228 link-statistics frames | up LQ avg 100.0 (min 100), down LQ avg 100.0 (min 100), 0 frames below 100 |
| RSSI | up avg -30.6 dBm, down avg -30.2 dBm |
| SNR | up avg 12.8, down avg 12.9 |
| `lost conn`, `Bad sync`, BUSY, `Timeout!`, `TLM crc error`, reboots | 0 after the `set:` lines |

Two `lost conn` lines came before the `set:` lines. They were old RX output from the rate change at the end of T5, kept until the monitor opened the port. Result: pass.

### T12: Release build

Both boards were flashed without debug flags (EU868, UID `114,26,129,203,82,62`, esptool v4.9.0, all images verified). Logs: [`logs/T12.log`](logs/T12.log), [`logs/T12_rxdebug.log`](logs/T12_rxdebug.log).

1. Release TX and release RX: `--reset TX --cmd Bind` ran (`Binding...`, then `idle`), but the TX link statistics stayed at LQ 0 for 60 s. The RX started its WiFi access point 60 s after boot (`ExpressLRS RX` appeared in the PC's WiFi scan), so it had no link. The TX did not start WiFi, so the bind had taken it out of the no-handset state.
2. Release TX and debug RX, the RX firmware that linked in every earlier test: the RX searched through all packet rates for 45 s and never logged `tentative conn`. So the release TX sends nothing the RX can receive.

After a bind, the TX follows the same path as the debug build: `ExitBindingMode()` → `UARTconnected()` → `awaitingModelId`, which `UpdateConnectDisconnectStatus()` leaves after `DisconnectTimeoutMs`.

3. Release TX with a 1 µs wait before the first BUSY read in `WaitOnBusy()` (as RadioLib does), and the debug RX: no `tentative conn` in 45 s. So the first suspicion, a BUSY race that the debug diagnostics had hidden, does not explain T12. Log: [`logs/T12_fix.log`](logs/T12_fix.log).
4. TX built with `-DDEBUG_LOG` only, and the debug RX: no link. This TX has the same driver code and diagnostics as the working debug build; only `DEBUG_TX_FREERUN` is missing. Log: [`logs/T12_txlog.log`](logs/T12_txlog.log).

   ```text
   17:26:18.333 [TX] Entered binding mode at freq = 866425000
   17:26:19.372 [TX] Exiting binding mode
   17:26:23.532 [TX] Timeout! #0 status 0x3a irq 0x0 err 0x40 dio1 0 freq 867475000
   17:26:23.572 [TX] Timeout! #1 status 0x3a irq 0x0 err 0x40 dio1 0 freq 867475000
   17:26:23.632 [TX] Timeout! #2 status 0x3a irq 0x0 err 0x40 dio1 0 freq 867475000
   ```

   - The bind itself runs without errors.
   - `status 0x3a` means the chip is in STDBY_XOSC and reports "failure to execute command". `err 0x40` is PLL_LOCK: the SetTx failed because the synthesizer did not lock.
   - The failures start 4.2 s after the bind, about when the TX leaves `awaitingModelId` and sends its first normal packets.
   - The diagnostic prints only #0 to #2 and then every 1000th. No #1000 came in the next 42 s.
   - Without a handset, `CRSFHandset::UARTwdt()` switches the handset UART baud rate every 250 ms (`Too many bad UART RX packets!`, `UART WDT: Switch to: 400000 baud`). `DEBUG_TX_FREERUN` turns this off.

5. The same TX build (`-DDEBUG_LOG` only), bound again. It failed the same way: 3 `Timeout!` lines 4.16 s after the bind exit, with the same status, error and frequency. Then the packet rate was changed to 25Hz, which runs the radio configuration again. The debug RX still received nothing in 30 s, so a fresh configuration does not clear the fault. The rate was set back to 50Hz afterwards. Logs: [`logs/T12_e7a.log`](logs/T12_e7a.log), [`logs/T12_e7b.log`](logs/T12_e7b.log).

6. The same TX build, left idle for 30 s after boot and then bound. The bind itself ran without a timeout. 4.37 s after the bind exit came the same 3 `Timeout!` lines (`status 0x3a`, `err 0x40`), this time at 864.850 MHz. So neither idle time nor one particular channel causes the failure. It comes on the first normal packets after the bind. Logs: [`logs/T12_e8a.log`](logs/T12_e8a.log), [`logs/T12_e8b.log`](logs/T12_e8b.log).

7. A TX built with `-DDEBUG_LOG` and a temporary flag that turns off only the UART watchdog (`DEBUG_TX_NO_UARTWDT`), so there were no `UART WDT` lines and no connection forced at boot. It failed the same way: 3 `Timeout!` lines 4.16 s after the bind exit, and no link. So the UART watchdog is not the cause. Log: [`logs/T12_nowdt.log`](logs/T12_nowdt.log).

8. The same build as in 7, bound twice. The TX's USB backlog from the previous run showed `Timeout! #3000` (status 0x3a, err 0x40, 869.050 MHz). So once the failures start, every SetTx fails, one `Timeout!` every 40 ms: the driver marks the chip as in TX even when SetTx fails, and catches that at the next slot. The first bind failed as before. A second bind right after it did not bring the link back either. Logs: [`logs/T12_e10a.log`](logs/T12_e10a.log), [`logs/T12_e10b.log`](logs/T12_e10b.log).

9. Probe build (`-DDEBUG_LOG` with temporary diagnostics). At boot and at the first failed SetTx, the TX read back settings that only `Begin()` writes. Then it tried FS on the failing frequency, before and after a full `Calibrate(0x7F)`. Log: [`logs/T12_probe.log`](logs/T12_probe.log).

   ```text
   boot:  packet type 1, OCP 0x38, RX gain 0x96, TX clamp 0xfe, TX modulation 0x4
   Timeout! #0 status 0x3a irq 0x0 err 0x40 dio1 0 freq 865900000
   probe: packet type 1, OCP 0x38, RX gain 0x96, TX clamp 0xfe, TX modulation 0x8
   probe FS #0 status 0x42 irq 0x0 err 0x0 dio1 0 freq 865900000
   probe FS after Calibrate #0 status 0x42 irq 0x0 err 0x0 dio1 0 freq 865900000
   Timeout! #1 status 0x3a irq 0x0 err 0x40 dio1 0 freq 864850000
   ```

   - The chip was not reset: the packet type is still LoRa, and OCP, RX gain and TX clamp keep the values from `Begin()`.
   - The PLL locks in FS on the failing frequency (`status 0x42`, no error), before and after `Calibrate`. Only SetTx fails, and it keeps failing after the full calibration.
   - Register 0x0889 (errata 15.1, TX modulation) changed from 0x04 at boot to 0x08. Bit 2 cleared is right for BW500. Bit 3 is undocumented, and where it comes from is still being checked.

10. Probe v2: at the first failed SetTx, the TX retried SetTx on the same frequency (865.9 MHz) five times, changing one thing each time. Log: [`logs/T12_probe2.log`](logs/T12_probe2.log).

    | Step | Change before SetTx | Result |
    |---|---|---|
    | 0 | none (as in `TXnb`, RF switch for all radios) | `status 0x3a err 0x40` |
    | 1 | RF switch for radio 1 | same |
    | 2 | register 0x0889 bit 3 cleared | same |
    | 3 | DIO2 RF switch command sent again | same |
    | 4 | PA config, OCP 140 mA and TX params sent again | same |

    - The TX state at the failure looks right: radio 3 (all), RXEN low, power 10 dBm, fallback FS.
    - So the RF switch, the extra bit in 0x0889 and the PA settings are not the cause. SetTx fails on a frequency where FS locks (step 9), and a full `Calibrate` does not help.

11. **Baseline with the formerly working build** (`-DDEBUG_LOG -DDEBUG_TX_FREERUN`, current source with the probe). It now fails too. 4.4 s after boot, at its first normal packets and with no bind, it logged `Timeout! #0 status 0x3a err 0x40` at 864.850 MHz, and all five probe steps failed. After a bind it reached `Timeout! #1000`. The same kind of build linked perfectly in T13 at 16:58. Logs: [`logs/T12_base_a.log`](logs/T12_base_a.log), [`logs/T12_base_b.log`](logs/T12_base_b.log).

    So the failure does not depend on `DEBUG_TX_FREERUN`. Something persistent changed between the end of T13 (17:02) and the first release test (17:11), and every TX build fails since then. Candidates:

    - a setting stored in the TX's flash
    - the hardware, for example the antenna
    - SX1262 state that survives the reset

    Items 2 to 10 are still valid observations, but the conclusion that the cause lies in the FREERUN code paths is withdrawn.

12. Stored TX settings, read over USB with `--params`: Packet Rate 50Hz, Telem Ratio Std (1:16), Switch Mode Wide, Link Mode Normal, Model Match Off, Max Power 10mW, Dynamic Off. All normal. The TX's USB backlog also held `tx ok: TX radio 3, power 10` and `tx ok: … TX modulation 0x8`. That line prints after every 500 real TX_DONE interrupts, so some transmissions do complete, yet the RX 1 m away received none of them. Register 0x0889 reads 0x08 during good transmissions too, so it is not the cause.

    Current suspicion: the TX board's hardware, for example a loose antenna connector. A PA driving into a bad load can pull the oscillator enough to break the PLL lock in TX but not in FS, and anything it does send is weak. Check: look at the antenna, power-cycle the board, and swap the TX and RX roles of the two boards.

13. Both boards power-cycled by hand: no change. The TX's backlog reached `Timeout! #10000` (`status 0x3a err 0x40`), with a few completed transmissions in between. Log: [`logs/T12_after_pwr.log`](logs/T12_after_pwr.log).

14. **Roles swapped**: board B (…47:B0, until then the RX) flashed as TX (`-DDEBUG_LOG -DDEBUG_TX_FREERUN`, Max Power 10mW set right after boot), board A (…47:A0) flashed as RX. Board B fails the same way: `Timeout! #1`, `#2`, `#1000` with `status 0x3a err 0x40`, and a few `tx ok`. Board A as RX received sync packets (`tentative conn` twice) but could not hold the link. Log: [`logs/T12_swap.log`](logs/T12_swap.log).

    So the fault is not in board A's hardware, and board A's radio receives. The release build failed at 17:11, before any source change made during this investigation. So the change around 17:05 must be outside the tracked source. Candidates: the build environment (the release build was the first full rebuild after clearing `PLATFORMIO_BUILD_FLAGS`), the fetched hardware layouts, or the ignored define files. Being checked.

15. **Back to the exact T13 source.** Every change made during items 3 to 14 was removed: the 1 µs wait, the probes, the boot register readout, the `tx ok` print and the watchdog flag. `git diff` against HEAD then shows only the three driver files as they were in T13. Both boards were flashed with the T13 flags (`-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RCVR_LINKSTATS`), board A as TX and board B as RX. **It links.** Log: [`logs/T12_revert.log`](logs/T12_revert.log).

    - The RX logged `got conn` 2.2 s after it booted.
    - The uplink LQ was 100 in every link-statistics frame. The downlink LQ rose from 7 to 100 in 30 s, which is the normal fill of the LQ window after a connect, as in T8.
    - The RX printed 1753 RC packets on all 13 channels (0.95 to 1.04 of the mean), with one gap of 15 ids at the connect.
    - The TX logged no `Timeout!`. The RX logged one, `Timeout! #0 status 0x3a err 0x40` at 867.475 MHz, on its first telemetry packet right after `got conn`, and none after it.

    What this changes:

    - The permanent failure in items 4 to 14 came from a change made during the investigation, not from the hardware or the build environment. The hardware conclusions of items 12 to 14 are withdrawn.
    - Items 3 to 14 all ran with the 1 µs wait in `WaitOnBusy()`, added in item 3 at 17:19. The probes, the boot readout and the `tx ok` print came later, and they run only at boot or after the first failure. So the 1 µs wait is the prime suspect. It is being tested on its own.
    - Items 1 and 2, the release build at 17:11 to 17:15, ran without the 1 µs wait and still did not link. A release build prints no log, so why is still unknown. Both items will be repeated.
    - A single SetTx failure with `status 0x3a err 0x40` is not new. Earlier logs show it without a lasting failure: `diag2.log`, `exp_25Hz.log`, `T4_100Hz.log` (before the hop deferral was added), the RX in `T12_swap.log`, and the RX in this run. What is new in items 4 to 14 is that it never stops.

16. **The 1 µs wait alone.** The TX (board A) was flashed with the T13 source plus only the 1 µs wait in `WaitOnBusy()`, with the same flags. The RX (board B) kept the build from item 15. **It fails.** Log: [`logs/T12_b1.log`](logs/T12_b1.log).

    - The RX logged `got conn` at 18:43:09.402 and printed 6 RC packets.
    - 200 ms later the TX logged `Timeout! #0 status 0x3a err 0x40` at 864.850 MHz, then `#1` and `#2` (the print throttle).
    - After that the RX received 8 RC packets in 30 s and lost the link at 18:43:20.
    - The TX heard the downlink at −54 dBm with SNR 0, against −31 dBm and SNR 13 in item 15.

    So the 1 µs wait turned the occasional SetTx failure into a permanent one in items 3 to 14.

    The wait changes behaviour only after the shortest commands:

    - `SPIEx::write()` returns while the frame is still being clocked out, so without the wait `WaitOnBusy()` samples BUSY during that frame and finds it low.
    - With the wait, it samples after a 1- or 2-byte frame has ended, sees BUSY high, and waits.
    - After longer frames it still samples during the frame.

    The two short commands in the link loop are:

    - SetFs, sent when leaving RX before a TX, followed by WriteBuffer.
    - SetStandby(XOSC), sent at each hop, followed by SetRfFrequency.

    Which of the two matters is tested next.

17. **Which short command matters.** Two more TX builds, each with a 3 µs wait after one command only (build flag `SX126X_BISECT`). The RX was unchanged.

    | Variant | Wait after | Next command | Result |
    |---|---|---|---|
    | 2 | SetFs (leaving RX before a TX) | WriteBuffer | Pass: up LQ 100, down LQ average 94 (84 to 100), no TX `Timeout!`. Log: [`logs/T12_bisect2.log`](logs/T12_bisect2.log) |
    | 3 | SetStandby(XOSC) (at each hop) | SetRfFrequency | Fail: `Timeout! #0` to `#2` (`status 0x3a err 0x40`, 864.850 MHz) 200 ms after `got conn`, then no link. Log: [`logs/T12_bisect3.log`](logs/T12_bisect3.log) |

    So the failure appears when SetRfFrequency is sent only after SetStandby(XOSC) has finished. In the build that works, SetRfFrequency goes out while the chip is still busy with SetStandby(XOSC). The datasheet says the chip ignores a command sent while BUSY is high. If that happens here, the working build never changes frequency: TX and RX both stay on the frequency that `Config()` set, and the link works without hopping.

    The channel column of the T13 statistics is the RX's hop-table entry (`FHSSsequence[FHSSptr]` in `rx_main.cpp`), not a measurement, so T13 cannot show this. T14 with the RTL-SDR checks it on air.

18. **Release build again (R1): interrupted.** The TX was flashed with the release build on the T13 source at 19:00, and its settings were read. Then the tool call waited because it lacked `--exit`, and the PC slept from 19:02 to 22:39. R1 will be repeated after T10 and T14. Log: [`logs/T12_r1_params.console.txt`](logs/T12_r1_params.console.txt).

19. **Cause found in T10: the SPI/BUSY race in the HAL (2026-09-16).** Session `carrier-frequency-check` tested with a BUSY wait after every command (`DebugLockRx`, `DebugTxMode`).
    - With those waits, SetRx locked on all 13 EU868 channels (868.000 MHz included) on both boards and with every start sequence. CW, infinite preamble and packets all radiated.
    - The same steps through the normal HAL failed with `status 0x3a err 0x40` (PLL_LOCK) or gave a silent carrier. See [T10](#t10-cw-frequency) and [root cause 4](#root-cause-4-commands-sent-while-busy-is-still-high).

    The mechanism: `SPIEx::write()` returns while the frame is still being clocked out. `WaitOnBusy()` then samples BUSY during that frame and finds it low. The next command starts while the chip is still busy with the previous one, and that command is lost or garbled. The TCXO, the PLL and the RF switch are not the cause.

    This explains the T12 results:

    - Which command lands inside BUSY depends on the timing between commands, so small timing changes move the failure around. That covers the occasional single PLL_LOCK in the logs since 15:50, and the permanent one when the 1 µs wait (items 3 to 16) or the bisect variant 3 (item 17) shifted the timing. Variant 3 made SetRfFrequency wait for SetStandby(XOSC), but the next command (SetTx or SetRx) then followed the 5-byte SetRfFrequency frame while BUSY was high.
    - The release build (items 1 and 2) has different timing from the debug build: no `noteCommand()` bookkeeping and a different BUSY timeout path. It most likely hit the same race. This will be confirmed after the fix.
    - The explanation in item 17, that the working build never changes frequency, is not needed for these results. T14 checks the hopping on air.

    The fix is not in the source yet. `WaitOnBusy()` and `WaitOnBusyLong()` will first wait until the SPI transfer has ended, then about 1 µs (BUSY rises at most 600 ns after NSS, datasheet §8.3.1), then poll BUSY as before. The `SX126X_BISECT` code will be removed. After the fix, T12 is repeated (release build on both boards, then a bind), together with T3 and T4 at 50Hz and 200Hz, and T14.

### T13: Per-packet link statistics

Both boards were flashed with `-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RCVR_LINKSTATS` (UID `114,26,129,203,82,62` on both). The run was 2 minutes at 50Hz, Telem Ratio Std (1:16) and 10 mW. Log: [`logs/T13.log`](logs/T13.log). The summary comes from the new [`linkstats_summary.py`](linkstats_summary.py).

| Check | Result |
|---|---|
| RC packets received | 5803, ids 4030 to 9833 |
| Missing ids | 1 (after id 4088), in the RX's backlog from before the monitor opened; none in the recording itself |
| RSSI | avg -32.7 dBm (-40 to -28) |
| LQ | 100 in every line |
| SNR | avg 13.1 dB (10.5 to 15.25) |
| Power | code 1 (10 mW) in every line |
| Timer offset | avg 2.9 (-10 to 26) |
| `lost conn`, BUSY, `Timeout!`, `TLM crc error`, reboots | 0 after the `set:` lines |

| Channel | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Packets | 451 | 445 | 444 | 450 | 451 | 443 | 422 | 450 | 450 | 446 | 450 | 453 | 448 |
| Of the mean | 1.01 | 1.00 | 0.99 | 1.01 | 1.01 | 0.99 | 0.95 | 1.01 | 1.01 | 1.00 | 1.01 | 1.01 | 1.00 |

- All 13 EU868 channels are used, each within 1 % of the mean except channel 6. Channel 6 is the sync channel (`sync=6` in the boot log). A sync packet there sometimes takes the place of an RC packet, and sync packets carry no packet id.
- The missing id 4089 falls at a hop (channel 3 to 10), in lines the RX printed before the monitor opened the port. They all carry the same time stamp, 17:00:21.866. The LQ stays 100 in the lines after it, so the packet was not lost on air.
- The TX link statistics show the downlink recovering from the RX re-flash. The TX was flashed first, and the RX was off the air while it was flashed. The TX's backlog shows up LQ 0 and down LQ falling to 58 during that time. In the recording the down LQ climbs back, reaches 100 at about 17:00:28.7, 6 s in, and stays at 100 to the end. That is why the TX summary counts 7 frames below 100 (min 95).

Result: pass.

### T14: Hopping channels

The RTL-SDR V4 was connected at 22:39, with the tools from the rtl-sdr-blog Windows release (`rtl_test -t`: `RTL-SDR Blog V4 Detected`, R828D tuner). Its antenna was 1 to 2 m from the boards.

Baseline, 19:02, nothing transmitting, gain 19.7 dB. Log: [`logs/T14_r1_scan.csv`](logs/T14_r1_scan.csv), the rows up to 19:02:47.

- The floor was about −40 dB (rtl_power levels are relative, not dBm).
- The SDR has its own spur at 864.004 MHz, 13 dB above the floor. That is 30 × its 28.8 MHz reference.
- An outside signal at 869.49 to 869.57 MHz, 9 dB above the floor, is probably a LoRa or Meshtastic device on 869.525 MHz. It overlaps channel 12.

First attempt, 22:48 to 22:50: the standard debug build (T13 source), SDR gain 19.7 dB, 60 s at 50Hz and 60 s at 200Hz. Logs: [`logs/T14_50Hz.csv`](logs/T14_50Hz.csv), [`logs/T14_50Hz_link.log`](logs/T14_50Hz_link.log), [`logs/T14_200Hz.csv`](logs/T14_200Hz.csv), [`logs/T14_200Hz_link.log`](logs/T14_200Hz_link.log).

- The link ran normally. At 50Hz: up LQ 100, down LQ average 98.7, no `Timeout!`, 2899 RC packets at the RX. At 200Hz: 12280 RC packets at the RX, and a downlink LQ of 15.8 on average (0 to 75), where T4 had 0.
- The SDR was overloaded. The empty spectrum outside the band (862 to 863 and 870 to 871 MHz) rose about 33 dB above the baseline, and the channels were only 3 to 6 dB above it. A 10 mW transmitter 1 to 2 m away is too strong for gain 20. So this recording cannot tell a hopping link from one that stays on one channel.

Repeat at gain 0, 2026-09-16, 01:01 to 01:04 (session `carrier-frequency-check`):

- Firmware: the normal link build on both boards (A = TX, B = RX), with `-DDEBUG_LOG -DDEBUG_TX_FREERUN -DDEBUG_RCVR_LINKSTATS`, at 50Hz and 10 mW.
- Recording: `rtl_power -f 862487500:870887500:10000 -c 0.125 -g 0 -i 2`, first a 20 s baseline with the link off, then 120 s with the link on.
- Logs: [`logs/T14_4d_base.csv`](logs/T14_4d_base.csv), [`logs/T14_4d_hop.csv`](logs/T14_4d_hop.csv), [`logs/T14_4d_link.log`](logs/T14_4d_link.log).

| Check | Result |
|---|---|
| Link (TX link statistics, 471 frames) | Up LQ 100 (min 100), down LQ average 99.4 (min 94); RSSI -17/-18 dBm; SNR 12 |
| RX hop table (`linkstats_summary.py`) | 5552 packets, no id missing; all 13 channels, 0.95 to 1.01 of the mean (channel 6, the sync channel, 0.95) |
| SDR, energy per channel | All 13 channels carry signal in 42 to 58 of 60 rows; each has 4.5 to 8.9 % of the energy (an even split would be 7.7 %) |
| SDR, strongest channel per row | 9 different channels are the strongest in two rows or more |
| SDR, row-to-row swing | 10.7 dB (a link parked on one frequency gives about 1 dB) |
| SDR, outside the band | 862.750, 870.100 and 870.625 MHz: median +30.6 dB, up to +37.7 dB against the baseline |

The script gives no verdict. The band outside 863 to 870 MHz is 31 dB above the baseline, so the SDR is overloaded even at gain 0. The boards were close (RSSI -17 dBm), and the signals outside the band are overload images. Every other sign points to a link that hops over all 13 channels, and none to a link stuck on one frequency.

Result: open. Repeat with the SDR, or just its antenna, further from the boards, until the slots outside the band stay within 6 dB of the baseline. After the test both boards were parked in the ROM bootloader, and an 8 s check showed no carrier.

## Diagnosis (in progress)

Both radios start correctly: SPI, BUSY, reset and the TCXO work, and the device error register is 0. But no packet passes in either direction:

- The TX never gets TX_DONE.
- The RX never gets RX_DONE.

Two explanations fit both symptoms:

1. The DIO1 interrupt does not reach the ESP32-S3 on either board.
2. The TX radio never completes a transmission.

A diagnostic build prints the chip status, the IRQ flags, the device errors and the DIO1 pin level at each TX timeout (TX) and every 2 s without a link (RX), to tell them apart. Log: [`logs/diag1.log`](logs/diag1.log).

```text
[TX] Timeout! #0 status 0x62 irq 0x0 err 0x40 dio1 0
[RX] diag irq 0x0 dio1 0
```

- `status 0x62`: the chip is still in TX mode about 5 ms after SetTx, although the packet takes 4.64 ms on air.
- `irq 0x0`, `dio1 0`: TX_DONE is never raised, so DIO1 has nothing to signal. The DIO1 wiring is not the cause.
- `err 0x40`: the device error register reports **PLL_LOCK**. It was 0 after boot, so the error appears when the chip first tries to transmit.

The synthesizer does not lock, so the chip can neither transmit nor receive. Possible causes:

1. A wrong frequency value.
2. The DC-DC regulator without its inductor on the Wio-SX1262 module. The boot calibration ran on the LDO, before `SetRegulatorMode(DCDC)`, which would explain why it passed.
3. The TCXO not running properly at 1.8 V.

The next diagnostic build runs an FS-mode PLL lock test at 866.425 MHz during boot, once with the DC-DC and once with the LDO, and prints the frequency with every diagnostic line. Log: [`logs/diag2.log`](logs/diag2.log).

```text
[TX] FS test DCDC, 866425000 Hz, reg 0x3626cccc: status 0x42 err 0x0
[TX] FS test LDO, 866425000 Hz, reg 0x3626cccc: status 0x42 err 0x0
[TX] Timeout! #0 status 0x62 irq 0x0 err 0x0 dio1 0 freq 866425000
[TX] Timeout! #1 status 0x62 irq 0x0 err 0x0 dio1 0 freq 867475000
[RX] diag #0 status 0x52 irq 0x0 err 0x0 dio1 0 freq 866425000
[RX] diag #6 status 0x54 irq 0x0 err 0x0 dio1 0 freq 866425000
```

- The PLL locks in FS on both boards, with the DC-DC and with the LDO. The frequency register is right, and the DC-DC and TCXO are not the cause.
- The TX is still in TX mode at the next slot, with no error, and it hops correctly. Each transmission takes longer than the 5 ms slot, so the next TXnb aborts it before TX_DONE. The PLL_LOCK error seen earlier looks like a side effect of those aborts.
- The RX sits in RX mode (`0x52`). Once it reported "data available" (`0x54`), so it sees fragments of the aborted packets.
- The PA ramp is 40 µs (`SX126X_RADIO_RAMP_40_US = 0x02`), so the ramp is not what stretches the transmission.

### Experiment: TX at 25 Hz

`elrs_usbmon.py --set "Packet Rate=25Hz"` on the TX. Log: [`logs/exp_25Hz.log`](logs/exp_25Hz.log).

- The CRSF parameter path over USB works: `device 'DIY SX1262 TX', 20 parameters`, `Set parameter [Packet Rate]=1`, `set rate 4`, `set: Packet Rate = 25Hz(-123dBm)`.
- At 25 Hz (40 ms slot, 29.95 ms on air) the TX prints no `Timeout!`. TX_DONE arrives, so DIO1 and the ISR work. At 200 Hz each transmission overruns its 5 ms slot.
- There is still no link. The RX printed 665 `SX126x BUSY timeout` lines in 40 s, in bursts from boot onwards. Its diagnostic then showed `status 0x32` (STDBY_XOSC), not RX mode.

When `WaitOnBusy()` gives up after 1 ms, the HAL sends the next command anyway, and the SX126x ignores commands while BUSY is high. So commands get lost; here the SetRx did not take effect. The chip should only hold BUSY high this long while the TCXO starts, which happens after STDBY_RC or SLEEP, or during a calibration. The next diagnostic build waits up to 30 ms and reports how long BUSY stayed high and which command came just before.

### Root cause 1: commands lost after the image calibration

Logs: [`logs/diag3.log`](logs/diag3.log) (25 Hz), [`logs/diag3_200Hz.log`](logs/diag3_200Hz.log) (200 Hz).

```text
[RX] SX126x DIO2 RF switch on
[RX] SX126x BUSY 6604 us after cmd 0x98, radio 3
[TX] SX126x BUSY 6605 us after cmd 0x98, radio 3
```

- `0x98` is CalibrateImage in `Begin()`. From STDBY_RC it starts the TCXO (5 ms) and then calibrates, so BUSY stays high for about 6.6 ms. No other command kept BUSY high for more than 1 ms.
- `Begin()` calls `WaitOnBusyLong()` after CalibrateImage, but that polls BUSY immediately after the SPI transfer. BUSY rises shortly after NSS goes high, so the first check can still read low and the function returns at once.
- The next commands then hit the 1 ms `WaitOnBusy()` timeout and are sent while the chip is busy, so the chip ignores them. The first is SetRxTxFallbackMode. The chip then keeps its default fallback, STDBY_RC, which turns the TCXO off after every TX and RX. Every packet then starts with a 5 ms TCXO restart. That makes the 200 Hz transmissions overrun their slot, and it causes more BUSY timeouts and more lost commands, such as the RX's SetRx.
- With the diagnostic wait (commands no longer lost), the TX runs at 200 Hz with **0** `Timeout!` in 40 s, and the RX receives the TX: `New TLMrate 1:64`, `tentative conn`.

Fix: `WaitOnBusyLong()` now waits 10 µs before it first checks BUSY.

### Problem 2: the RX cannot hold the connection

After `tentative conn` the RX hops (its RX mode is `0x52`, and the frequency changes), but no further sync packet arrives. After `RxLockTimeoutMs` (2.5 s at 200 Hz, 4 s at 25 Hz) it logs `Bad sync, aborting` and `lost conn`, and starts again. The TX never receives telemetry (`lq 0` down). Next: the RX scoreboard (`DEBUG_RX_SCOREBOARD`) shows for every expected packet whether it was missed, failed the CRC, or was good.

### Root cause 2: packets lost after the first frequency hop

RX scoreboard at 200 Hz, with the `WaitOnBusyLong()` fix. Log: [`logs/score200.log`](logs/score200.log).

```text
[RX] sNew TLMrate 1:64
[RX] tentative conn
[RX] RR____T_______________________________________________________________T_______...
```

Totals over 30 s: 10 `R`, 57 `T`, 3533 `_`, 0 `.`, and 0 `SX126x BUSY` lines, so the BUSY fix works.

- The RX receives the sync packet (`s`) and the next 2 RC packets (`RR`) on the sync channel.
- After its first hop it receives nothing at all: no `.` (CRC error), only `_` (no packet).
- Its timer keeps running correctly: it sends telemetry (`T`) every 64 slots.

The radios can transmit and receive on one channel, but after a hop they are not on the same frequency. The driver changed the frequency in FS mode: after TX, where FS is the fallback mode, and in RX via SetFs. The SX1262 appears to keep its PLL locked to the old frequency there.

Fix: `SetFrequencyReg()` goes to STDBY_XOSC first. The next SetTx or SetRx then locks the PLL to the new frequency.

Result at 200 Hz, about 25 s. Log: [`logs/hopfix200.log`](logs/hopfix200.log).

| RX scoreboard | Count |
|---|---|
| `R` RC packets received | 4890 |
| `s` sync packets | 31 |
| `T` telemetry sent (every 64 slots) | 79 |
| `_` missed | 0 |
| `.` CRC error | 0 |

The RX logs `got conn` and `Timer locked`. There are no BUSY lines and no TX `Timeout!`. **The uplink works.**

### Problem 3: the TX does not receive telemetry

The RX sends its telemetry on schedule and gets TX_DONE, but the TX link statistics stay at `lq 0` down, and the TX never logs `got downlink conn`. The TX log has no `TLM crc error` either: it receives no telemetry at all, not even damaged packets.

At 25 Hz the full link works. Log: [`logs/hopfix25.log`](logs/hopfix25.log).

```text
[TX] got downlink conn
[RX] got conn ... Timer locked
[TX] LINK up: rssi -18 dBm lq 100 snr 11 | down: rssi -20 dBm lq 100 snr 11 | rate 25Hz power 10 mW
```

- The uplink LQ is 100 for the whole run.
- The downlink LQ rises 61, 77, 92, 100 as its 100-packet window fills (telemetry 1:8 is about 3 packets per second).
- Uplink and downlink RSSI differ by 2 dB, so the RF paths of both boards work in both directions.

So the problem at 200 Hz is timing. The TX packet takes 4.64 ms of the 5 ms slot, and the telemetry packet takes another 4.64 ms. Both turnarounds (TX to RX on the TX, and RX to TX on the RX) must fit into the 0.72 ms left in the two slots. The SX126x's SF6 packets are about 260 µs longer than the SX127x's, because of 2 extra preamble symbols, and the frequency hop now goes through STDBY_XOSC. T4 checks which rates keep the downlink.

The budget at 200 Hz, measured from the end of the TX packet:

| Step | Time |
|---|---|
| RX_DONE on the RX (demodulation latency, about 1 symbol) | +0.1 ms (estimate) |
| RX tock: `PACKET_TO_TOCK_SLACK` (`rx_main.cpp:73`) | +0.2 ms |
| RX telemetry start: RX to FS, write buffer, SetTx, PA ramp | +0.1 ms (estimate) |
| Telemetry on air | +4.64 ms |
| RX_DONE on the TX | +0.1 ms (estimate) |
| **Total** | **about 5.14 ms** |
| TX's next SetTx: 10 ms after the previous SetTx, which is 4.64 ms before the end | at +5.36 ms |

That leaves about 0.2 ms. If RX_DONE on the TX comes later, `TXnb()` switches the TX from RX to TX before the telemetry has been received, and the TX gets nothing, not even `TLM crc error`. This matches what we see. At SF7 and above, the slots have 1.2 to 10 ms of slack.

### Root cause 4: commands sent while BUSY is still high

Found on 2026-09-16 in T10 by session `carrier-frequency-check`; see [T10](#t10-cw-frequency) and [T12](#t12-release-build) item 19. It is not fixed in the source yet.

- `SPIEx::write()` starts the SPI transfer and returns straight away, so the processor can go on while the frame is clocked out.
- The next command's `WaitOnBusy()` then reads BUSY while that frame is still going out, and finds it low. The next command starts as soon as the SPI is free.
- By then BUSY has risen (at most 600 ns after NSS goes high), so the chip loses or garbles that command.
- The 10 µs wait added for root cause 1 covers only `WaitOnBusyLong()`. Everywhere else the outcome depends on the time between two commands and on the length of the first frame.

It explains three results:

- the PLL_LOCK errors (`status 0x3a err 0x40`) after a frequency change, from the first diagnostic above to T12;
- a CW that the chip reports as TX mode but that does not radiate (T10);
- why small timing changes (a 1 µs wait, the debug bookkeeping, the release build) move or hide the failures (T12).

With a BUSY wait after every command, SetRx locks on all 13 channels and every TX mode radiates.

The fix: `WaitOnBusy()` and `WaitOnBusyLong()` first wait until the SPI transfer has ended, then about 1 µs, then poll BUSY. After the fix, T3, T4, T12 and T14 are repeated.

## Tool fix during the run

`elrs_usbmon.py --reset` did not reset the board at first: Windows `usbser.sys` only sends a new RTS state with a DTR update. It now writes DTR after each RTS change, as esptool does, and resets both boards (`rst:0x15 (USB_UART_CHIP_RESET)`).

New option `--reset-count N`: with `--reset`, it resets the board N times, 1.2 s apart. Three resets put a bound RX in bind mode through its power-on counter (T8).

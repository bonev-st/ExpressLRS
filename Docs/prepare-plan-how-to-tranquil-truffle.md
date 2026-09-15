# Plan: Port `src/lib/SX126xDriver` to a real SX1262 LoRa-900 driver (TX + RX, Wio-SX1262)

## Update after the bench tests (2026-09-16)

These corrections to the plan below come from the bench tests. Details: `Docs/hwtest/test-report.md` (T10, T12) and `Docs/Wio-SX1262-hardware-test-plan.md`.

- **HAL (fixed on 2026-09-16 in `SX126x_hal.cpp` and `SPIEx`, retested in T1-T14):** `SPIEx::write()` is asynchronous. `WaitOnBusy()` can therefore sample BUSY before the previous frame has ended and send the next command while the chip is busy, and the SX1262 then drops or garbles it. This made the CW test silent (T10) and caused the PLL_LOCK failures of SetTx and SetRx (T12). The fix:
  1. `WaitOnBusy()` first waits for the SPI transfer to finish.
  2. It then allows about 1 µs for BUSY to rise.
  3. Only then does it poll BUSY.

  `WaitOnBusyLong()` needs the same. With every command waiting like this, SetRx locks on all 13 EU868 channels, and the CW, the preamble and packets all radiate.
- **Pins:** the Meshtastic `seeed_xiao_s3` pins under "Wio-SX1262 on XIAO ESP32-S3" belong to the B2B-connector kit. The user's pin-header board uses DIO1 GPIO2, RST GPIO3, BUSY GPIO4, NSS GPIO5 and RF_SW GPIO6 (`src/hardware/TX/DIY XIAO ESP32S3 Wio-SX1262.json`).
- **Git:** `src/` is not a nested git repository. Only `src/hardware`, the targets fork, is.

## Execution split (decided by the user)
- **Part 1, on this PC (the only thing this session does after approval):**
  1. Save this plan as `SX126X_DRIVER_PLAN.md` at the **outer repo root** (`…/ExpressLRS/ExpressLRS/`, next to `README.md`), not in `src/`: `src/` contains a nested git repo (`src/.git`).
  2. Commit only that file on branch `Wio-SX1262`, with the required co-author trailer. Push to `origin` (`github.com/bonev-st/ExpressLRS`) and confirm the remote has the commit.
- **Part 2, on the other computer (a new Claude Code session implements it):**
  1. `git clone -b Wio-SX1262 https://github.com/bonev-st/ExpressLRS.git` (**must** use `-b`: GitHub `main` is an unrelated "first commit" snapshot of `src/`).
  2. Install VS Code, the PlatformIO IDE extension and Node.js.
  3. Open Claude Code at the repo root and say: "Implement `SX126X_DRIVER_PLAN.md`, Part 2".
  4. Connect both boards for the hardware tests.

All paths below are relative to the repo root. PlatformIO and npm commands run in `src/` or `src/html/`.

---

## Context
`src/lib/SX126xDriver/` (commit `1b119454`) is the SX1280 driver with `SX1280`→`SX126x` renamed; the real diff is two debug strings. It sends the **SX1280 protocol** to an SX1262:
- Its `ClearIrqStatus` opcode (0x97) is SX126x `SetDio3AsTcxoCtrl`, its `GetIrqStatus` (0x15) is `GetRssiInst`, and its `GetPacketStatus` (0x1D) is `ReadRegister`.
- Frequency, modulation, packet, IRQ and status encodings are SX1280's, and it writes SX1280-only registers.
- The build fails: header type names, FLRC leftovers, the SX1280 10-argument `Config`, and missing enums in `src/src/common.cpp`.

**Goal:** a working SX1262 LoRa driver for ExpressLRS on 900 MHz. Test setup: **two** Seeed XIAO ESP32-S3 + Wio-SX1262 kits (one TX module, one RX), so both TX and RX paths must work. Regulatory domain EU868; FCC915 must not break. The driver exposes the SX127x-style API and the SX1280/LR1121 "frequency change keeps receiving" behaviour, so `tx_main.cpp`/`rx_main.cpp` only need `Begin` branches. It keeps ELRS 900 LoRa over-the-air settings, so SF7–SF9 rates stay compatible with SX127x/LR1121 receivers.

## Key facts (SX1261/2 datasheet, RadioLib `SX126x_commands.h`/`SX126x_registers.h`, Meshtastic `seeed_xiao_s3`, mLRS)

| Area | Current (SX1280) | SX1262 |
|---|---|---|
| Register/buffer opcodes | 0x18/0x19/0x1A/0x1B | 0x0D/0x1D/0x0E/0x1E |
| IRQ opcodes (SetDioIrq/GetIrq/ClrIrq) | 0x8D/0x15/0x97 | 0x08/0x12/0x02 |
| RX buffer status / packet status / RSSI inst | 0x17/0x1D/0x1F | 0x13/0x14/0x15 |
| AutoFS | 0x9E | SetRxTxFallbackMode 0x93 (0x40 = FS, 0x20 = STDBY_RC) |
| New commands | – | SetPaConfig 0x95, Dio2AsRfSwitch 0x9D (param 0x01), Dio3AsTcxo 0x97, CalibrateImage 0x98, Calibrate 0x89 (0x7F), GetDeviceErrors 0x17, ClearDeviceErrors 0x07 (2 zero bytes) |
| Unchanged opcodes | – | GetStatus 0xC0, Sleep 0x84, Standby 0x80, Fs 0xC1 (**no parameter**), Tx 0x83, Rx 0x82, TxCW 0xD1, Regulator 0x96, PacketType 0x8A, RfFreq 0x86, TxParams 0x8E, ModParams 0x8B, PktParams 0x8C, BufferBase 0x8F |
| Frequency | 52 MHz/2^18, 3 bytes | 32 MHz/2^25 = 0.9537 Hz/step, **4 bytes big-endian** |
| ModulationParams (LoRa) | SF<<4, SX1280 BW | 8 bytes: SF 0x05–0x0C, BW125/250/500 = 0x04/0x05/0x06, CR 4/5…4/8 = 0x01…0x04, LDRO 0x00, 4× 0x00 |
| PacketParams (LoRa) | 7 bytes | 6 bytes: preamble MSB, LSB, header (explicit 0x00 / implicit 0x01), payload length, CRC off 0x00, IQ standard 0x00 |
| IRQ bits | SX1280 layout | TX_DONE 0x1, RX_DONE 0x2, PREAMBLE 0x4, SYNC 0x8, HDR_VALID 0x10, HDR_ERR 0x20, CRC_ERR 0x40, CAD_DONE 0x80, CAD_DET 0x100, TIMEOUT 0x200 (all = 0x03FF) |
| Status byte | mode bits 7:5, byte 0 | byte 1 of the reply: mode bits 6:4 (STBY_RC 0x20, STBY_XOSC 0x30, FS 0x40, RX 0x50, TX 0x60), command bits 3:1. **Don't gate packets on it** |
| RX/TX timeout | base + 16 bit (the current bytes mean a 1.024 s timeout on SX126x) | 24 bit in 15.625 µs: RX continuous 0xFFFFFF, TX none 0x000000 |
| Power | −18…+13 dBm, +18 offset, ramp 0x20 | int8 −9…+22 dBm. Order: SetPaConfig(0x04, 0x07, 0x00, 0x01) → OCP → SetTxParams(dBm, 0x02 = 40 µs) |
| Registers | 0x0891, 0x0925, 0x0153 (SX1280-only: remove) | version 0x0320, IQ 0x0736, sync word 0x0740/41 (reset value 0x14 0x24), freq error 0x076B, BW500 fix 0x0889, RX gain 0x08AC (0x96 boosted), TX clamp 0x08D8, OCP 0x08E7 (2.5 mA/LSB, 0x38 = 140 mA) |
| Errata (§15) | – | BW500: 0x0889 bit2 = 0. TX clamp: 0x08D8 \|= 0x1E. Standard IQ: 0x0736 bit2 = 1. Implicit-header timeout fix (0x0902/0x0944) only with RX timeouts (not used). 0x08D8/0x08AC/OCP are lost on cold sleep, so `Begin` sets them |
| TCXO | – | voltage codes 0..7 = 1.6/1.7/1.8/2.2/2.4/2.7/3.0/3.3 V; delay 24-bit in 15.625 µs. **STDBY_RC switches the TCXO off**; the next XOSC/FS/RX/TX holds BUSY for the whole delay |
| Image calibration | – | `(fmin_MHz−1)/4`, `1+(fmax_MHz+1)/4` (as in LR1121) → 902–928: 0xE1/0xE8; 863–870: 0xD7/0xDA (these cover the band) |
| SPI clock | 17.5 MHz (hal .cpp lines 69 and 84) | ≤16 MHz: use 10 MHz |

**Over-the-air conventions to keep** (`src/lib/SX127xDriver/SX127x.cpp`, `src/lib/LR1121Driver/LR1121.cpp`):
- BW500, implicit header, CRC off, LDRO off.
- **Standard IQ**: SX127x ignores InvertIQ, and LR1121 forces standard below 1 GHz.
- Sync word 0x12, which is SX126x 0x1424 (MSB = `(sw & 0xF0) | 0x04`, LSB = `((sw & 0x0F) << 4) | 0x04`).

**SF6 caveat:** SX126x SF6 ≠ SX127x SF6 (mLRS docs; the LR1121 needed a Semtech register tweak; no public SX126x fix).
- Two SX1262s: all rates work.
- Against SX127x/LR1121 receivers: only 100 Hz (SF7), 50 Hz (SF8) and 25 Hz (SF9).
- Keep the rate table identical in order to SX127X.

**Wio-SX1262 on XIAO ESP32-S3** (Meshtastic `seeed_xiao_s3`):
- NSS 41, DIO1 39, BUSY 40, RST 42, SCK 7, MISO 8, MOSI 9.
- RXEN 38 (no TXEN), DIO2 = RF switch, TCXO 1.8 V on DIO3.
- DC-DC (RadioLib default).
- GPIO 39–42 are the S3's JTAG pins; fine as GPIO unless the JTAG efuse is set.

## Part 2: Implementation
Re-read every file before editing. The driver files in commit `1b119454` are the renamed SX1280 copy.

### A. Driver: `src/lib/SX126xDriver/`
1. **`SX126x_Regs.h`: rewrite from scratch.**
   - Opcode enum **replaced wholesale** (stale SX1280 values collide with real SX126x commands); registers; LoRa codes; IRQ and status masks; fallback modes; PA and ramp constants; TCXO codes; calibration flags.
   - `SX126X_XTAL_FREQ 32000000`, `SX126X_POWER_MIN -9`, `SX126X_POWER_MAX 22`.
   - Consistent names: `SX126x_*_t` typedefs, `SX126X_*` values.
   - Drop FLRC, ranging, tick-size, `FREQ_STEP`, and the SX127x-style `SX126X_BW_500_00_KHZ`/`SX126X_SF_*`/`SX126X_CR_*` aliases.
   - Afterwards, grep the driver for leftover literal opcodes.
2. **`SX126x_hal.h/.cpp`.**
   - Keep the SPI framing (command data at +2, `ReadRegister` +4, `ReadBuffer` +3) with SX126x opcodes.
   - `ReadCommand` returns `OutBuffer[1] & 0x7E` (status).
   - SPI clock 10 MHz at both `setFrequency` calls.
   - Remove `SX1280_BusyState`.
   - Add `WaitOnBusyLong(timeoutMs)` for `Begin` (TCXO start, Calibrate ~3.5 ms, CalibrateImage) and the CW test. The normal 1 ms `WaitOnBusy` stays for the hot path. Log when a wait times out.
3. **`SX126x.h`: SX127x-shaped API.**
   - `bool Begin(uint32_t minFreqHz, uint32_t maxFreqHz)`.
   - `Config(bw, sf, cr, freqHz, preambleLen, bool InvertIQ, payloadLen)` (7 arguments).
   - `SetOutputPower(int8_t dBm)`.
   - Keep `End`, `SetTxIdleMode()` (→ FS), `SetFrequencyReg`, `startCWTest`, `TXnb`/`RXnb`, `GetIrqStatus`/`ClearIrqStatus`, `GetRssiInst`, `GetLastPacketStats`, `CheckForSecondPacket`, `GetFrequencyErrorbool`, and `FrequencyErrorAvailable()` (returns false; both ends have a TCXO, as on LR1121).
   - Keep `RADIO_SNR_SCALE 4`.
   - Remove the FLRC API and `packet_mode`.
4. **`SX126x.cpp`: rewrite** on the existing skeleton (dual radio, Gemini, IRQ dispatch, RX stats). Mark ISR-path helpers `ICACHE_RAM_ATTR`; no `double` or 64-bit division in ISRs.
   - **`Begin(min, max)`, in this order:**
     1. `hal.init`/`reset`, then STDBY_RC.
     2. Detect the chip: reg 0x0740 reads 0x14 0x24 after reset (per radio).
     3. If `hardware_int(HARDWARE_radio_tcxo) != -1`: SetDio3AsTcxoCtrl(code, delay). Use `radio_tcxo_delay` if it isn't -1, else 320 (= 5 ms); it's a raw count in 15.625 µs units.
     4. ClearDeviceErrors → Calibrate(0x7F) → `WaitOnBusyLong`.
     5. DCDC if `OPT_USE_HARDWARE_DCDC`.
     6. SetDio2AsRfSwitchCtrl(1) if `hardware_flag(HARDWARE_radio_dio2_rfsw)`. **Log** whether it's on: without it the Wio has no TX path.
     7. SetPacketType(LoRa), **only here**.
     8. SetPaConfig → OCP 0x38 → TX clamp → RX gain 0x96 → sync word 0x1424.
     9. CalibrateImage(min, max) → `WaitOnBusyLong`.
     10. SetRxTxFallbackMode(FS). Keep the old AutoFS rule: STDBY_XOSC instead for dual-radio TX; never STDBY_RC (TCXO).
     11. SetDioIrqParams(irq = dio1 = TX_DONE | RX_DONE); SetBufferBaseAddress(0x00, 0x80).
     12. Default power 14 dBm, committed. POWERMGNT overrides it; RX telemetry keeps it when a layout has no `power_values`.
     13. Log `GetDeviceErrors` (expect 0).
     14. Finish in **STDBY_XOSC** (long wait for the TCXO).
   - **`Config`:**
     1. **STDBY_XOSC** (never STDBY_RC after `Begin`). No SetPacketType.
     2. ModulationParams → BW500 fix (0x0889).
     3. PacketParams: implicit (explicit under `DEBUG_FREQ_CORRECTION`), CRC off, **IQ standard** (`IQinverted = false`) → IQ fix (0x0736).
     4. `SetFrequencyReg(freq, All, false)` → ClearIrqStatus(0x03FF).
   - **`SetFrequencyReg(freqHz, radio, doRx)`:**
     - Convert with `reg = (uint32_t)(((uint64_t)freqHz * 1125899907ULL) >> 30)`: an inline 32×32→64 multiply, under one step of error up to 960 MHz.
     - Record `wasRx = (currOpmode == RX_CONT)` first. If `wasRx`, send SetFs **via hal directly**, without touching `currOpmode`, so back-to-back per-radio Gemini calls still see RX.
     - Send the 4-byte SetRfFrequency; set `currFreq = freqHz`.
     - If `doRx || wasRx`, send SetRx(0xFFFFFF): a frequency change keeps receiving, like SX1280/LR1121. On TX the chip is already in FS after TX_DONE (fallback), so no extra command.
   - **`SetMode`:**
     - SetSleep(**0x00**) (0x01 would enable RTC wake), STDBY_RC/STDBY_XOSC, SetFs (**no parameter**), SetRx(0xFFFFFF), SetTx(0x000000).
     - `TXnbISR`: `currOpmode = fallBackMode`.
   - **`TXnb`:** keep the timeout catch and the Gemini/diversity logic. **If `currOpmode == RX_CONT`, send SetFs to all radios first** (the RX telemetry slot and the TX after a telemetry RX). Then `RFAMP.TXenable` → WriteBuffer → SetTx.
   - **Power:** clamp to −9…22 and keep it pending; commit after TX with SetTxParams(int8, 0x02).
   - **RX:**
     - Accept packets on the **RX_DONE IRQ alone**. `GetRxBufferAddr` returns the pointer and must not gate on status; log the raw status once at the first RX_DONE for bring-up.
     - `RXnbISR` reads the buffer (CRC off; ELRS checks its own OTA CRC). `CheckForSecondPacket` works the same way.
     - `GetLastPacketStats` reads 3 bytes: RSSI = −RssiPkt/2, SNR = (int8)SnrPkt, with the SX127x negative-SNR correction.
     - `GetRssiInst` returns −raw/2.
   - **`startCWTest(freqHz)`** takes Hz directly (the current code double-converts): SetFrequencyReg → commit power → `RFAMP.TXenable` → SetTxContinuousWave.
   - **`End()`:** SetSleep(0x00); `currFreq` = 915000000.
   - Remove all FLRC code and the 0x0891/0x0925/0x0153 register accesses.

### B. Frequency units: FHSS in Hz for SX126X (land this together with `Begin(min,max)`)
- `src/lib/FHSS/FHSS.h`:
  - Add `RADIO_SX126X` to the LR1121/LR2021 Hz branch (`FREQ_HZ_TO_REG_VAL(f) f`, `FREQ_SPREAD_SCALE 1`). The spreads are exact: EU868 525000, FCC 600000. The 0.95 Hz step would otherwise overflow FCC915.
  - Set the SX126X `FreqCorrectionMax` to `((int32_t)100000)`: a non-zero literal, because it's a divisor under `DEBUG_FREQ_CORRECTION`.
- `src/lib/FHSS/FHSS.cpp:19-20`: remove the unreachable duplicate `#elif defined(RADIO_SX126X)` include.

### C. Call sites
- `src/src/tx_main.cpp:473-474`: **remove** the SX126X `Config` branch (SX1280 extra args).
- `src/src/tx_main.cpp:1423-1432`: add `#elif defined(RADIO_SX126X) init_success = Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq());`. Today `init_success` (declared at about line 1417) is uninitialised and the radio never starts.
- `src/src/rx_main.cpp:1592-1601`: add the same `Begin` branch (`bool init_success = …`). `Config` uses the 7-argument default. No hop or RXnb changes are needed, because the driver keeps RX across frequency changes.
- `src/lib/WIFI/devWIFI.cpp:568-572`: SX126X → `has_low_band = true`, `has_high_band = false`, `reg_domain_low` (used by `state.js` and the CW page).
- `src/lib/WIFI/devWIFI.cpp:1060-1061`: CW test → `Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq())`.

### D. Rate tables: `src/src/common.cpp` SX126X block (~164-184)
- Constants → `SX126X_LORA_BW_500`, `SX126X_LORA_SF6…SF9`, `SX126X_LORA_CR_4_7/4_8`. Keep the SX127X order, intervals, preambles (8 at SF6, as LR1121 uses) and payload sizes.
- `TOA` with the SX126x formula (SF5/6 add 2 preamble symbols; verified to reproduce the SX127x table for SF7+):
  - Rows 0 and 5: 4380 → **4640** µs (360 µs of margin at 200 Hz).
  - Row 1: 6690 → **6944** µs.
  - SF7–SF9 rows are unchanged.

### E. Hardware option and web UI
- New BOOL field `radio_dio2_rfsw`:
  - Add `HARDWARE_radio_dio2_rfsw` after `HARDWARE_radio_tcxo_delay` in `src/include/hardware.h`.
  - Add `{HARDWARE_radio_dio2_rfsw, "radio_dio2_rfsw", BOOL}` to `fields[]` in `src/lib/OPTIONS/hardware.cpp`. Order doesn't matter; each entry carries its position.
  - The driver calls `hardware_flag()` directly, as LR1121 does, so no target-header macros are needed.
- `src/html/src/utils/hardware-schema.js`. The preprocessor has no AND, but nesting works:
  - Add a `/* FEATURE: HAS_SX126X */` block with `radio_tcxo`, `radio_tcxo_delay` (help text "15.625 µs steps, 320 = 5 ms") and `radio_dio2_rfsw`.
  - Nest `radio_rfo_hf` inside `NOT HAS_SX126X`.
  - Regenerate the headers: `npm ci && npm run build:sx126x-tx && npm run build:sx126x-rx` in `src/html`.

### F. Build targets and layouts
- `src/targets/esp32s3-rx.ini`: add, mirroring the LR1121 S3 RX envs:
  ```
  [env:Unified_ESP32S3_SX126X_RX_via_UART]
  extends = env_common_esp32s3rx, radio_SX126X
  build_flags =
  	${env_common_esp32s3rx.build_flags}
  	${radio_SX126X.build_flags}

  [env:Unified_ESP32S3_SX126X_RX_via_BetaflightPassthrough]
  extends = env:Unified_ESP32S3_SX126X_RX_via_UART

  [env:Unified_ESP32S3_SX126X_RX_via_WIFI]
  extends = env:Unified_ESP32S3_SX126X_RX_via_UART
  ```
- `src/hardware/TX/DIY XIAO ESP32S3 Wio-SX1262.json`:
  - Radio: `radio_sck 7`, `radio_miso 8`, `radio_mosi 9`, `radio_nss 41`, `radio_busy 40`, `radio_dio1 39`, `radio_rst 42`.
  - Radio options: `radio_dcdc true`, `radio_tcxo 2`, `radio_tcxo_delay 320`, `radio_dio2_rfsw true`.
  - Power: `power_rxen 38`, `power_min 0`, `power_max 3`, `power_default 0` (explicit; unset means max), `power_control 0`, `power_values [10,14,17,20]`.
  - Other: `led_red 21` + `led_red_invert true`, `button 0`, and CRSF `serial_rx` = `serial_tx` = 1 (XIAO D0, an assumption).
- `src/hardware/RX/DIY XIAO ESP32S3 Wio-SX1262.json`: the same radio and power fields, with `serial_rx 44` and `serial_tx 43` (XIAO D7/D6 to the flight controller).
- `src/hardware/targets.json`, `diy` section:
  - `tx_900` entry: firmware `Unified_ESP32S3_SX126X_TX`, platform `esp32-s3`, upload `["uart","wifi","etx"]`, lua name "DIY SX1262 TX" (≤16 bytes).
  - `rx_900` entry: firmware `Unified_ESP32S3_SX126X_RX`, upload `["uart","wifi","betaflight"]`, lua name "DIY SX1262 RX".
  - `min_version` "4.0.0".
- No Python changes are needed: `build_flags.py`, `copy_html.py` and `UnifiedConfiguration.py` already handle SX126X and RX.

### G. Housekeeping
- `src/platformio.ini` `env:native` `lib_ignore`: add `SX126xDriver`.
- `src/python/binary_configurator.py:50`: remove the unused `FREQ_HZ_TO_REG_VAL_SX126X` (wrong step).

### H. Verification (on the other computer)
1. **Build:**
   - `pio run -e Unified_ESP32S3_SX126X_TX_via_UART -e Unified_ESP32S3_SX126X_RX_via_UART`.
   - Regression builds for the shared edits: `Unified_ESP32_900_TX_via_UART`, `Unified_ESP32_LR1121_TX_via_UART`, `Unified_ESP32S3_2400_TX_via_UART`, `Unified_ESP32S3_900_RX_via_UART`.
   - `pio test -e native`.
2. **Bring-up** (both boards, debug log on):
   - Chip detected; `GetDeviceErrors` is 0 after the TCXO and calibration; no BUSY timeouts logged; the DIO2 switch log says on.
   - Read back 0x0740, 0x0889, 0x08D8, 0x08E7 and 0x08AC.
   - Log the raw status at the first RX_DONE.
3. **CW test (web UI):** SDR or spectrum analyzer shows a carrier at the EU868 center within about ±2 ppm; power steps of about 10/14/17/20 dBm.
4. **Link, SX1262 TX ↔ SX1262 RX:**
   - Bind (50 Hz), then test all 6 rates. Check LQ near 100%, plausible RSSI/SNR, telemetry, and a hopping waterfall.
   - Build FCC915 once.
   - At 200 Hz, measure the hop with a GPIO toggle and `DEBUG_SX126X_OTA_TIMING` against the 360 µs margin.
5. **Commit and push** the implementation to `Wio-SX1262` after the user approves.

## Risks and notes
- The FS-before-frequency-change and FS-before-TX steps are conservative. If the bench shows SetRfFrequency and SetTx work directly from RX, remove them to save time at 200 Hz.
- SF6 won't interoperate with SX127x/LR1121 (chip limitation).
- If TX fails with `radio_dcdc true`, retry with LDO.
- **Existing race:** TX `ExitBindingMode` resumes the timer (`tx_main.cpp` ~747/1023) before `SetRFLinkRate` (~1025), so `Config` SPI traffic can interleave with `TXnb` from the ISR. Long BUSY holds make it likelier. Keep this in mind for glitches around binding.
- The serial, LED and button pins are XIAO assumptions; they can be changed without reflashing through the web Hardware page.

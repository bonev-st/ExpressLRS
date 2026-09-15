#!/usr/bin/env python3
r"""USB CDC monitor for the ExpressLRS XIAO ESP32-S3 + Wio-SX1262 hardware tests.

What it does:
- Opens the ESP32-S3 native USB port without resetting the board (DTR and RTS stay off).
- Prints the text log of one or two boards with a timestamp and a label, and can save it.
- Decodes the binary CRSF frames that the TX sends over USB. LINK_STATISTICS is printed
  about once per second, and every frame is saved to a CSV file.
- Reads and writes TX module parameters (packet rate, telemetry ratio, power, bind, WiFi)
  over the same port, as the ExpressLRS Lua script does on a handset.

Run it with the PlatformIO Python, which already has pyserial:
  $py = 'C:\Users\bonev\.platformio\penv\Scripts\python.exe'
  & $py elrs_usbmon.py --list
  & $py elrs_usbmon.py --port TX=47:A0 --port RX=47:B0 --log run1
  & $py elrs_usbmon.py --port TX=47:A0 --params --exit
  & $py elrs_usbmon.py --port TX=47:A0 --set "Packet Rate=50Hz"
  & $py elrs_usbmon.py --port TX=47:A0 --cmd "Enable WiFi" --exit

A port is COMx or the end of the board's USB serial number (for example 47:A0).
--params, --set and --cmd talk to the first --port, which must be the TX.
Press Ctrl+C to stop.
"""

import argparse
import csv
import datetime
import queue
import re
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit(r"pyserial is missing. Run this script with C:\Users\bonev\.platformio\penv\Scripts\python.exe")

ESP_USB_VID = 0x303A
ESP_USB_PID = 0x1001

# CRSF addresses and frame types (src/include/crsf_protocol.h)
CRSF_SYNC_BYTE = 0xC8
ADDR_BROADCAST = 0x00
ADDR_RADIO_TRANSMITTER = 0xEA
ADDR_CRSF_TRANSMITTER = 0xEE
FRAME_START_BYTES = (0xC8, 0xEA, 0xEC, 0xEE, 0xEF)

FT_LINK_STATISTICS = 0x14
FT_DEVICE_PING = 0x28
FT_DEVICE_INFO = 0x29
FT_PARAMETER_SETTINGS_ENTRY = 0x2B
FT_PARAMETER_READ = 0x2C
FT_PARAMETER_WRITE = 0x2D
FT_ELRS_STATUS = 0x2E

# CRSF parameter types; bit 7 of the type byte marks a hidden parameter
PT_UINT8, PT_INT8, PT_UINT16, PT_INT16, PT_FLOAT = 0, 1, 2, 3, 8
PT_TEXT_SELECTION, PT_STRING, PT_FOLDER, PT_INFO, PT_COMMAND = 9, 10, 11, 12, 13
PT_NAMES = {PT_UINT8: "uint8", PT_INT8: "int8", PT_UINT16: "uint16", PT_INT16: "int16", PT_FLOAT: "float",
            PT_TEXT_SELECTION: "select", PT_STRING: "string", PT_FOLDER: "folder", PT_INFO: "info",
            PT_COMMAND: "command"}

# Command steps, as used by the ExpressLRS Lua script (lcsIdle, lcsClick, ...)
CMD_IDLE, CMD_CLICK, CMD_EXECUTING, CMD_ASK_CONFIRM, CMD_CONFIRMED, CMD_CANCEL, CMD_QUERY = range(7)
CMD_STEP_NAMES = ["idle", "click", "executing", "ask-confirm", "confirmed", "cancel", "query"]

# CRSF uplink_TX_Power enum -> mW (powerToCrsfPower in src/lib/POWERMGNT/POWERMGNT.cpp)
CRSF_POWER_MW = {0: 0, 1: 10, 2: 25, 3: 100, 4: 500, 5: 1000, 6: 2000, 7: 250, 8: 50}

# linkStats.rf_Mode is the rate enum_rate (expresslrs_RFrates_e, src/include/common.h)
RF_MODE_NAMES = {0: "25Hz", 1: "50Hz", 2: "100Hz", 3: "100Hz Full", 4: "150Hz", 5: "200Hz", 6: "200Hz Full",
                 7: "250Hz", 8: "333Hz Full", 9: "500Hz", 10: "D50"}


def crc8_dvb_s2(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def make_frame(frame_type, payload, start=CRSF_SYNC_BYTE):
    body = bytes([frame_type]) + bytes(payload)
    return bytes([start, len(body) + 1]) + body + bytes([crc8_dvb_s2(body)])


def make_ext_frame(frame_type, dest, orig, payload=b""):
    return make_frame(frame_type, bytes([dest, orig]) + bytes(payload))


class Frame:
    def __init__(self, raw):
        self.raw = raw
        self.type = raw[2]
        self.payload = raw[3:-1]
        extended = self.type >= FT_DEVICE_PING and len(self.payload) >= 2
        self.dest = self.payload[0] if extended else None
        self.orig = self.payload[1] if extended else None
        self.data = self.payload[2:] if extended else self.payload


def s8(v):
    return v - 256 if v > 127 else v


def rssi_dbm(v):
    # The TX sends both RSSI fields as int8 dBm (tx_main.cpp negates the over-the-air uplink value).
    # Values 1..127 are read as -dBm, for senders that use the positive CRSF convention
    return s8(v) if v > 127 else -v


def decode_link_stats(p):
    # crsfLinkStatistics_t, 10 bytes
    return {
        "ul_rssi1": rssi_dbm(p[0]), "ul_rssi2": rssi_dbm(p[1]), "ul_lq": p[2], "ul_snr": s8(p[3]),
        "antenna": p[4], "rf_mode": p[5], "rate": RF_MODE_NAMES.get(p[5], f"mode{p[5]}"),
        "ul_power_mw": CRSF_POWER_MW.get(p[6], -1), "ul_power_raw": p[6],
        "dl_rssi": rssi_dbm(p[7]), "dl_lq": p[8], "dl_snr": s8(p[9]),
    }


class Demux:
    """Splits a USB byte stream into text lines and CRC-checked CRSF frames."""

    STALE_S = 0.3
    MAX_LINE = 200

    def __init__(self):
        self.buf = bytearray()
        self.line = bytearray()
        self.last_rx = time.monotonic()
        self.noise = 0

    def feed(self, data, now=None):
        now = time.monotonic() if now is None else now
        if data:
            self.buf += data
            self.last_rx = now
        stale = now - self.last_rx >= self.STALE_S
        events = []
        buf = self.buf
        i = 0
        while i < len(buf):
            b = buf[i]
            if b in FRAME_START_BYTES:
                if i + 1 >= len(buf):
                    if not stale:
                        break  # wait for the length byte
                else:
                    n = buf[i + 1]
                    if 2 <= n <= 62:
                        end = i + n + 2
                        if end > len(buf):
                            if not stale:
                                break  # wait for the rest of the frame
                        elif crc8_dvb_s2(buf[i + 2:end - 1]) == buf[end - 1]:
                            events.append(("frame", Frame(bytes(buf[i:end]))))
                            i = end
                            continue
                # Not a valid frame, so the byte is handled as text or noise below
            if b == 0x0A:
                events.append(("text", self.line.decode("ascii", "replace")))
                self.line.clear()
            elif b == 0x09 or 0x20 <= b < 0x7F:
                self.line.append(b)
                if len(self.line) >= self.MAX_LINE:
                    events.append(("text", self.line.decode("ascii", "replace")))
                    self.line.clear()
            elif b != 0x0D:
                self.noise += 1
            i += 1
        del buf[:i]
        # Output without a newline (the RX scoreboard, a prompt) is shown once the stream pauses
        if self.line and stale:
            events.append(("text", self.line.decode("ascii", "replace")))
            self.line.clear()
        return events


class Output:
    def __init__(self, log_name=None, ls_every=1.0, show_frames=False):
        self.lock = threading.Lock()
        self.ls_every = ls_every
        self.show_frames = show_frames
        self.last_ls_print = {}
        self.stats = {}
        self.link_up_since = {}
        self.frames_seen = {}
        self.log = None
        self.csv = None
        if log_name:
            self.log = open(log_name + ".log", "a", encoding="utf-8")
            new_csv = open(log_name + "_linkstats.csv", "a", newline="", encoding="utf-8")
            self.csv = csv.writer(new_csv)
            self.csv_file = new_csv
            if new_csv.tell() == 0:
                self.csv.writerow(["time", "port", "ul_rssi1", "ul_rssi2", "ul_lq", "ul_snr", "antenna", "rf_mode",
                                   "ul_power_mw", "dl_rssi", "dl_lq", "dl_snr"])

    @staticmethod
    def stamp():
        return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]

    def line(self, label, msg):
        line = f"{self.stamp()} [{label}] {msg}"
        with self.lock:
            print(line, flush=True)
            if self.log:
                self.log.write(line + "\n")
                self.log.flush()

    def link_stats(self, label, ls):
        now = time.monotonic()
        self.frames_seen[label] = self.frames_seen.get(label, 0) + 1
        # The summary starts 5 s after the link is first up in both directions, so the frames sent before
        # the RX connected and while LQ ramps up do not count. Later drops do
        if ls["ul_lq"] > 0 and ls["dl_lq"] > 0:
            self.link_up_since.setdefault(label, now)
        if label in self.link_up_since and now - self.link_up_since[label] >= 5.0:
            self.stats.setdefault(label, []).append(
                (ls["ul_lq"], ls["dl_lq"], ls["ul_rssi1"], ls["dl_rssi"], ls["ul_snr"], ls["dl_snr"]))
        if self.csv:
            with self.lock:
                self.csv.writerow([self.stamp(), label, ls["ul_rssi1"], ls["ul_rssi2"], ls["ul_lq"], ls["ul_snr"],
                                   ls["antenna"], ls["rf_mode"], ls["ul_power_mw"], ls["dl_rssi"], ls["dl_lq"],
                                   ls["dl_snr"]])
                self.csv_file.flush()
        if now - self.last_ls_print.get(label, 0) >= self.ls_every:
            self.last_ls_print[label] = now
            self.line(label, "LINK up: rssi {ul_rssi1} dBm lq {ul_lq} snr {ul_snr} | down: rssi {dl_rssi} dBm "
                             "lq {dl_lq} snr {dl_snr} | rate {rate} power {ul_power_mw} mW".format(**ls))

    def frame(self, label, fr):
        if self.show_frames:
            self.line(label, f"CRSF type 0x{fr.type:02X}: {fr.raw.hex(' ')}")

    def summary(self):
        names = ["up LQ", "down LQ", "up RSSI", "down RSSI", "up SNR", "down SNR"]
        for label, count in self.frames_seen.items():
            rows = self.stats.get(label)
            if not rows:
                self.line(label, f"summary: {count} link-stats frames, but the link was never up for 5 s")
                continue
            parts = [f"{n} avg {sum(c) / len(c):.1f} min {min(c)} max {max(c)}" for n, c in zip(names, zip(*rows))]
            below = sum(1 for r in rows if r[0] < 100 or r[1] < 100)
            self.line(label, f"summary of {len(rows)} link-stats frames from 5 s after the link came up: "
                             + "; ".join(parts) + f"; {below} frames with LQ below 100")


def find_ports():
    return [p for p in list_ports.comports() if p.vid == ESP_USB_VID and p.pid == ESP_USB_PID]


def resolve_port(spec):
    if re.fullmatch(r"(?i)com\d+", spec):
        return spec.upper()
    want = spec.replace(":", "").upper()
    matches = [p.device for p in find_ports() if (p.serial_number or "").replace(":", "").upper().endswith(want)]
    if len(matches) > 1:
        raise SystemExit(f'"{spec}" matches more than one port: {matches}')
    return matches[0] if matches else None


class Port(threading.Thread):
    def __init__(self, label, spec, out):
        super().__init__(daemon=True)
        self.label = label
        self.spec = spec
        self.out = out
        self.ser = None
        self.frames = queue.Queue()
        self.write_lock = threading.Lock()
        self.connected = threading.Event()
        self.demux = Demux()
        self.reset_pending = False
        self.reset_count = 1

    def _open(self):
        dev = resolve_port(self.spec)
        if dev is None:
            return False
        ser = serial.Serial()
        ser.port = dev
        ser.baudrate = 115200  # ignored by USB CDC
        ser.timeout = 0.05
        # Asserting RTS resets an ESP32-S3 on its USB Serial/JTAG port, and DTR selects download mode
        ser.dtr = False
        ser.rts = False
        ser.open()
        if self.reset_pending:
            # A hard reset like esptool's: RTS pulls EN low on the USB Serial/JTAG port. DTR stays low, so the
            # chip boots the application. The port may drop and come back; the reader reconnects
            self.reset_pending = False
            for n in range(self.reset_count):
                if n:
                    # Each boot must get through setup, where the RX counts power-ons, and end within 2 s,
                    # before the RX clears the count again
                    time.sleep(1.2)
                # Windows usbser.sys only sends a new RTS state together with a DTR update, so write DTR
                # again after each RTS change (esptool does the same)
                ser.rts = True
                ser.dtr = False
                time.sleep(0.2)
                ser.rts = False
                ser.dtr = False
            self.out.line(self.label, "reset the board" if self.reset_count == 1
                          else f"reset the board {self.reset_count} times")
        self.ser = ser
        self.connected.set()
        self.out.line(self.label, f"connected to {dev}")
        return True

    def run(self):
        waiting_reported = False
        while True:
            if self.ser is None:
                try:
                    if not self._open():
                        if not waiting_reported:
                            self.out.line(self.label, f"waiting for port {self.spec} ...")
                            waiting_reported = True
                        time.sleep(0.5)
                        continue
                except serial.SerialException as e:
                    self.out.line(self.label, f"cannot open {self.spec}: {e}")
                    time.sleep(1.0)
                    continue
                waiting_reported = False
            try:
                data = self.ser.read(self.ser.in_waiting or 1)
            except (serial.SerialException, OSError) as e:
                self.out.line(self.label, f"port lost ({e}), reconnecting")
                self.connected.clear()
                with self.write_lock:
                    try:
                        self.ser.close()
                    except Exception:
                        pass
                    self.ser = None
                time.sleep(0.5)
                continue
            for kind, value in self.demux.feed(data):
                if kind == "text":
                    self.out.line(self.label, value)
                else:
                    self._frame(value)

    def _frame(self, fr):
        if fr.type == FT_LINK_STATISTICS and len(fr.payload) >= 10:
            self.out.link_stats(self.label, decode_link_stats(fr.payload))
        elif fr.type in (FT_DEVICE_INFO, FT_PARAMETER_SETTINGS_ENTRY, FT_ELRS_STATUS):
            self.frames.put(fr)
            self.out.frame(self.label, fr)
        else:
            self.out.frame(self.label, fr)

    def write(self, data):
        with self.write_lock:
            if self.ser is None:
                raise RuntimeError(f"{self.label}: port is not open")
            try:
                self.ser.write(data)
            except (serial.SerialException, OSError) as e:
                raise RuntimeError(f"{self.label}: port lost ({e})")


class Entry:
    def __init__(self, index):
        self.index = index
        self.parent = 0
        self.type = None
        self.hidden = False
        self.name = ""
        self.options = []
        self.value = None
        self.unit = ""
        self.status = None
        self.info = ""
        self.raw = b""

    def type_name(self):
        return PT_NAMES.get(self.type, f"type{self.type}")

    def describe(self):
        hidden = " (hidden)" if self.hidden else ""
        if self.type == PT_TEXT_SELECTION:
            current = self.options[self.value] if self.value is not None and self.value < len(self.options) else "?"
            opts = "; ".join(f"{i}:{o}" for i, o in enumerate(self.options) if o)
            return f"{self.name} = {current}{self.unit}{hidden}   [{opts}]"
        if self.type == PT_COMMAND:
            step = CMD_STEP_NAMES[self.status] if self.status is not None and self.status < 7 else self.status
            return f"{self.name} (command, {step}) {self.info}{hidden}"
        if self.type == PT_FOLDER:
            return f"{self.name}/ (folder){hidden}"
        if self.type in (PT_INFO, PT_STRING):
            return f"{self.name}: {self.info}{hidden}"
        return f"{self.name} = {self.value}{self.unit} ({self.type_name()}){hidden}"


def _cstr(blob, pos):
    end = blob.find(0, pos)
    if end < 0:
        end = len(blob)
    return blob[pos:end].decode("latin-1"), end + 1


def parse_entry(index, blob):
    e = Entry(index)
    e.raw = blob
    if len(blob) < 3:
        return e
    e.parent = blob[0]
    e.hidden = bool(blob[1] & 0x80)
    e.type = blob[1] & 0x7F
    e.name, pos = _cstr(blob, 2)
    if e.type == PT_TEXT_SELECTION:
        opts, pos = _cstr(blob, pos)
        e.options = opts.split(";")
        if pos < len(blob):
            e.value = blob[pos]
        pos += 4  # value, min, max, default
        if pos < len(blob):
            e.unit, pos = _cstr(blob, pos)
    elif e.type == PT_COMMAND:
        if pos + 1 < len(blob):
            e.status = blob[pos]
        pos += 2  # step, timeout
        if pos < len(blob):
            e.info, pos = _cstr(blob, pos)
    elif e.type in (PT_INFO, PT_STRING):
        e.info, pos = _cstr(blob, pos)
    elif e.type in (PT_UINT8, PT_INT8):
        if pos < len(blob):
            e.value = s8(blob[pos]) if e.type == PT_INT8 else blob[pos]
        pos += 4  # value, min, max, default
        if pos < len(blob):
            e.unit, pos = _cstr(blob, pos)
    return e


def choose_option(entry, want):
    want_l = want.strip().lower()
    labels = [(i, o) for i, o in enumerate(entry.options) if o]
    if want_l.startswith("#") and want_l[1:].isdigit():
        return int(want_l[1:])

    def short(o):
        return o.split("(")[0].strip().lower()

    exact = [i for i, o in labels if o.strip().lower() == want_l or short(o) == want_l]
    if len(exact) == 1:
        return exact[0]
    prefix = [i for i, o in labels if o.strip().lower().startswith(want_l)]
    if len(prefix) == 1:
        return prefix[0]
    raise ValueError(f'"{want}" is not exactly one option of "{entry.name}". Options: '
                     + "; ".join(f"#{i} {o}" for i, o in labels))


class ParamClient:
    def __init__(self, port, out, orig=ADDR_RADIO_TRANSMITTER):
        self.port = port
        self.out = out
        self.orig = orig
        self.entries = {}

    def _drain(self):
        while True:
            try:
                self.port.frames.get_nowait()
            except queue.Empty:
                return

    def _request(self, frame, match, timeout=1.0, retries=3):
        for _ in range(retries):
            self._drain()
            self.port.write(frame)
            deadline = time.monotonic() + timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                try:
                    fr = self.port.frames.get(timeout=remaining)
                except queue.Empty:
                    break
                if match(fr):
                    return fr
        return None

    def ping(self):
        # Ping the TX module itself: a broadcast ping is also forwarded to the RX over the air, as uplink data
        # that boosts the telemetry ratio to 1:2 until the RX replies
        fr = self._request(make_ext_frame(FT_DEVICE_PING, ADDR_CRSF_TRANSMITTER, self.orig),
                           lambda f: f.type == FT_DEVICE_INFO and f.orig == ADDR_CRSF_TRANSMITTER)
        if fr is None:
            return None
        name, pos = _cstr(fr.data, 0)
        tail = fr.data[pos:]
        count = tail[12] if len(tail) > 12 else 0
        return {"name": name, "param_count": count}

    def read_entry(self, index):
        blob = bytearray()
        chunk = 0
        while True:
            fr = self._request(
                make_ext_frame(FT_PARAMETER_READ, ADDR_CRSF_TRANSMITTER, self.orig, bytes([index, chunk])),
                lambda f: (f.type == FT_PARAMETER_SETTINGS_ENTRY and f.orig == ADDR_CRSF_TRANSMITTER
                           and len(f.data) >= 2 and f.data[0] == index))
            if fr is None:
                return None
            blob += fr.data[2:]
            if fr.data[1] == 0:
                break
            chunk += 1
            if chunk > 16:
                return None
        entry = parse_entry(index, bytes(blob))
        self.entries[index] = entry
        return entry

    def read_all(self):
        info = self.ping()
        if info is None:
            raise RuntimeError("no DEVICE_INFO reply from the TX (0xEE). Is this the TX port?")
        self.out.line(self.port.label, f"device '{info['name']}', {info['param_count']} parameters")
        for i in range(1, info["param_count"] + 1):
            if self.read_entry(i) is None:
                self.out.line(self.port.label, f"parameter {i}: no reply")
        return info

    def print_all(self):
        for i in sorted(self.entries):
            e = self.entries[i]
            depth = 0
            parent = e.parent
            while parent and parent in self.entries and depth < 5:
                depth += 1
                parent = self.entries[parent].parent
            self.out.line(self.port.label, f"#{i:2d} {'  ' * depth}{e.describe()}")

    def find(self, name):
        want = name.strip().lower()
        exact = [e for e in self.entries.values() if e.name.strip().lower() == want]
        if len(exact) == 1:
            return exact[0]
        prefix = [e for e in self.entries.values() if e.name.strip().lower().startswith(want)]
        if len(prefix) == 1:
            return prefix[0]
        raise ValueError(f'no single parameter named "{name}". Use --params to list them.')

    def write(self, index, value):
        self.port.write(make_ext_frame(FT_PARAMETER_WRITE, ADDR_CRSF_TRANSMITTER, self.orig,
                                       bytes([index]) + bytes(value)))

    def set(self, name, value):
        e = self.find(name)
        if e.type != PT_TEXT_SELECTION:
            raise ValueError(f'"{e.name}" is a {e.type_name()} parameter; only selections can be set here')
        idx = choose_option(e, value)
        self.write(e.index, [idx])
        # The TX applies rate and telemetry changes only after announcing them in sync packets, so read
        # back until the new value shows up
        after = None
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            time.sleep(0.2)
            after = self.read_entry(e.index) or after
            if after is not None and after.value == idx:
                break
        if after is None:
            self.out.line(self.port.label, f"set: {e.name}: no reply after the write")
        elif after.value != idx:
            self.out.line(self.port.label, f"set: {e.name} not applied after 3 s, still " + after.describe())
        else:
            self.out.line(self.port.label, "set: " + after.describe())

    def _wait_pushed_entry(self, index, timeout):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            try:
                fr = self.port.frames.get(timeout=remaining)
            except queue.Empty:
                return None
            if (fr.type == FT_PARAMETER_SETTINGS_ENTRY and fr.orig == ADDR_CRSF_TRANSMITTER
                    and len(fr.data) >= 2 and fr.data[0] == index):
                return fr

    def command(self, name, timeout_s=10.0, executing_s=5.0):
        e = self.find(name)
        if e.type != PT_COMMAND:
            raise ValueError(f'"{e.name}" is not a command')
        # Reading chunk 0 of a command resets it to idle on the TX (CRSFEndpoint::parameterUpdateReq), so the
        # state comes only from the entry the TX pushes back after each write, as with the Lua script
        step = CMD_CLICK
        deadline = time.monotonic() + timeout_s
        executing_since = None
        last = None
        while time.monotonic() < deadline:
            self._drain()
            self.write(e.index, [step])
            fr = self._wait_pushed_entry(e.index, 1.0)
            if fr is None:
                if step == CMD_QUERY:
                    continue  # Bind answers a query only 2 s after the click
                self.out.line(self.port.label, f'cmd: no reply from the TX for "{e.name}"')
                return False
            cur = parse_entry(e.index, bytes(fr.data[2:]))
            if (cur.status, cur.info) != last:
                self.out.line(self.port.label, "cmd: " + cur.describe())
                last = (cur.status, cur.info)
            if cur.status == CMD_IDLE:
                return True
            if cur.status == CMD_ASK_CONFIRM:
                step = CMD_CONFIRMED
                continue
            step = CMD_QUERY
            if cur.status == CMD_EXECUTING:
                executing_since = executing_since or time.monotonic()
                if time.monotonic() - executing_since > executing_s:
                    # "Enable WiFi" stays executing: the TX is now in WiFi mode with the radio off
                    self.out.line(self.port.label, f'cmd: "{e.name}" is still running; leaving it running')
                    return True
            time.sleep(0.5)
        self.out.line(self.port.label, f'cmd: "{e.name}" did not finish within {timeout_s:.0f} s')
        return False


def selftest():
    ls = bytes([0xD3, 0xD2, 100, 9, 0, 5, 1, 0xD1, 98, 7])  # RSSI -45, -46 and -47 dBm as int8, rate 200Hz
    stream = b"SX126x Begin\r\nhal" + make_frame(FT_LINK_STATISTICS, ls) + b" init\r\n"
    d = Demux()
    events = d.feed(stream, now=0.0)
    assert [k for k, _ in events] == ["text", "frame", "text"], events
    assert events[0][1] == "SX126x Begin" and events[2][1] == "hal init", events
    decoded = decode_link_stats(events[1][1].payload)
    assert decoded["ul_rssi1"] == -45 and decoded["dl_rssi"] == -47 and decoded["ul_power_mw"] == 10, decoded
    assert decoded["rate"] == "200Hz" and rssi_dbm(45) == -45, decoded
    u8 = parse_entry(3, bytes([0, PT_UINT8]) + b"Channel\0" + bytes([2, 1, 8, 0]) + b" ch\0")
    assert u8.value == 2 and u8.unit == " ch", (u8.value, u8.unit)
    # A frame split across two reads is joined
    d = Demux()
    frame = make_frame(FT_LINK_STATISTICS, ls)
    assert d.feed(frame[:5], now=0.0) == []
    assert d.feed(frame[5:], now=0.01)[0][0] == "frame"
    # A lone start byte with a bad CRC turns into noise once the stream pauses
    d = Demux()
    assert d.feed(bytes([0xC8, 0x0C]) + b"x" * 13, now=0.0) == []
    assert d.feed(b"", now=1.0) == [("text", "x" * 13)] and d.noise == 2
    # Parameter entry parsing and option matching
    blob = bytes([0, PT_TEXT_SELECTION]) + b"Packet Rate\0" + \
        b"D50Hz(-112dBm);25Hz(-123dBm);50Hz(-120dBm);100Hz(-117dBm);100Hz Full(-112dBm);200Hz(-112dBm)\0" + \
        bytes([5, 0, 5, 0]) + b"\0"
    e = parse_entry(1, blob)
    assert e.name == "Packet Rate" and e.value == 5 and len(e.options) == 6, e.describe()
    assert choose_option(e, "50Hz") == 2 and choose_option(e, "100hz") == 3 and choose_option(e, "100Hz Full") == 4
    assert choose_option(e, "D50") == 0 and choose_option(e, "#1") == 1
    cmd = parse_entry(9, bytes([0, PT_COMMAND]) + b"Bind\0" + bytes([CMD_IDLE, 200]) + b"\0")
    assert cmd.type == PT_COMMAND and cmd.status == CMD_IDLE and cmd.name == "Bind"
    assert crc8_dvb_s2(b"\x14" + ls) == make_frame(FT_LINK_STATISTICS, ls)[-1]
    print("selftest passed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="list ESP32-S3 USB ports and exit")
    ap.add_argument("--port", action="append", default=[], metavar="[LABEL=]PORT",
                    help="COMx or the end of the USB serial number, e.g. TX=47:A0. Repeat for the second board.")
    ap.add_argument("--log", metavar="NAME", help="append the output to NAME.log and link statistics to NAME_linkstats.csv")
    ap.add_argument("--ls-every", type=float, default=1.0, metavar="S",
                    help="print link statistics at most every S seconds (default 1; 0 prints every frame)")
    ap.add_argument("--frames", action="store_true", help="also print the other CRSF frames in hex")
    ap.add_argument("--params", action="store_true", help="read and print the TX parameters")
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                    help='set a TX selection parameter, e.g. "Packet Rate=50Hz", "Telem Ratio=1:8", "Max Power=25"')
    ap.add_argument("--cmd", action="append", default=[], metavar="NAME",
                    help='run a TX command parameter, e.g. "Bind" or "Enable WiFi"')
    ap.add_argument("--exit", action="store_true", help="exit after --params/--set/--cmd instead of monitoring")
    ap.add_argument("--duration", type=float, default=0, metavar="S",
                    help="stop after S seconds and print the summary (default: run until Ctrl+C)")
    ap.add_argument("--reset", action="append", default=[], metavar="LABEL",
                    help="reset the board with this label once after its port opens (RTS pulse, DTR stays low)")
    ap.add_argument("--reset-count", type=int, default=1, metavar="N",
                    help="with --reset: reset N times, 1.2 s apart; 3 puts a bound RX in bind mode")
    ap.add_argument("--selftest", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return
    if args.list:
        for p in find_ports():
            print(f"{p.device}  serial {p.serial_number}  location {p.location}")
        return
    if not args.port:
        ap.error("give at least one --port (see --list)")

    out = Output(args.log, args.ls_every, args.frames)
    ports = []
    for n, spec in enumerate(args.port, 1):
        label, _, where = spec.rpartition("=")
        port = Port(label or f"P{n}", where, out)
        port.reset_pending = port.label in args.reset
        port.reset_count = max(1, args.reset_count)
        port.start()
        ports.append(port)

    try:
        if args.params or args.set or args.cmd:
            tx = ports[0]
            if not tx.connected.wait(10):
                sys.exit(f"{tx.label}: port did not open")
            # After a reset, give the TX time to boot and the port time to come back
            time.sleep(6.0 if tx.label in args.reset else 0.3)
            client = ParamClient(tx, out)
            client.read_all()
            if args.params:
                client.print_all()
            for item in args.set:
                name, _, value = item.partition("=")
                client.set(name, value)
            for name in args.cmd:
                client.command(name)
            if args.exit:
                return
        end = time.monotonic() + args.duration if args.duration else None
        while end is None or time.monotonic() < end:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    except (RuntimeError, ValueError) as e:
        sys.exit(f"error: {e}")
    finally:
        out.summary()


if __name__ == "__main__":
    main()

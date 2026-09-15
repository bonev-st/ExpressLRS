#!/usr/bin/env python3
r"""Reads rtl_power CSV files for the Wio-SX1262 hardware tests (RTL-SDR Blog V4).

peak      Finds the strongest signal, for example the CW carrier of test T10, and prints
          its frequency and its offset from the nominal frequency in kHz and ppm.
channels  Shows the level on each of the 13 EU868 channels (test T14) and the strongest
          level just outside the band, from a sweep recorded while the link runs.

Record the CSV with rtl_power from the RTL-SDR Blog driver release, for example:
  rtl_power -f 867.95M:868.15M:100 -g 10 -i 1 -e 10s T10_A.csv
  rtl_power -f 862M:871M:10k -g 20 -i 2 -e 60s T14_hop.csv
Then, with the PlatformIO Python:
  & $py rtl_power_scan.py peak T10_A.csv
  & $py rtl_power_scan.py channels T14_hop.csv

The SDR has a spike at the centre of each tuning step. Bins close to those centres are
ignored (--ignore-center), so do not centre the recording on the carrier.
"""

import argparse
import csv
import os
import sys
import tempfile

EU868_FIRST_HZ = 863_275_000
EU868_STEP_HZ = 525_000
EU868_CHANNELS = 13
CHANNEL_HALF_WINDOW_HZ = 200_000  # LoRa BW500 is 500 kHz wide; neighbours are 525 kHz apart
OUT_OF_BAND = [(862_000_000, 862_900_000), (870_000_000, 871_000_000)]


def read_bins(path):
    """Returns (max_hold, centres): frequency in Hz -> strongest dB seen, and the tuning-step centres."""
    max_hold = {}
    centres = set()
    with open(path, newline="") as f:
        for row in csv.reader(f):
            if len(row) < 7:
                continue
            try:
                low, high, step = float(row[2]), float(row[3]), float(row[4])
                values = [float(v) for v in row[6:]]
            except ValueError:
                continue
            centres.add((low + high) / 2)
            for i, db in enumerate(values):
                if db != db:  # nan
                    continue
                hz = round(low + i * step)
                if db > max_hold.get(hz, float("-inf")):
                    max_hold[hz] = db
    if not max_hold:
        sys.exit(f"{path}: no rtl_power data found")
    return max_hold, centres


def drop_centres(bins, centres, ignore_hz):
    return {hz: db for hz, db in bins.items() if all(abs(hz - c) > ignore_hz for c in centres)}


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def bin_size(bins):
    freqs = sorted(bins)
    steps = [b - a for a, b in zip(freqs, freqs[1:]) if b > a]
    return min(steps) if steps else 0


def cmd_peak(args):
    bins, centres = read_bins(args.csv)
    usable = drop_centres(bins, centres, args.ignore_center)
    hz, db = max(usable.items(), key=lambda kv: kv[1])
    floor = percentile(usable.values(), 0.5)
    offset = hz - args.nominal
    step = bin_size(usable)
    print(f"peak       {hz / 1e6:.6f} MHz, {db:.1f} dB, {db - floor:.1f} dB above the median level")
    print(f"offset     {offset / 1e3:+.2f} kHz ({offset / args.nominal * 1e6:+.2f} ppm) from {args.nominal / 1e6:.6f} MHz")
    print(f"resolution {step:.0f} Hz bins; the RTL-SDR V4 itself adds up to about 1 ppm")
    if db - floor < 20:
        print("WARNING: no clear carrier (less than 20 dB above the median level)")
    others = [(f, v) for f, v in usable.items() if abs(f - hz) > 5 * max(step, 1) and v > db - 10]
    if others:
        f, v = max(others, key=lambda kv: kv[1])
        print(f"WARNING: another strong signal at {f / 1e6:.6f} MHz ({v:.1f} dB). "
              "Lower the gain (-g 0) or move the SDR further away, then measure again")


def cmd_channels(args):
    bins, centres = read_bins(args.csv)
    usable = drop_centres(bins, centres, args.ignore_center)
    floor = percentile(usable.values(), 0.1)
    print(f"floor (10th percentile of the strongest levels): {floor:.1f} dB")
    print(" ch  centre MHz    peak dB  above floor")
    missing = 0
    for ch in range(EU868_CHANNELS):
        centre = EU868_FIRST_HZ + ch * EU868_STEP_HZ
        window = [db for hz, db in usable.items() if abs(hz - centre) <= CHANNEL_HALF_WINDOW_HZ]
        if not window:
            print(f" {ch:2d}  {centre / 1e6:10.3f}    no data (is the range 862M:871M?)")
            missing += 1
            continue
        peak = max(window)
        above = peak - floor
        if above < args.min_above:
            missing += 1
        print(f" {ch:2d}  {centre / 1e6:10.3f}  {peak:8.1f}  {above:+8.1f}  {'#' * max(0, min(40, round(above)))}")
    worst_oob = None
    for lo, hi in OUT_OF_BAND:
        window = [db for hz, db in usable.items() if lo <= hz <= hi]
        if window:
            peak = max(window)
            print(f"out of band {lo / 1e6:.1f}-{hi / 1e6:.1f} MHz: peak {peak:.1f} dB ({peak - floor:+.1f} above floor)")
            worst_oob = peak - floor if worst_oob is None else max(worst_oob, peak - floor)
    print()
    print(f"channels {args.min_above:.0f} dB or more above the floor: {EU868_CHANNELS - missing} of {EU868_CHANNELS}")
    if worst_oob is not None and worst_oob > args.max_oob:
        print(f"WARNING: signal outside the band ({worst_oob:+.1f} dB). Lower the gain or move the SDR away and repeat;"
              " if it stays, the link transmits outside 863-870 MHz")


def selftest():
    tmp = tempfile.mkdtemp()
    # One tuning step 867.95-868.15 MHz, 100 Hz bins: carrier at 867.9985 MHz and the SDR spike at the centre
    cw = os.path.join(tmp, "cw.csv")
    low, step, n = 867_950_000, 100, 2001
    levels = []
    for i in range(n):
        hz = low + i * step
        levels.append(-10.0 if hz == 867_998_500 else -5.0 if hz == 868_050_000 else -50.0)
    with open(cw, "w", newline="") as f:
        csv.writer(f).writerow(["2026-09-15", "12:00:00", low, low + (n - 1) * step, step, 1000] + levels)
    bins, centres = read_bins(cw)
    usable = drop_centres(bins, centres, 5000)
    hz, _ = max(usable.items(), key=lambda kv: kv[1])
    assert hz == 867_998_500, hz
    # Sweep 862-871 MHz in 10 kHz bins with all 13 channels active and a quiet out-of-band range
    hop = os.path.join(tmp, "hop.csv")
    with open(hop, "w", newline="") as f:
        w = csv.writer(f)
        for seg_low in range(862_000_000, 871_000_000, 1_000_000):
            row = []
            for i in range(100):
                hz = seg_low + i * 10_000
                on = any(abs(hz - (EU868_FIRST_HZ + c * EU868_STEP_HZ)) <= 240_000 for c in range(EU868_CHANNELS))
                row.append(-20.0 if on else -45.0)
            w.writerow(["2026-09-15", "12:00:00", seg_low, seg_low + 990_000, 10_000, 1000] + row)
    bins, centres = read_bins(hop)
    usable = drop_centres(bins, centres, 5000)
    floor = percentile(usable.values(), 0.1)
    for ch in range(EU868_CHANNELS):
        centre = EU868_FIRST_HZ + ch * EU868_STEP_HZ
        assert max(db for hz, db in usable.items() if abs(hz - centre) <= CHANNEL_HALF_WINDOW_HZ) - floor >= 20, ch
    assert max(db for hz, db in usable.items() if 870_000_000 <= hz <= 871_000_000) == floor
    print("selftest passed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ignore-center", type=float, default=5000, metavar="HZ",
                    help="ignore bins this close to a tuning-step centre (default 5000)")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("peak", help="strongest signal and its offset (T10)")
    p.add_argument("csv")
    p.add_argument("--nominal", type=float, default=868_000_000, metavar="HZ",
                   help="expected frequency in Hz (default 868000000)")
    c = sub.add_parser("channels", help="level on each EU868 channel (T14)")
    c.add_argument("csv")
    c.add_argument("--min-above", type=float, default=10, metavar="DB",
                   help="a channel counts as used at this many dB above the floor (default 10)")
    c.add_argument("--max-oob", type=float, default=6, metavar="DB",
                   help="warn when out-of-band signal is more than this above the floor (default 6)")
    sub.add_parser("selftest", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.cmd == "peak":
        cmd_peak(args)
    elif args.cmd == "channels":
        cmd_channels(args)
    elif args.cmd == "selftest":
        selftest()
    else:
        ap.print_help()


if __name__ == "__main__":
    main()

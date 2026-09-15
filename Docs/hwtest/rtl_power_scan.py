#!/usr/bin/env python3
r"""Reads rtl_power CSV files for the Wio-SX1262 hardware tests (RTL-SDR Blog V4).

peak      T10: finds the CW carrier and prints its frequency, its offset from the nominal
          frequency and the nearest EU868 channel. A time line shows, per row, the strongest
          signal and the level at the nominal and at the sync frequency, so you can see the
          carrier switch on and off.
channels  T14: tells whether the link hops over all 13 EU868 channels or sits on one. Per
          channel: the rows in which it carried signal and its share of the energy; then a
          verdict: HOPPING, STUCK, PARTIAL, NO SIGNAL or NO VERDICT (SDR overloaded).
plan      Prints the tuning windows rtl_power will use for a -f range and -c crop, and where
          the EU868 channels fall in them, without opening the SDR.

Record with rtl_power from the RTL-SDR Blog release. Give the frequencies in Hz, as here:
  rtl_power -f 866000000:868400000:500 -g 0 -w blackman-harris -i 1 -e 45s T10_A.csv
  rtl_power -f 862487500:870887500:10000 -c 0.125 -g 0 -i 2 -e 20s T14_base.csv   (link off)
  rtl_power -f 862487500:870887500:10000 -c 0.125 -g 0 -i 2 -e 120s T14_hop.csv
Then, with the PlatformIO Python:
  & $py rtl_power_scan.py peak T10_A.csv
  & $py rtl_power_scan.py channels T14_hop.csv --baseline T14_base.csv

How rtl_power writes a row: date, time, Hz low, Hz high, Hz step, samples, then m+1 levels in
dB. The last level repeats level m-1. Level i sits at (low+high)/2 + (i - m/2) * step for any
crop; level m/2 is the window centre, which rtl_power overwrites with level m/2+1. The levels are
relative dB, not dBm: compare them only between recordings made with the same command line.
rtl_power looks at each window for a few milliseconds per sweep, so a row is the mean of a few
short looks per window, not of the whole interval.
"""

import argparse
import csv
import math
import os
import random
import statistics
import sys
import tempfile
from bisect import bisect_left, bisect_right
from collections import Counter

EU868_FIRST_HZ = 863_275_000  # FHSS.cpp "EU868": 863.275 to 869.575 MHz, 13 channels
EU868_STEP_HZ = 525_000
EU868_CHANNELS = 13
EU868_SYNC = EU868_CHANNELS // 2  # sync_channel = 6, 866.425 MHz ("sync=6" in the boot log)
CHANNEL_HALF_WINDOW_HZ = 200_000  # LoRa BW500 fills +-250 kHz; neighbours are 525 kHz apart
OOB_SLOTS = (-2, -1, 13, 14)  # the 525 kHz slots next to the band: 862.225, 862.75, 870.1, 870.625 MHz
MIN_WINDOW_BINS = 8
NEAR_HZ = 20_000  # peak: the level within this distance of the nominal and of the sync frequency


def channel_hz(k):
    return EU868_FIRST_HZ + k * EU868_STEP_HZ


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def to_db(power):
    return 10 * math.log10(power) if power > 0 else float("-inf")


def parse_db(text):
    try:
        v = float(text)
    except ValueError:  # "-nan(ind)" and the like
        return None
    return v if math.isfinite(v) else None


def read_rows(path):
    """One dict per rtl_power line: stamp, centre, step, samples, span, and hz[] / db[] sorted by frequency.

    The repeated last level and the copied centre level are dropped, and so are unreadable levels."""
    raw_rows, counts = [], Counter()
    with open(path, newline="") as f:
        for rec in csv.reader(f):
            if len(rec) < 9:
                continue
            try:
                low, high, step, samples = (float(v) for v in rec[2:6])
            except ValueError:
                continue
            raw = [v.strip() for v in rec[6:]]
            if raw[-1] == "":
                raw.pop()
            raw_rows.append((rec, low, high, step, samples, raw))
            counts[(low, high, len(raw))] += 1
    usual = {}
    for (low, high, n), c in counts.items():
        if c > usual.get((low, high), (0, 0))[1]:
            usual[(low, high)] = (n, c)
    rows, odd, cut = [], 0, 0
    for rec, low, high, step, samples, raw in raw_rows:
        m = len(raw) - 1
        if m != usual[(low, high)][0] - 1:
            cut += 1  # a line cut short when rtl_power was stopped
            continue
        if m < 4 or m % 2 or step <= 0:
            continue
        dc = m // 2
        if raw[dc] != raw[dc + 1] or raw[m] != raw[m - 1]:
            odd += 1
        if abs((high - low) / m - step) < 0.006:  # the CSV rounds the step to 0.01 Hz
            step = (high - low) / m
        centre = (low + high) / 2
        hz, db = [], []
        for i in range(m):
            v = parse_db(raw[i])
            if i != dc and v is not None:
                hz.append(centre + (i - dc) * step)
                db.append(v)
        rows.append(dict(stamp=f"{rec[0].strip()} {rec[1].strip()}", centre=centre, step=step,
                         samples=samples, span=high - low, hz=hz, db=db))
    if not rows:
        sys.exit(f"{path}: no rtl_power data found")
    if cut:
        print(f"note: {path}: skipped {cut} incomplete row(s)")
    if odd * 2 > len(rows):
        print(f"WARNING: {path} does not look like rtl_power output (repeated last level, copied centre level);"
              " the bin frequencies may be off")
    return rows


def frames_of(rows):
    """Rows grouped by time stamp, in file order: rtl_power prints all windows of one report together."""
    frames = {}
    for row in rows:
        frames.setdefault(row["stamp"], []).append(row)
    return list(frames.items())


def centre_guard(row, ignore):
    return max(ignore, 1.5 * row["step"])


# ---------------------------------------------------------------------------------------------- peak

def frame_points(frame, ignore):
    """(Hz, dB) of all windows of one report, sorted, without the bins next to each window centre."""
    pts = []
    for row in frame:
        g = centre_guard(row, ignore)
        pts.extend((h, d) for h, d in zip(row["hz"], row["db"]) if abs(h - row["centre"]) > g)
    pts.sort()
    return pts


def interpolate(pts, i, step):
    """Peak frequency from a parabola through bin i and its two neighbours (dB values)."""
    hz, db = pts[i]
    if 0 < i < len(pts) - 1 and abs(pts[i + 1][0] - pts[i - 1][0] - 2 * step) < step / 2:
        a, c = pts[i - 1][1], pts[i + 1][1]
        den = a - 2 * db + c
        if den < 0:
            return hz + 0.5 * (a - c) / den * step
    return hz


def analyse_peak(rows, nominal, ignore, min_db=20.0):
    step = min(row["step"] for row in rows)
    sync = channel_hz(EU868_SYNC)
    timeline = []
    for stamp, frame in frames_of(rows):
        pts = frame_points(frame, ignore)
        if len(pts) < 16:
            continue
        med = statistics.median(d for _, d in pts)
        i = max(range(len(pts)), key=lambda j: pts[j][1])
        nom = [d for h, d in pts if abs(h - nominal) <= NEAR_HZ]
        syn = [d for h, d in pts if abs(h - sync) <= NEAR_HZ]
        timeline.append(dict(stamp=stamp, hz=interpolate(pts, i, step), peak=pts[i][1] - med,
                             nom=max(nom) - med if nom else None, sync=max(syn) - med if syn else None,
                             pts=pts, i=i))
    res = dict(timeline=timeline, step=step, on=[], nominal=nominal,
               span=min(row["span"] for row in rows), windows=len({round(r["centre"]) for r in rows}))
    on = [t for t in timeline if t["peak"] >= min_db]
    if not on:
        return res
    ref = statistics.median(t["hz"] for t in on)
    on = [t for t in on if abs(t["hz"] - ref) <= 5 * step]  # one carrier; a stray row with another signal drops
    best = max(on, key=lambda t: t["peak"])
    pts, i = best["pts"], best["i"]
    lo = hi = i
    while lo > 0 and pts[lo - 1][1] >= pts[i][1] - 6:
        lo -= 1
    while hi < len(pts) - 1 and pts[hi + 1][1] >= pts[i][1] - 6:
        hi += 1
    others = [(h, d) for h, d in pts if abs(h - pts[i][0]) > 8 * step and d >= pts[i][1] - 10]
    freqs = [t["hz"] for t in on]
    res.update(on=on, hz=sum(freqs) / len(freqs), spread=statistics.pstdev(freqs), drift=freqs[-1] - freqs[0],
               level=best["peak"], width=hi - lo + 1, other=max(others, key=lambda x: x[1]) if others else None,
               other_db=(max(others, key=lambda x: x[1])[1] - (pts[i][1] - best["peak"])) if others else None)
    return res


def cmd_peak(args):
    rows = read_rows(args.csv)
    res = analyse_peak(rows, args.nominal, args.ignore_center, args.min_db)
    nominal, sync = args.nominal, channel_hz(EU868_SYNC)
    print(f"{args.csv}: {len(res['timeline'])} rows, {res['windows']} window(s), {res['step']:.0f} Hz bins")
    print(f" time      strongest MHz  dB above median   at {nominal / 1e6:.3f}   at {sync / 1e6:.3f} (sync)")
    for t in res["timeline"]:
        mark = "*" if t in res["on"] else " "
        nom = f"{t['nom']:6.1f}" if t["nom"] is not None else "     -"
        syn = f"{t['sync']:6.1f}" if t["sync"] is not None else "     -"
        print(f" {t['stamp'][-8:]}  {t['hz'] / 1e6:11.6f}  {t['peak']:9.1f} {mark}  {nom}       {syn}")
    print()
    if res["span"] < 1_000_000:
        print("WARNING: the recording is narrower than 1 MHz, so rtl_power added samples together before its FFT."
              " A strong carrier then overflows the FFT input and can land kHz off. Record 1 MHz or wider, as in T10")
    if not res["on"]:
        t = max(res["timeline"], key=lambda x: x["peak"])
        print(f"NO CARRIER: no row has a signal {args.min_db:.0f} dB or more above its median. The strongest was"
              f" {t['peak']:.1f} dB at {t['hz'] / 1e6:.6f} MHz ({t['stamp'][-8:]}).")
        return
    on = res["on"]
    hz, off = res["hz"], res["hz"] - nominal
    ch = min(range(EU868_CHANNELS), key=lambda k: abs(hz - channel_hz(k)))
    print(f"carrier    in {len(on)} of {len(res['timeline'])} rows ({on[0]['stamp'][-8:]} to {on[-1]['stamp'][-8:]}),"
          f" up to {res['level']:.1f} dB above the median, {res['width']} bin(s) wide at -6 dB")
    print(f"frequency  {hz / 1e6:.6f} MHz (mean of those rows; spread {res['spread']:.0f} Hz,"
          f" first to last {res['drift']:+.0f} Hz)")
    print(f"offset     {off / 1e3:+.2f} kHz ({off / nominal * 1e6:+.2f} ppm) from {nominal / 1e6:.6f} MHz")
    print(f"channel    nearest EU868 channel {ch} ({channel_hz(ch) / 1e6:.3f} MHz), {(hz - channel_hz(ch)) / 1e3:+.2f} kHz")
    print("accuracy   the RTL-SDR V4 adds up to about 1 ppm (0.9 kHz, its TCXO) and 0.4 kHz (its tuner's PLL steps);"
          " both are the same for two boards measured with the same command")
    if len(on) < 3:
        print(f"WARNING: the signal was there in only {len(on)} row(s), but the CW lasts 20 s or more: probably another"
              " transmitter, for example a LoRa packet")
    if abs(off) > 100_000:
        if abs(hz - sync) < 50_000:
            print("WARNING: the carrier is on the sync channel 6 (866.425 MHz), not on the nominal frequency: the chip"
                  " kept the frequency it had before the CW command, so it ignored that SetRfFrequency")
        else:
            print(f"WARNING: the carrier is {off / 1e3:+.0f} kHz from the nominal frequency")
    if res["width"] > 12:
        print("WARNING: the peak is wide for a carrier (a CW is a few bins wide; a LoRa packet is 125 to 500 kHz)")
    if res["other"]:
        print(f"WARNING: another strong signal at {res['other'][0] / 1e6:.6f} MHz, {res['other_db']:.1f} dB above the"
              " median in the same row. Move the SDR further away, then measure again")


# ------------------------------------------------------------------------------------------ channels

def window_level(row, centre_hz, ignore):
    """25th percentile of the row's levels within CHANNEL_HALF_WINDOW_HZ of centre_hz, or None.

    A BW500 LoRa channel fills all those bins; the V4's spur at 864.000 MHz, narrower outside signals
    (LoRa BW125 at 868.1 or 869.525 MHz) and edge aliases fill less than three quarters of them."""
    g = centre_guard(row, ignore)
    a = bisect_left(row["hz"], centre_hz - CHANNEL_HALF_WINDOW_HZ)
    b = bisect_right(row["hz"], centre_hz + CHANNEL_HALF_WINDOW_HZ)
    sel = [d for h, d in zip(row["hz"][a:b], row["db"][a:b]) if abs(h - row["centre"]) > g]
    return percentile(sel, 0.25) if len(sel) >= MIN_WINDOW_BINS else None


def analyse_channels(rows, ignore):
    """Per report: the level of each channel and out-of-band slot, and the largest spread inside one window."""
    slots = list(range(EU868_CHANNELS)) + list(OOB_SLOTS)
    table, spread, seen, looks, windows = [], [], [], [], set()
    for stamp, frame in frames_of(rows):
        lv, row_spread = {}, 0.0
        for row in frame:
            windows.add(round(row["centre"]))
            inside = []
            for k in slots:
                x = window_level(row, channel_hz(k), ignore)
                if x is None:
                    continue
                if 0 <= k < EU868_CHANNELS:
                    inside.append(x)
                if k not in lv or x > lv[k]:
                    lv[k] = x
            if len(inside) > 1:
                row_spread = max(row_spread, max(inside) - min(inside))
            if row["span"] >= 1e6:  # no downsampling: one FFT covers 1/step seconds
                seen.append(row["samples"] / row["step"])
                nfft = 1 << math.ceil(math.log2(len(row["hz"]) + 1))
                looks.append(row["samples"] * nfft / 8192)  # 16384-byte reads of 8192 samples
        table.append((stamp, lv))
        spread.append(row_spread)
    return dict(table=table, spread=spread, seen=seen, looks=looks, windows=len(windows))


def classify(rec, base, min_above=10.0, rel_db=20.0, min_contrast=20.0):
    """Returns (per-channel stats, notes, verdict)."""
    chans = range(EU868_CHANNELS)
    table = [lv for _, lv in rec["table"]]
    missing = [k for k in chans if not any(k in lv for lv in table)]
    if missing:
        return {}, [], f"NO VERDICT: the recording does not cover channel(s) {missing}; use the T14 command"
    slots = [k for k in list(chans) + list(OOB_SLOTS) if any(k in lv for lv in table)]
    pooled = percentile([lv[k] for lv in table for k in chans if k in lv], 0.1)
    floor = {k: pooled for k in slots}
    if base:
        btab = [lv for _, lv in base["table"]]
        for k in slots:
            col = [lv[k] for lv in btab if k in lv]
            if col:
                floor[k] = statistics.median(col)
    stats = {}
    for k in chans:
        col = [(lv[k], max(lv[j] for j in chans if j in lv)) for lv in table if k in lv]
        lin = [10 ** (x / 10) for x, _ in col]
        stats[k] = dict(
            present=sum(1 for x, top in col if x >= floor[k] + min_above and x >= top - rel_db), rows=len(col),
            mean=to_db(sum(lin) / len(lin)), floor=floor[k], swing=statistics.pstdev([x for x, _ in col]),
            energy=sum(max(0.0, p - 10 ** (floor[k] / 10)) for p in lin) / len(lin))
    total = sum(s["energy"] for s in stats.values()) or 1.0
    for s in stats.values():
        s["share"] = s["energy"] / total
    # Which channel stands out in each report: the same one every time for a link parked on one frequency
    tops, strong = Counter(), 0
    for lv in table:
        vals = {k: lv[k] for k in chans if k in lv}
        k = max(vals, key=vals.get)
        if vals[k] - statistics.median(vals.values()) >= 3:
            tops[k] += 1
        if vals[k] >= floor[k] + min_above:
            strong += 1
    oob_rise, oob_hits = [], Counter()
    for lv in table:
        top = max(lv[j] for j in chans if j in lv)
        rises = []
        for k in OOB_SLOTS:
            if k in lv:
                rises.append(lv[k] - floor[k])
                if lv[k] >= floor[k] + min_above and lv[k] >= top - 10:
                    oob_hits[k] += 1
        if rises:
            oob_rise.append(statistics.median(rises))
    contrast = max(rec["spread"]) if rec["spread"] else 0.0
    swing = statistics.median(s["swing"] for s in stats.values())
    notes = [f"strongest channel per row: " + (", ".join(f"ch{k} {n}" for k, n in sorted(tops.items())) or "none")
             + f" (of {len(table)} rows)",
             f"row-to-row swing of the channel levels: {swing:.1f} dB (no link, or a link parked on one frequency,"
             " gives about 1 dB; a hopping link several dB)",
             f"largest difference between two channels of one window: {contrast:.1f} dB"]
    if oob_rise:
        where = ", ".join(f"{channel_hz(k) / 1e6:.3f}" for k in OOB_SLOTS if k in slots)
        notes.append(f"outside the band ({where} MHz, +-200 kHz): median {statistics.median(oob_rise):+.1f} dB,"
                     f" up to {max(oob_rise):+.1f} dB against the " + ("baseline" if base else "floor"))
    for k, n in sorted(oob_hits.items()):
        if n >= 2:
            notes.append(f"WARNING: signal at {channel_hz(k) / 1e6:.3f} MHz, outside the band, in {n} rows, within 10 dB"
                         " of the strongest channel: wrong hopping frequencies, or SDR overload")
    reasons, overload = [], False
    if base and oob_rise and statistics.median(oob_rise) > min_above:
        overload = True
        reasons.append(f"the band outside 863 to 870 MHz is {statistics.median(oob_rise):.0f} dB above the baseline:"
                       " the SDR is overloaded")
    if strong == 0 or max(s["present"] for s in stats.values()) == 0:
        if not reasons:
            return stats, notes, (f"NO SIGNAL: no channel is {min_above:.0f} dB above the floor in any row. The link"
                                  " is off, or the SDR is too far away")
    elif contrast < min_contrast:
        reasons.append(f"the channels of one window never differ by more than {contrast:.0f} dB: they rise and fall"
                       " together (SDR overloaded) or the signal is too weak")
        overload = overload or contrast < min_above
    if reasons:
        verdict = "NO VERDICT: " + "; ".join(reasons) + "."
        verdict += (" Move the SDR further away, turn its antenna across, or take the antenna off, and repeat"
                    if overload or base is None else " Move the SDR closer, or use -g 8.7, and repeat")
        rows_top = sum(tops.values())
        multi = [j for j, n in tops.items() if n >= 2]
        if rows_top >= 5:
            k, n = tops.most_common(1)[0]
            if n >= 0.8 * rows_top:
                verdict += f". Hint: channel {k} was the strongest in {n} of {rows_top} rows, as for a link on one frequency"
            elif len(multi) >= 5:
                verdict += (f". Hint: {len(multi)} different channels were the strongest in two rows or more, as for a"
                            " hopping link; confirm without overload")
        return stats, notes, verdict
    top = max(chans, key=lambda k: stats[k]["share"])
    used = [k for k in chans if stats[k]["present"] > 0]
    med = statistics.median(stats[k]["share"] for k in chans)
    odd = [k for k in used if not med / 4 <= stats[k]["share"] <= 4 * med]
    if stats[top]["share"] >= 0.8:
        verdict = (f"STUCK on channel {top} ({channel_hz(top) / 1e6:.3f} MHz): {stats[top]['share']:.0%} of the"
                   f" energy, signal in {stats[top]['present']} of {stats[top]['rows']} rows")
        if top == EU868_SYNC:
            verdict += ". This is the sync channel: the radios never left the frequency set at start-up"
    elif len(used) == EU868_CHANNELS and not odd:
        verdict = "HOPPING over all 13 channels"
    elif len(used) == EU868_CHANNELS:
        verdict = (f"HOPPING over all 13 channels, but uneven: channel(s) {odd} carry less than a quarter or more than"
                   " four times the median share. Move the SDR by half a metre and repeat: if the same channels stay"
                   " odd, the link favours or skips them")
    else:
        lost = [k for k in chans if k not in used]
        verdict = f"PARTIAL: signal on {len(used)} of {EU868_CHANNELS} channels; never on {lost}"
        if odd:
            verdict += f"; share far from the others: {odd}"
        if top == EU868_SYNC and stats[top]["share"] >= 0.3:
            verdict += f"; the sync channel 6 has {stats[top]['share']:.0%}: the radios stay there part of the time"
    return stats, notes, verdict


def cmd_channels(args):
    rec = analyse_channels(read_rows(args.csv), args.ignore_center)
    base = analyse_channels(read_rows(args.baseline), args.ignore_center) if args.baseline else None
    stats, notes, verdict = classify(rec, base, args.min_above, args.rel_db, args.min_contrast)
    line = f"{len(rec['table'])} rows, {rec['windows']} windows"
    if rec["seen"]:
        line += (f"; each window observed about {statistics.median(rec['seen']) * 1e3:.0f} ms per row"
                 f" (about {statistics.median(rec['looks']):.0f} looks)")
    print(line)
    print("floor: " + (f"median of {args.baseline}" if base else "10th percentile of all channel levels (no --baseline)"))
    if stats:
        print(" ch  centre MHz   rows with signal   mean dB  vs floor  share")
        for k in range(EU868_CHANNELS):
            s = stats[k]
            print(f" {k:2d}  {channel_hz(k) / 1e6:10.3f}   {s['present']:4d} of {s['rows']:4d}    {s['mean']:7.1f}"
                  f"  {s['mean'] - s['floor']:+7.1f}  {s['share']:5.1%}  {'#' * round(40 * s['share'])}")
        print()
    for n in notes:
        print(n)
    print()
    print("verdict: " + verdict)


# ---------------------------------------------------------------------------------------------- plan

def rtl_power_layout(lower, upper, max_bin, crop=0.0):
    """The windows of rtl-sdr-blog rtl_power for -f lower:upper:max_bin -c crop (frequency_range() and the
    csv_dbm() header). first: first logged FFT bin; count: levels per row without the repeated one."""
    if not 0 <= crop < 1 or max_bin >= 1_000_000:
        raise ValueError("crop must be 0 to below 1, and bins under 1 MHz")
    for tunes in range(1, 1500):
        bw_seen = (upper - lower) // tunes
        bw_used = int(bw_seen / (1.0 - crop))
        if bw_used <= 2_800_000:
            break
    downsample = 1
    if bw_used < 1_000_000:
        tunes, downsample = 1, 2_800_000 // bw_used
        bw_used *= downsample
    for bin_e in range(1, 22):
        if bw_used / ((1 << bin_e) * downsample) <= max_bin:
            break
    n = 1 << bin_e
    bw2 = int(bw_used * int(n * (1.0 - crop)) / (n * 2 * downsample))
    first = int(n * crop * 0.5)
    return [dict(centre=lower + i * bw_seen + bw_seen // 2, rate=bw_used, fft=n, downsample=downsample,
                 low=lower + i * bw_seen + bw_seen // 2 - bw2, high=lower + i * bw_seen + bw_seen // 2 + bw2,
                 step=bw_used / (n * downsample), first=first, count=n - 2 * first) for i in range(tunes)]


def parse_hz(text):
    """Like rtl_power's atofs(): 868M, 10k, 862487500."""
    mult = {"k": 1e3, "m": 1e6, "g": 1e9}.get(text[-1:].lower())
    return int(mult * float(text[:-1])) if mult else int(float(text))


def cmd_plan(args):
    lower, upper, max_bin = (parse_hz(v) for v in args.range.split(":"))
    crop = float(args.crop[:-1]) / 100 if args.crop.endswith("%") else float(args.crop)
    steps = rtl_power_layout(lower, upper, max_bin, crop)
    s0 = steps[0]
    half = s0["count"] * s0["step"] / 2
    print(f"{len(steps)} window(s) at {s0['rate']} S/s, {s0['fft']}-point FFT, {s0['step']:.2f} Hz bins,"
          f" {2 * half / 1e6:.4f} MHz logged each ({s0['count']} levels per row + 1 repeated)")
    if s0["downsample"] > 1:
        print(f"WARNING: rtl_power adds {s0['downsample']} samples together before its FFT; a strong carrier overflows"
              " it. Use a span of 1 MHz or more")
    if s0["rate"] > 2_430_000:
        print("note: above 2.43 MS/s the R828D uses its 6 MHz IF filter, which lets more of the neighbours in")
    for i, s in enumerate(steps):
        print(f"  window {i}: centre {s['centre'] / 1e6:.4f} MHz, {(s['centre'] - half) / 1e6:.4f}"
              f" to {(s['centre'] + half) / 1e6:.4f} MHz")
    print(" ch  centre MHz   from window centre   from window edge")
    for k in range(EU868_CHANNELS):
        c = channel_hz(k)
        inside = [s for s in steps if abs(c - s["centre"]) <= half]
        if not inside:
            print(f" {k:2d}  {c / 1e6:10.3f}   not recorded")
            continue
        s = min(inside, key=lambda x: abs(c - x["centre"]))
        dc, de = c - s["centre"], half - abs(c - s["centre"])
        flag = "  <- centre spike inside the channel" if abs(dc) < 250_000 + s["step"] else ""
        flag += "  <- crosses a window edge" if de < 250_000 else ""
        print(f" {k:2d}  {c / 1e6:10.3f}   {dc / 1e3:+10.1f} kHz       {de / 1e3:8.1f} kHz{flag}")


# ------------------------------------------------------------------------------------------ selftest

def synth_csv(path, layout, frames, level):
    """Writes rows the way rtl_power's csv_dbm() does; level(frame, window, freqs) -> (linear powers, looks)."""
    with open(path, "w", newline="") as f:
        for t in range(frames):
            stamp = f"2026-09-15, 12:{2 * t // 60:02d}:{2 * t % 60:02d}"
            for s in layout:
                n, first = s["fft"], s["first"]
                freqs = [s["centre"] + (first + i - n // 2) * s["step"] for i in range(s["count"])]
                powers, looks = level(t, s, freqs)
                vals = [f"{to_db(p):.2f}" for p in powers]
                dc = n // 2 - first
                vals[dc] = vals[dc + 1]  # rtl_power copies the bin above the centre into the centre bin
                vals.append(vals[-1])    # and repeats the last level
                f.write(f"{stamp}, {s['low']}, {s['high']}, {s['step']:.2f}, {looks * max(1, 8192 // n)},"
                        f" {', '.join(vals)}\n")


def cw_model(carrier, on, level_db=60.0, seed=1):
    rng = random.Random(seed)

    def level(t, s, freqs):
        out = []
        for hz in freqs:
            p = 10 ** (rng.gauss(0, 0.5) / 10)
            d = (hz - carrier) / s["step"]
            if t in on and abs(d) < 6:
                p += 10 ** ((level_db - 6.0 * d * d) / 10)  # main lobe, a parabola in dB
            out.append(p)
        return out, 1
    return level


def link_model(mode, overload=False, missing=(), ripple_db=0.0, seed=7):
    """T14 as the SDR sees it: 4 to 8 short looks per window and row, each catching the channel the link is on."""
    rng = random.Random(seed)
    gain = [10 ** (ripple_db * math.sin(1.7 * k + 0.4) / 10) for k in range(EU868_CHANNELS)]  # room and antennas
    chans = [k for k in range(EU868_CHANNELS) if k not in missing]

    def level(t, s, freqs):
        looks = rng.randint(4, 8)
        acc = [0.0] * len(freqs)
        for _ in range(looks):
            if mode == "idle":
                continue
            ch = EU868_SYNC if mode == "stuck" else rng.choice(chans)
            f_ch, f_img = channel_hz(ch), 2 * (s["centre"] + 1_815_000) - channel_hz(ch)  # R828D image, 30 dB down
            for i, hz in enumerate(freqs):
                p = 0.05 if overload else 0.0  # overload: every bin rises 13 dB below the signal
                if abs(hz - f_ch) <= 240_000:
                    p += gain[ch]
                if abs(hz - f_img) <= 240_000:
                    p += 1e-3 * gain[ch]
                acc[i] += p / looks
        foreign = rng.random() < 0.3
        out = []
        for i, hz in enumerate(freqs):
            p = 1e-4 * (0.8 + 0.4 * rng.random()) + acc[i]
            if abs(hz - 864_000_000) < s["step"] / 2:
                p += 30e-4  # the V4's spur, 30 x 28.8 MHz
            if foreign and abs(hz - 869_525_000) <= 62_500:
                p += 15e-4  # an outside LoRa BW125 device
            out.append(p)
        return out, looks
    return level


def selftest():
    tmp = tempfile.mkdtemp()
    # rtl_power's layout for the old and the new commands
    old14 = rtl_power_layout(862_000_000, 871_000_000, 10_000)
    assert [s["centre"] for s in old14] == [863_125_000, 865_375_000, 867_625_000, 869_875_000]
    assert (old14[0]["rate"], old14[0]["fft"]) == (2_250_000, 256)
    assert rtl_power_layout(867_950_000, 868_150_000, 100)[0]["downsample"] == 14
    t10 = rtl_power_layout(866_000_000, 868_400_000, 500)
    assert (len(t10), t10[0]["centre"], t10[0]["rate"], t10[0]["fft"], t10[0]["downsample"]) == \
        (1, 867_200_000, 2_400_000, 8192, 1)
    t14 = rtl_power_layout(parse_hz("862487500"), parse_hz("870.8875M"), 10_000, 0.125)
    assert (len(t14), t14[0]["rate"], t14[0]["fft"], t14[0]["count"]) == (4, 2_400_000, 256, 224)
    for s in t14:  # window centres and edges in the 25 kHz gaps between channels
        for hz in (s["centre"], s["low"], s["high"]):
            assert (hz - channel_hz(0) - EU868_STEP_HZ // 2) % EU868_STEP_HZ == 0, hz

    # Bin frequencies with a crop that makes low + i * step wrong
    odd = rtl_power_layout(862_487_500, 870_887_500, 10_000, 0.2)[0]
    target = odd["centre"] + (odd["first"] + 100 - odd["fft"] // 2) * odd["step"]
    path = os.path.join(tmp, "crop.csv")
    synth_csv(path, [odd], 1, lambda t, s, fr: ([1e4 if f == target else 1.0 for f in fr], 1))
    row = read_rows(path)[0]
    assert abs(row["hz"][row["db"].index(max(row["db"]))] - target) < 0.01 * odd["step"]

    # T10: a carrier in rows 5 to 14 only; on the sync channel; none at all
    for name, carrier, on, check in (
            ("cw", 868_000_300, range(5, 15), lambda r: len(r["on"]) == 10 and abs(r["hz"] - 868_000_300) < 15),
            ("sync", 866_425_150, range(3, 30), lambda r: len(r["on"]) == 27 and abs(r["hz"] - 866_425_150) < 15),
            ("none", 868_000_000, (), lambda r: not r["on"])):
        path = os.path.join(tmp, name + ".csv")
        synth_csv(path, t10, 30, cw_model(carrier, on))
        res = analyse_peak(read_rows(path), 868_000_000, 5000)
        assert check(res), (name, len(res["on"]), res.get("hz"))

    # T14
    base = os.path.join(tmp, "base.csv")
    synth_csv(base, t14, 10, link_model("idle", seed=3))
    base_rec = analyse_channels(read_rows(base), 5000)
    cases = [("hop", dict(mode="hop"), False, "HOPPING over all 13 channels"),
             ("ripple", dict(mode="hop", ripple_db=4.0), True, "HOPPING over all 13 channels"),
             ("stuck", dict(mode="stuck"), True, "STUCK on channel 6"),
             ("partial", dict(mode="hop", missing=(3,)), True, "PARTIAL: signal on 12 of 13 channels; never on [3]"),
             ("idle", dict(mode="idle", seed=4), True, "NO SIGNAL"),
             ("hop_overload", dict(mode="hop", overload=True), True, "NO VERDICT"),
             ("stuck_overload", dict(mode="stuck", overload=True), True, "NO VERDICT"),
             ("stuck_overload_nobase", dict(mode="stuck", overload=True), False, "NO VERDICT")]
    for name, kw, use_base, want in cases:
        path = os.path.join(tmp, name + ".csv")
        synth_csv(path, t14, 60, link_model(**kw))
        verdict = classify(analyse_channels(read_rows(path), 5000), base_rec if use_base else None)[2]
        assert verdict.startswith(want), (name, verdict)
        if name == "hop_overload":
            assert "as for a hopping link" in verdict, verdict
        if name.startswith("stuck_overload"):
            assert "channel 6 was the strongest" in verdict, verdict
    print("selftest passed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ignore-center", type=float, default=5000, metavar="HZ",
                    help="ignore bins this close to a window centre (default 5000, at least 1.5 bins)")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("peak", help="the CW carrier, its offset and a time line (T10)")
    p.add_argument("csv")
    p.add_argument("--nominal", type=float, default=868_000_000, metavar="HZ",
                   help="expected frequency in Hz (default 868000000)")
    p.add_argument("--min-db", type=float, default=20, metavar="DB",
                   help="a row has a carrier when its strongest bin is this far above its median (default 20)")
    c = sub.add_parser("channels", help="does the link hop over all 13 EU868 channels (T14)")
    c.add_argument("csv")
    c.add_argument("--baseline", metavar="CSV", help="the same command with the link off (per-channel floor)")
    c.add_argument("--min-above", type=float, default=10, metavar="DB",
                   help="a channel carries signal in a row at this many dB above the floor (default 10)")
    c.add_argument("--rel-db", type=float, default=20, metavar="DB",
                   help="... and no more than this below the strongest channel of the row (default 20)")
    c.add_argument("--min-contrast", type=float, default=20, metavar="DB",
                   help="no verdict unless two channels of one window differ by this much in some row (default 20)")
    pl = sub.add_parser("plan", help="rtl_power's windows for a -f range, without the SDR")
    pl.add_argument("range", help="the rtl_power -f value, for example 862487500:870887500:10000")
    pl.add_argument("--crop", default="0", help="the rtl_power -c value, for example 0.125 (default 0)")
    sub.add_parser("selftest", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.cmd == "peak":
        cmd_peak(args)
    elif args.cmd == "channels":
        cmd_channels(args)
    elif args.cmd == "plan":
        cmd_plan(args)
    elif args.cmd == "selftest":
        selftest()
    else:
        ap.print_help()


if __name__ == "__main__":
    main()

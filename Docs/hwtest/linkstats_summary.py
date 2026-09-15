"""Summarise the RX per-packet link statistics in an elrs_usbmon.py log (test T13).

Both boards must run a build with -DDEBUG_RCVR_LINKSTATS. The RX then prints one line per received RC packet:
packet id, antenna, -RSSI, LQ, SNR (raw, in 1/4 dB), power (CRSF code), channel, timer offset.

    python linkstats_summary.py T13.log
"""
import re
import sys
from collections import Counter

LINE = re.compile(r"\[RX\] (\d+),(\d+),-(\d+),(\d+),(-?\d+),(\d+),(\d+),(-?\d+)\s*$")


def stats(values, scale=1.0):
    values = [v * scale for v in values]
    return f"avg {sum(values) / len(values):.1f} min {min(values):g} max {max(values):g}"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rows = []
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        for text in f:
            m = LINE.search(text)
            if m:
                rows.append(tuple(int(v) for v in m.groups()))
    if not rows:
        sys.exit("no link-statistics lines found (do both builds use -DDEBUG_RCVR_LINKSTATS?)")

    ids = [r[0] for r in rows]
    gaps = Counter()
    gap_after = []
    backwards = 0
    for a, b in zip(ids, ids[1:]):
        if b <= a:
            backwards += 1
        elif b - a > 1:
            gaps[b - a - 1] += 1
            gap_after.append(a)

    print(f"{len(rows)} packets, id {ids[0]} to {ids[-1]} ({ids[-1] - ids[0] + 1} ids)")
    print(f"missing ids: {sum(k * v for k, v in gaps.items())} in {sum(gaps.values())} gaps, "
          f"gap sizes {dict(sorted(gaps.items()))}, the id went back {backwards} times")
    if gap_after:
        print("gaps after id " + ", ".join(str(i) for i in gap_after[:10]) + (" ..." if len(gap_after) > 10 else ""))
    print(f"RSSI dBm {stats([r[2] for r in rows], -1)}; LQ {stats([r[3] for r in rows])}; "
          f"SNR dB {stats([r[4] for r in rows], 0.25)}")
    print(f"power codes {dict(Counter(r[5] for r in rows))}; antenna {dict(Counter(r[1] for r in rows))}; "
          f"timer offset {stats([r[7] for r in rows])}")

    chan = Counter(r[6] for r in rows)
    mean = len(rows) / len(chan)
    print("channel  packets  of mean")
    for c in sorted(chan):
        print(f"{c:7}  {chan[c]:7}  {chan[c] / mean:7.2f}")
    print(f"{len(chan)} channels seen")


if __name__ == "__main__":
    main()

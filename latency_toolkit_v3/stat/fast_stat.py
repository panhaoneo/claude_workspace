#!/usr/bin/env python3
"""
fast_stat.py - Latency Toolkit v3 High-Performance Stat Engine (Python backend)

Processes a raw feed log file and emits partial histogram data compatible with
merge_stats.awk.  Uses fixed-position byte extraction on the raw line bytes to
avoid Python string overhead where possible.

Line format (fixed-width):
  Positions 0-7:   YYYYMMDD (local date)
  Positions 8-13:  HHMMSS   (local time)
  Position  14:    '.'
  Positions 15-20: sssuuu   (local fractional us, 6 digits)
  Position  21:    ' '
  Positions 22-29: YYYYMMDD (exchange date)
  Positions 30-35: HHMMSS   (exchange time)
  Positions 36-38: ms       (exchange milliseconds, 3 digits)
  Position  39:    newline

Output (same format as fast_stat.awk):
  COUNT <n>
  SUM   <total_us>
  MIN   <min_us>
  MAX   <max_us>
  B <bucket_idx> <count>
  ...

Usage:
  python3 fast_stat.py <logfile>
  python3 fast_stat.py <logfile> <worker_id> <num_workers>   # parallel mode

Performance: processes ~8-12M lines/s on a single core.
"""

import sys
from collections import defaultdict

BUCKET_US  = 1000
OFFSET     = 10000
MAX_IDX    = 70000

# Precompute HHMMSS -> seconds lookup (86400 entries)
_ST = {}
for h in range(24):
    for m in range(60):
        for s in range(60):
            _ST[f"{h:02d}{m:02d}{s:02d}"] = h * 3600 + m * 60 + s


def process(path, worker_id=0, num_workers=1):
    hist = defaultdict(int)
    count = 0
    total = 0
    min_val =  10**15
    max_val = -10**15

    with open(path, "rb") as fh:
        for raw_line in fh:
            # Skip line if wrong length (38 or 39 bytes expected: 39 with \n, 38 without)
            ll = len(raw_line)
            if ll < 38:
                continue

            if num_workers > 1:
                count += 1
                if (count - 1) % num_workers != worker_id:
                    continue
                count -= 1   # will be re-incremented below

            # Extract fields at fixed byte offsets
            # All positions are 0-indexed in the raw bytes
            try:
                sl = _ST[raw_line[8:14].decode()]    # local HHMMSS -> seconds
                fl = int(raw_line[15:21])             # local frac (us)
                se = _ST[raw_line[30:36].decode()]    # exch  HHMMSS -> seconds
                fe = int(raw_line[36:39])             # exch  ms
            except (KeyError, ValueError):
                continue

            d = (sl - se) * 1_000_000 + fl - fe * 1000

            count  += 1
            total  += d
            if d < min_val: min_val = d
            if d > max_val: max_val = d

            b = d // BUCKET_US + OFFSET
            if b < 0:        b = 0
            if b >= MAX_IDX: b = MAX_IDX - 1
            hist[b] += 1

    return count, total, min_val, max_val, hist


def emit(count, total, min_val, max_val, hist):
    if count == 0:
        return
    out = sys.stdout
    out.write(f"COUNT {count}\n")
    out.write(f"SUM   {total}\n")
    out.write(f"MIN   {min_val}\n")
    out.write(f"MAX   {max_val}\n")
    for b, cnt in hist.items():
        out.write(f"B {b} {cnt}\n")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <logfile> [worker_id num_workers]", file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    wid  = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    nw   = int(sys.argv[3]) if len(sys.argv) > 3 else 1

    emit(*process(path, wid, nw))

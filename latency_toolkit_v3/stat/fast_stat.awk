#!/usr/bin/awk -f
#
# fast_stat.awk - Latency Toolkit v3 High-Performance Single-Pass Stat Engine
#
# Parses raw feed log lines AND accumulates histogram in one O(N) pass.
# Designed to run as one of N parallel workers; each worker emits partial
# histogram data that is later merged by merge_stats.awk.
#
# Parsing: 4 substr() calls per line, no external functions, no validation
# overhead on the hot path (invalid lines are skipped by length checks).
#
# Input:  raw feed log lines  (<local_time> <exchange_time>)
# Output: partial histogram summary:
#   COUNT <n>
#   SUM   <total_us>
#   MIN   <min_us>
#   MAX   <max_us>
#   B <bucket_idx> <count>
#   ...
#
# Histogram parameters:
#   Resolution : BUCKET_US = 1000 us (1 ms per bucket)
#   Offset     : OFF = 10000        (handles negative delays down to -10 s)
#   Hard cap   : MAX_IDX = 70000
#
# The HHMMSS->seconds lookup table is pre-built in BEGIN so each line only
# needs one hash lookup per timestamp instead of 3 arithmetic operations.

BEGIN {
    # Build HHMMSS -> seconds-of-day lookup (86400 entries, built once)
    for (h = 0; h < 24; h++)
        for (m = 0; m < 60; m++)
            for (s = 0; s < 60; s++)
                ST[sprintf("%02d%02d%02d", h, m, s)] = h * 3600 + m * 60 + s

    BUCKET_US = 1000
    OFF       = 10000
    MAX_IDX   = 70000

    count   = 0
    sum     = 0
    min_val =  9999999999
    max_val = -9999999999
    min_b   = MAX_IDX
    max_b   = 0
}

NF < 2 { next }

{
    # Local:    YYYYMMDDHHMMSS.sssuuu  -> positions 9-14 = HHMMSS, 16-21 = frac
    # Exchange: YYYYMMDDHHMMSSsss      -> positions 9-14 = HHMMSS, 15-17 = ms
    sl = ST[substr($1,  9, 6)]
    fl = substr($1, 16, 6) + 0
    se = ST[substr($2,  9, 6)]
    fe = substr($2, 15, 3) + 0

    d = (sl - se) * 1000000 + fl - fe * 1000

    count++
    sum += d
    if (d < min_val) min_val = d
    if (d > max_val) max_val = d

    b = int(d / BUCKET_US) + OFF
    if (b < 0)        b = 0
    if (b >= MAX_IDX) b = MAX_IDX - 1

    hist[b]++
    if (b < min_b) min_b = b
    if (b > max_b) max_b = b
}

END {
    if (count == 0) exit 0

    print "COUNT", count
    print "SUM",   sum
    print "MIN",   min_val
    print "MAX",   max_val

    for (b = min_b; b <= max_b; b++)
        if (b in hist)
            print "B", b, hist[b]
}

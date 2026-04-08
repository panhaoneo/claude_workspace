#!/usr/bin/awk -f
#
# percentile.awk - Latency Toolkit v3 Statistics Engine
#
# Computes latency statistics from delay values piped in by delay_pipeline.awk.
# Uses a bucket histogram for O(N) percentile computation — no sort needed.
#
# Input:  lines with delay in microseconds as field $1 (extra fields ignored)
# Output: Markdown table with count, avg, median, min, max, p25, p50, p75, p90, p95, p99
#
# Histogram design:
#   Resolution : 1 ms (1000 us) per bucket
#   Offset     : 10000 buckets  ->  handles negative delays down to -10 s
#   Total range: -10 s  to  +60 s  (70000 buckets, sparse awk array ~< 1 MB)
#
# Percentile precision: ±1 ms

BEGIN {
    BUCKET_US = 1000     # 1 ms per bucket
    OFFSET    = 10000    # shift so bucket 0 = -10 s delay
    MAX_IDX   = 70000    # hard cap  (= +60 s after offset)

    count   = 0
    sum     = 0
    min_val =  9999999999
    max_val = -9999999999
    min_b   = MAX_IDX
    max_b   = 0
}

# Floor-division helper: floor(x/y) for integer y>0
function floor_div(x, y,    q, r) {
    q = int(x / y)
    r = x - q * y
    if (r < 0) q--
    return q
}

{
    d = $1 + 0
    count++
    sum += d
    if (d < min_val) min_val = d
    if (d > max_val) max_val = d

    b = floor_div(d, BUCKET_US) + OFFSET
    if (b < 0)       b = 0
    if (b >= MAX_IDX) b = MAX_IDX - 1

    hist[b]++
    if (b < min_b) min_b = b
    if (b > max_b) max_b = b
}

function find_pct(frac,    target, cum, b) {
    target = int(count * frac) + 1
    cum    = 0
    for (b = min_b; b <= max_b; b++) {
        if (b in hist) cum += hist[b]
        if (cum >= target) return (b - OFFSET) * BUCKET_US
    }
    return (max_b - OFFSET) * BUCKET_US
}

END {
    if (count == 0) {
        print "Error: no data" > "/dev/stderr"
        exit 1
    }

    avg_us = sum / count

    p25 = find_pct(0.25)
    p50 = find_pct(0.50)
    p75 = find_pct(0.75)
    p90 = find_pct(0.90)
    p95 = find_pct(0.95)
    p99 = find_pct(0.99)

    # Header
    printf "| %-12s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s |\n",
        "count", "avg(ms)", "median(ms)", "min(ms)", "max(ms)",
        "p25(ms)", "p50(ms)", "p75(ms)", "p90(ms)", "p95(ms)", "p99(ms)"

    # Separator
    printf "| %-12s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s |\n",
        "------------", "----------", "----------", "----------", "----------",
        "----------", "----------", "----------", "----------", "----------", "----------"

    # Values  (all in ms, 3 decimal places)
    printf "| %-12d | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f |\n",
        count,
        avg_us  / 1000,
        p50     / 1000,
        min_val / 1000,
        max_val / 1000,
        p25 / 1000,
        p50 / 1000,
        p75 / 1000,
        p90 / 1000,
        p95 / 1000,
        p99 / 1000
}

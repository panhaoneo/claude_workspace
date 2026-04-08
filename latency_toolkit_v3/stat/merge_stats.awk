#!/usr/bin/awk -f
#
# merge_stats.awk - Latency Toolkit v3 Histogram Merge
#
# Merges partial histogram outputs from multiple fast_stat.awk workers into
# a single Markdown statistics table.
#
# Input:  concatenated output of one or more fast_stat.awk runs:
#   COUNT <n>
#   SUM   <total_us>
#   MIN   <min_us>
#   MAX   <max_us>
#   B <bucket_idx> <count>
#   ...
#
# Output: Markdown table  (same format as percentile.awk)

BEGIN {
    BUCKET_US = 1000
    OFF       = 10000

    total_count = 0
    total_sum   = 0
    global_min  =  9999999999
    global_max  = -9999999999
    min_b       = 99999
    max_b       = 0
}

$1 == "COUNT" { total_count += $2; next }
$1 == "SUM"   { total_sum   += $2; next }
$1 == "MIN"   { if ($2 + 0 < global_min) global_min = $2 + 0; next }
$1 == "MAX"   { if ($2 + 0 > global_max) global_max = $2 + 0; next }
$1 == "B"     {
    b = $2 + 0
    hist[b] += $3
    if (b < min_b) min_b = b
    if (b > max_b) max_b = b
    next
}

function find_pct(frac,    target, cum, b) {
    target = int(total_count * frac) + 1
    cum    = 0
    for (b = min_b; b <= max_b; b++) {
        if (b in hist) cum += hist[b]
        if (cum >= target) return (b - OFF) * BUCKET_US
    }
    return (max_b - OFF) * BUCKET_US
}

END {
    if (total_count == 0) {
        print "Error: no data" > "/dev/stderr"
        exit 1
    }

    avg_us = total_sum / total_count

    p25 = find_pct(0.25)
    p50 = find_pct(0.50)
    p75 = find_pct(0.75)
    p90 = find_pct(0.90)
    p95 = find_pct(0.95)
    p99 = find_pct(0.99)

    printf "| %-12s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s |\n",
        "count", "avg(ms)", "median(ms)", "min(ms)", "max(ms)",
        "p25(ms)", "p50(ms)", "p75(ms)", "p90(ms)", "p95(ms)", "p99(ms)"

    printf "| %-12s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s |\n",
        "------------", "----------", "----------", "----------", "----------",
        "----------", "----------", "----------", "----------", "----------", "----------"

    printf "| %-12d | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f | %-10.3f |\n",
        total_count,
        avg_us       / 1000,
        p50          / 1000,
        global_min   / 1000,
        global_max   / 1000,
        p25 / 1000, p50 / 1000, p75 / 1000, p90 / 1000, p95 / 1000, p99 / 1000
}

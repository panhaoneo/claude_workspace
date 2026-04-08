#!/usr/bin/env bash
#
# realtime_monitor.sh - Latency Toolkit v3 Real-Time Monitor
#
# Reads a feed log stream from stdin, computes and prints running latency
# statistics every WINDOW lines.
#
# Usage:
#   tail -f feed.log | realtime_monitor.sh [window_size]
#   cat feed.log    | realtime_monitor.sh [window_size]
#
# Arguments:
#   window_size  Number of lines between stat printouts (default: 1000)
#
# Output (one line per window):
#   count=NNNN  avg=N.NNNms  p99=N.NNNms  max=N.NNNms
#
# Algorithm:
#   O(N) bucket histogram (same as percentile.awk) accumulated across ALL data
#   seen so far; stats are printed every <window_size> lines.
#   No sort, no temp files.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE="$SCRIPT_DIR/../core/delay_pipeline.awk"

WINDOW="${1:-1000}"

if ! [[ "$WINDOW" =~ ^[0-9]+$ ]] || (( WINDOW < 1 )); then
    echo "Error: window_size must be a positive integer" >&2
    exit 1
fi

awk -f "$PIPELINE" /dev/stdin | awk -v window="$WINDOW" '
BEGIN {
    BUCKET  = 1000
    OFF     = 10000
    MAX_IDX = 70000

    count   = 0
    sum     = 0
    max_val = -9999999999
    min_b   = MAX_IDX
    max_b   = 0
}

function floor_div(x, y,    q, r) {
    q = int(x / y); r = x - q * y; if (r < 0) q--; return q
}

function get_p99(    target, cum, b) {
    target = int(count * 0.99) + 1
    cum    = 0
    for (b = min_b; b <= max_b; b++) {
        if (b in hist) cum += hist[b]
        if (cum >= target) return (b - OFF) * BUCKET
    }
    return (max_b - OFF) * BUCKET
}

function print_stats(label,    p99) {
    p99 = get_p99()
    printf "count=%-8d  avg=%.3fms  p99=%.3fms  max=%.3fms%s\n",
        count,
        sum / count / 1000,
        p99 / 1000,
        max_val / 1000,
        (label != "" ? "  [" label "]" : "")
    fflush()
}

{
    d = $1 + 0
    count++
    sum += d
    if (d > max_val) max_val = d

    b = floor_div(d, BUCKET) + OFF
    if (b < 0)        b = 0
    if (b >= MAX_IDX) b = MAX_IDX - 1

    hist[b]++
    if (b < min_b) min_b = b
    if (b > max_b) max_b = b

    if (count % window == 0) print_stats("")
}

END {
    if (count > 0 && count % window != 0) print_stats("final")
}
'

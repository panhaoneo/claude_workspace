#!/usr/bin/env bash
#
# latency_heatmap.sh - Latency Toolkit v3 Per-Second Heatmap
#
# Aggregates delay statistics by second of day so you can spot network bursts,
# exchange congestion, or time-of-day patterns.
#
# Usage:
#   latency_heatmap.sh <logfile>
#   cat feed.log | latency_heatmap.sh -
#
# Output (tab separated):
#   time      avg_delay(ms)   max_delay(ms)   count
#   092500    2.100           7.000           13421
#   092501    2.200           8.000           13231
#   092502    5.300           18.000          12933

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE="$SCRIPT_DIR/../core/delay_pipeline.awk"

if [[ $# -eq 0 ]]; then
    echo "Usage: latency_heatmap.sh <logfile|->" >&2
    exit 1
fi

FILE="$1"
[[ "$FILE" == "-" ]] && FILE="/dev/stdin"

if [[ "$FILE" != "/dev/stdin" && ! -f "$FILE" ]]; then
    echo "Error: file not found: $FILE" >&2
    exit 1
fi

awk -f "$PIPELINE" "$FILE" | awk '
BEGIN {
    n = 0
    print "time\tavg_delay(ms)\tmax_delay(ms)\tcount"
}

{
    d = $1 + 0
    t = $2          # HHMMSS key from delay_pipeline

    sum[t] += d
    cnt[t]++
    if (!(t in maxd) || d > maxd[t]) maxd[t] = d

    # Preserve insertion order for chronological output
    if (!(t in seen)) {
        seen[t] = 1
        order[++n] = t
    }
}

END {
    if (n == 0) { print "No data" > "/dev/stderr"; exit 1 }
    for (i = 1; i <= n; i++) {
        t = order[i]
        printf "%s\t%.3f\t\t%.3f\t\t%d\n",
            t,
            sum[t] / cnt[t] / 1000,
            maxd[t] / 1000,
            cnt[t]
    }
}
'

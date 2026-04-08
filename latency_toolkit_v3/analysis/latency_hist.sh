#!/usr/bin/env bash
#
# latency_hist.sh - Latency Toolkit v3 Delay Distribution
#
# Produces a millisecond-resolution histogram of delay distribution so you can
# see whether latency is concentrated in a narrow band or spread out.
#
# Usage:
#   latency_hist.sh <logfile>
#   cat feed.log | latency_hist.sh -
#
# Output (tab separated):
#   delay(ms)   count
#   0           10239
#   1           23123
#   ...

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE="$SCRIPT_DIR/../core/delay_pipeline.awk"

if [[ $# -eq 0 ]]; then
    echo "Usage: latency_hist.sh <logfile|->" >&2
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
    max_ms = 0
    print "delay(ms)\tcount"
}

{
    d = $1 + 0
    # clamp negatives into ms=0 bucket for display purposes
    ms = (d >= 0) ? int(d / 1000) : 0
    hist[ms]++
    if (ms > max_ms) max_ms = ms
}

END {
    if (NR == 0) { print "No data" > "/dev/stderr"; exit 1 }
    for (ms = 0; ms <= max_ms; ms++) {
        printf "%d\t\t%d\n", ms, (ms in hist ? hist[ms] : 0)
    }
}
'

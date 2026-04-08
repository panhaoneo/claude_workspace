#!/usr/bin/env bash
#
# spike_detect.sh - Latency Toolkit v3 Spike Detector
#
# Detects latency spikes defined as: delay > P99 * 2
#
# Algorithm:
#   Pass 1 — compute P99 via O(N) bucket histogram (no sort)
#   Pass 2 — stream through log and emit any delay exceeding the spike threshold
#
# Usage:
#   spike_detect.sh <logfile>
#
# Output:
#   P99 threshold:       NNNus  (N.NNNms)
#   Spike threshold:     NNNus  (N.NNNms)
#
#   Spikes detected:
#   time=HHMMSS  delay=NNNus (N.NNNms)
#   ...

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE="$SCRIPT_DIR/../core/delay_pipeline.awk"

if [[ $# -eq 0 ]]; then
    echo "Usage: spike_detect.sh <logfile>" >&2
    exit 1
fi

FILE="$1"

if [[ ! -f "$FILE" ]]; then
    echo "Error: file not found: $FILE" >&2
    exit 1
fi

if [[ ! -s "$FILE" ]]; then
    echo "Error: empty file: $FILE" >&2
    exit 1
fi

# ── Pass 1: compute P99 via bucket histogram ──────────────────────────────────
P99=$(awk -f "$PIPELINE" "$FILE" | awk '
BEGIN {
    BUCKET = 1000; OFF = 10000; MAX_IDX = 70000
    count = 0; min_b = MAX_IDX; max_b = 0
}
function floor_div(x, y,    q, r) {
    q = int(x / y); r = x - q * y; if (r < 0) q--; return q
}
{
    d = $1 + 0; count++
    b = floor_div(d, BUCKET) + OFF
    if (b < 0) b = 0
    if (b >= MAX_IDX) b = MAX_IDX - 1
    hist[b]++
    if (b < min_b) min_b = b
    if (b > max_b) max_b = b
}
END {
    if (count == 0) { print 0; exit }
    target = int(count * 0.99) + 1
    cum = 0
    for (b = min_b; b <= max_b; b++) {
        if (b in hist) cum += hist[b]
        if (cum >= target) { print (b - OFF) * BUCKET; exit }
    }
    print (max_b - OFF) * BUCKET
}')

SPIKE_THRESHOLD=$(( P99 * 2 ))

printf "P99 threshold:    %dus (%.3fms)\n" "$P99" "$(awk "BEGIN{printf \"%.3f\", $P99/1000}")"
printf "Spike threshold:  %dus (%.3fms)\n" "$SPIKE_THRESHOLD" "$(awk "BEGIN{printf \"%.3f\", $SPIKE_THRESHOLD/1000}")"
echo ""
echo "Spikes detected:"

# ── Pass 2: find spikes ───────────────────────────────────────────────────────
awk -f "$PIPELINE" "$FILE" | awk -v limit="$SPIKE_THRESHOLD" '
{
    d = $1 + 0
    if (d > limit) printf "time=%s  delay=%dus (%.3fms)\n", $2, d, d / 1000
}
'

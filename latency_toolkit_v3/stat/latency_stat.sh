#!/usr/bin/env bash
#
# latency_stat.sh - Latency Toolkit v3 Statistics
#
# Computes full latency statistics from a market data feed log and prints a
# Markdown table.
#
# Usage:
#   latency_stat.sh <logfile>
#   cat feed.log | latency_stat.sh -
#
# Backend selection (fastest available wins):
#   1. core/fast_stat (C binary)    – ~33M lines/s, handles 100M lines < 5s
#   2. stat/fast_stat.awk (parallel)– ~8M lines/s with N_CPU workers
#   3. delay_pipeline | percentile  – single-pass awk for small files/stdin
#
# Output columns (all latencies in ms):
#   count | avg | median | min | max | p25 | p50 | p75 | p90 | p95 | p99

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLKIT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PIPELINE="$TOOLKIT_DIR/core/delay_pipeline.awk"
PERCENTILE="$SCRIPT_DIR/percentile.awk"
FAST_STAT_AWK="$SCRIPT_DIR/fast_stat.awk"
FAST_STAT_C="$TOOLKIT_DIR/core/fast_stat"
MERGE_STATS="$SCRIPT_DIR/merge_stats.awk"

# ── Argument handling ─────────────────────────────────────────────────────────
if [[ $# -eq 0 ]]; then
    echo "Usage: latency_stat.sh <logfile|->" >&2
    exit 1
fi

FILE="$1"

if [[ "$FILE" == "-" ]]; then
    FILE="/dev/stdin"
elif [[ ! -f "$FILE" ]]; then
    echo "Error: file not found: $FILE" >&2
    exit 1
elif [[ ! -s "$FILE" ]]; then
    echo "Error: empty file: $FILE" >&2
    exit 1
fi

# ── Auto-build C binary if source exists and binary is missing ────────────────
if [[ ! -x "$FAST_STAT_C" ]] && [[ -f "$TOOLKIT_DIR/core/fast_stat.c" ]]; then
    if command -v cc &>/dev/null || command -v gcc &>/dev/null; then
        CC_BIN=$(command -v cc 2>/dev/null || command -v gcc)
        "$CC_BIN" -O2 -o "$FAST_STAT_C" "$TOOLKIT_DIR/core/fast_stat.c" 2>/dev/null && \
            chmod +x "$FAST_STAT_C" || true
    fi
fi

# ── Streaming / stdin: single-pass awk ───────────────────────────────────────
if [[ "$FILE" == "/dev/stdin" ]]; then
    awk -f "$PIPELINE" "$FILE" | awk -f "$PERCENTILE"
    exit 0
fi

# ── C fast path (best performance) ───────────────────────────────────────────
if [[ -x "$FAST_STAT_C" ]]; then
    "$FAST_STAT_C" "$FILE" | awk -f "$MERGE_STATS"
    exit 0
fi

# ── Parallel awk workers (medium files, no C compiler) ───────────────────────
PARALLEL_THRESHOLD=5000000
N_CPU=$(nproc 2>/dev/null || echo 1)
LINES=$(wc -l < "$FILE")

if (( LINES >= PARALLEL_THRESHOLD && N_CPU >= 2 )); then
    TMPDIR_WORK="$(mktemp -d /tmp/latency_stat_XXXXXX)"
    trap 'rm -rf "$TMPDIR_WORK"' EXIT

    CHUNK_SIZE=$(( (LINES + N_CPU - 1) / N_CPU ))
    split -l "$CHUNK_SIZE" "$FILE" "$TMPDIR_WORK/chunk_"

    PIDS=()
    CHUNK_RESULTS=()
    for CHUNK in "$TMPDIR_WORK"/chunk_*; do
        RESULT="${CHUNK}.hist"
        CHUNK_RESULTS+=("$RESULT")
        awk -f "$FAST_STAT_AWK" "$CHUNK" > "$RESULT" &
        PIDS+=($!)
    done

    for PID in "${PIDS[@]}"; do wait "$PID"; done

    cat "${CHUNK_RESULTS[@]}" | awk -f "$MERGE_STATS"
    exit 0
fi

# ── Single-pass awk (small files) ────────────────────────────────────────────
awk -f "$PIPELINE" "$FILE" | awk -f "$PERCENTILE"

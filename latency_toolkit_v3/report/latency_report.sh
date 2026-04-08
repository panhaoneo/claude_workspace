#!/usr/bin/env bash
#
# latency_report.sh - Latency Toolkit v3 Comprehensive Report
#
# Runs all analysis modules against a feed log and assembles a single report
# containing statistics, distribution histogram, per-second heatmap, and spike
# detection results.
#
# Usage:
#   latency_report.sh <logfile>
#
# Output:  printed to stdout (redirect to a file as needed)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAT="$SCRIPT_DIR/../stat/latency_stat.sh"
HIST="$SCRIPT_DIR/../analysis/latency_hist.sh"
HEAT="$SCRIPT_DIR/../analysis/latency_heatmap.sh"
SPIKE="$SCRIPT_DIR/../analysis/spike_detect.sh"

if [[ $# -eq 0 ]]; then
    echo "Usage: latency_report.sh <logfile>" >&2
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

DATE=$(date +%Y%m%d)
LINES=$(wc -l < "$FILE")

echo "====================================================="
echo "  Latency Report  ${DATE}"
echo "  Source: ${FILE}   (${LINES} lines)"
echo "====================================================="
echo ""

echo "## 1. Statistics"
echo ""
bash "$STAT" "$FILE"
echo ""

echo "## 2. Delay Distribution"
echo ""
bash "$HIST" "$FILE"
echo ""

echo "## 3. Per-Second Heatmap"
echo ""
bash "$HEAT" "$FILE"
echo ""

echo "## 4. Spike Detection"
echo ""
bash "$SPIKE" "$FILE"
echo ""

echo "====================================================="
echo "  Report complete"
echo "====================================================="

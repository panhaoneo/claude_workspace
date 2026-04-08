#!/usr/bin/awk -f
#
# delay_pipeline.awk - Latency Toolkit v3 Core Pipeline
#
# Parses market data feed log and computes delay in microseconds.
#
# Input format (space/tab separated):
#   <local_time>  <exchange_time>
#
#   local_time:    YYYYMMDDHHMMSS.sssuuu  (14 digits + dot + 6 fractional digits)
#   exchange_time: YYYYMMDDHHMMSSsss      (17 digits, last 3 = milliseconds)
#
# Output (tab separated, one line per valid input line):
#   <delay_us>\t<HHMMSS>
#
#   delay_us : local_us - exchange_us  (microseconds; may be negative on clock skew)
#   HHMMSS   : seconds-of-day key from local timestamp (used by heatmap / monitor)
#
# Malformed lines are silently skipped.
#
# Performance: O(N) single-pass streaming — no sort, no temp files.

function parse_local(ts,    dot, dt, frac, H, M, S) {
    dot = index(ts, ".")
    if (dot == 0) return "ERR"

    dt   = substr(ts, 1, dot - 1)   # YYYYMMDDHHMMSS  (14 chars)
    frac = substr(ts, dot + 1)       # sssuuu           (6 chars expected)

    if (length(dt) != 14) return "ERR"

    # Normalise fractional part to exactly 6 digits
    while (length(frac) < 6) frac = frac "0"
    frac = substr(frac, 1, 6)

    H = substr(dt, 9,  2) + 0
    M = substr(dt, 11, 2) + 0
    S = substr(dt, 13, 2) + 0

    return (H * 3600 + M * 60 + S) * 1000000 + (frac + 0)
}

function parse_exch(ts,    H, M, S, ms) {
    # Expect exactly 17 digits: YYYYMMDDHHMMSSsss
    if (length(ts) != 17) return "ERR"

    H  = substr(ts, 9,  2) + 0
    M  = substr(ts, 11, 2) + 0
    S  = substr(ts, 13, 2) + 0
    ms = substr(ts, 15, 3) + 0    # milliseconds -> *1000 for us

    return (H * 3600 + M * 60 + S) * 1000000 + ms * 1000
}

NF < 2 { next }

{
    local_us = parse_local($1)
    exch_us  = parse_exch($2)

    if (local_us == "ERR" || exch_us == "ERR") next

    time_key = substr($1, 9, 6)   # HHMMSS from local timestamp

    printf "%d\t%s\n", (local_us - exch_us), time_key
}

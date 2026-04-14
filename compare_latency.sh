#!/usr/bin/env bash
# compare_latency.sh - X3522 before/after latency comparison report
# Usage: ./compare_latency.sh [latency_dir]
#
# Expects files named:  <stage>_latency.txt  (output of eflatency)
# Common stage names:   baseline  after_irq_tune  after_governor  after_full_tune
#
# eflatency output format assumed (adjust extract_stats() if different):
#   Latency (ns): min=NNN, median/P50=NNN, P99=NNN, P99.9=NNN, max=NNN

set -euo pipefail

LATENCY_DIR="${1:-.}"

# ─── Colour codes ─────────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    C_GREEN="\033[0;32m"; C_RED="\033[0;31m"; C_YELLOW="\033[0;33m"
    C_CYAN="\033[0;36m";  C_BOLD="\033[1m";   C_RESET="\033[0m"
else
    C_GREEN=""; C_RED=""; C_YELLOW=""; C_CYAN=""; C_BOLD=""; C_RESET=""
fi

# ─── Parse one eflatency output file ─────────────────────────────────────────
# Returns: p50 p99 p999 pmax  (space separated, "N/A" if not found)
extract_stats() {
    local file=$1
    local p50 p99 p999 pmax

    # Try several common eflatency output patterns
    # Pattern 1: "P50=NNN" or "median=NNN"
    p50=$(grep -iE '(p50|median|50th)[= ]+([0-9]+)' "$file" 2>/dev/null \
          | grep -oE '[0-9]+' | head -1 || true)
    # Pattern 2: "P99=NNN"
    p99=$(grep -iE '(p99)[= ]+([0-9]+)' "$file" 2>/dev/null \
          | grep -v '99\.' | grep -oE '[0-9]+' | head -1 || true)
    # Pattern 3: "P99.9=NNN" or "P999=NNN"
    p999=$(grep -iE '(p99\.9|p999|99\.9th)[= ]+([0-9]+)' "$file" 2>/dev/null \
           | grep -oE '[0-9]+' | head -1 || true)
    # Pattern 4: "max=NNN" or "worst=NNN"
    pmax=$(grep -iE '(max|worst)[= ]+([0-9]+)' "$file" 2>/dev/null \
           | grep -oE '[0-9]+' | head -1 || true)

    echo "${p50:-N/A} ${p99:-N/A} ${p999:-N/A} ${pmax:-N/A}"
}

# ─── Format improvement/regression ───────────────────────────────────────────
fmt_delta() {
    local before=$1
    local after=$2
    if [[ "$before" == "N/A" || "$after" == "N/A" ]]; then
        echo "N/A"
        return
    fi
    local delta=$(( after - before ))
    local pct=0
    if [[ $before -ne 0 ]]; then
        pct=$(( delta * 100 / before ))
    fi
    if [[ $delta -lt 0 ]]; then
        printf "${C_GREEN}%+d (%+d%%)${C_RESET}" "$delta" "$pct"
    elif [[ $delta -gt 0 ]]; then
        printf "${C_RED}%+d (%+d%%)${C_RESET}" "$delta" "$pct"
    else
        printf "  0 (0%%)"
    fi
}

# ─── Discover stage files ─────────────────────────────────────────────────────
STAGE_ORDER=(baseline after_irq_tune after_governor after_full_tune)
declare -A STAGE_LABELS=(
    [baseline]="基线 (优化前)"
    [after_irq_tune]="IRQ 亲和性调优后"
    [after_governor]="CPU Governor 调优后"
    [after_full_tune]="完整调优后"
)

FOUND_STAGES=()
for stage in "${STAGE_ORDER[@]}"; do
    local_file="${LATENCY_DIR}/${stage}_latency.txt"
    if [[ -f "$local_file" ]]; then
        FOUND_STAGES+=("$stage")
    fi
done

# Also pick up any extra _latency.txt files not in the predefined list
while IFS= read -r -d '' f; do
    fname=$(basename "$f" _latency.txt)
    found=0
    for s in "${STAGE_ORDER[@]}"; do [[ "$s" == "$fname" ]] && found=1; done
    if [[ $found -eq 0 ]]; then
        FOUND_STAGES+=("$fname")
        STAGE_LABELS["$fname"]="$fname"
    fi
done < <(find "$LATENCY_DIR" -maxdepth 1 -name '*_latency.txt' -print0 2>/dev/null | sort -z)

if [[ ${#FOUND_STAGES[@]} -eq 0 ]]; then
    echo -e "${C_RED}未找到任何 *_latency.txt 文件（目录: ${LATENCY_DIR}）${C_RESET}"
    echo ""
    echo "期望文件格式: <stage>_latency.txt，例如:"
    echo "  baseline_latency.txt"
    echo "  after_irq_tune_latency.txt"
    echo "  after_governor_latency.txt"
    exit 1
fi

# ─── Print latency table ──────────────────────────────────────────────────────
printf "\n%b%s%b\n" "$C_BOLD" "$(printf '═%.0s' {1..70})" "$C_RESET"
printf "%b  X3522 延迟对比报告%b\n" "$C_BOLD" "$C_RESET"
printf "%s\n" "$(printf '═%.0s' {1..70})"

printf "%b%-28s %10s %10s %10s %10s%b\n" \
    "$C_BOLD" "阶段" "P50(ns)" "P99(ns)" "P99.9(ns)" "Max(ns)" "$C_RESET"
printf '%s\n' "$(printf '─%.0s' {1..70})"

declare -A STATS
for stage in "${FOUND_STAGES[@]}"; do
    file="${LATENCY_DIR}/${stage}_latency.txt"
    [[ -f "$file" ]] || continue
    read -r p50 p99 p999 pmax < <(extract_stats "$file")
    STATS["${stage}_p50"]="$p50"
    STATS["${stage}_p99"]="$p99"
    STATS["${stage}_p999"]="$p999"
    STATS["${stage}_pmax"]="$pmax"
    label="${STAGE_LABELS[$stage]:-$stage}"
    printf "%-28s %10s %10s %10s %10s\n" "$label" "$p50" "$p99" "$p999" "$pmax"
done

# ─── Print delta table (vs baseline) ─────────────────────────────────────────
if [[ ${#FOUND_STAGES[@]} -gt 1 && -n "${STATS[baseline_p50]+x}" ]]; then
    printf '\n%b  相对基线的变化:%b\n' "$C_BOLD" "$C_RESET"
    printf '%s\n' "$(printf '─%.0s' {1..70})"
    printf "%b%-28s %10s %10s %10s %10s%b\n" \
        "$C_BOLD" "阶段" "P50 Δ" "P99 Δ" "P99.9 Δ" "Max Δ" "$C_RESET"
    printf '%s\n' "$(printf '─%.0s' {1..70})"

    for stage in "${FOUND_STAGES[@]}"; do
        [[ "$stage" == "baseline" ]] && continue
        [[ -z "${STATS[${stage}_p50]+x}" ]] && continue
        label="${STAGE_LABELS[$stage]:-$stage}"
        printf "%-28s " "$label"
        for metric in p50 p99 p999 pmax; do
            delta_str=$(fmt_delta "${STATS[baseline_${metric}]}" "${STATS[${stage}_${metric}]}")
            printf " %b%-10s%b" "" "$delta_str" ""
        done
        printf "\n"
    done
fi

printf '%s\n\n' "$(printf '═%.0s' {1..70})"

# ─── IRQ interrupt load change ────────────────────────────────────────────────
IRQ_BEFORE="${LATENCY_DIR}/irq_before.txt"
IRQ_AFTER="${LATENCY_DIR}/irq_after.txt"

if [[ -f "$IRQ_BEFORE" && -f "$IRQ_AFTER" ]]; then
    printf "%b  IRQ 核中断负载变化 (sfc / efct 相关):%b\n" "$C_BOLD" "$C_RESET"
    diff "$IRQ_BEFORE" "$IRQ_AFTER" 2>/dev/null \
        | grep -E "sfc|efct|eth" | head -20 || echo "  (无变化或无匹配行)"
    printf "\n"
fi

# ─── CPU utilisation change ───────────────────────────────────────────────────
CPU_BEFORE="${LATENCY_DIR}/baseline_cpu.txt"
CPU_AFTER="${LATENCY_DIR}/after_full_tune_cpu.txt"

if [[ -f "$CPU_BEFORE" && -f "$CPU_AFTER" ]]; then
    printf "%b  CPU 利用率变化 (Average 行):%b\n" "$C_BOLD" "$C_RESET"
    printf "  Before: "
    grep -m1 Average "$CPU_BEFORE" || echo "(not found)"
    printf "  After : "
    grep -m1 Average "$CPU_AFTER"  || echo "(not found)"
    printf "\n"
fi

echo "提示: 运行 'x3522-tune check --iface <if> --app-cpus <cpus> --json > snapshot.json'"
echo "      后可用 'x3522-tune report --before baseline.json --after after.json' 生成配置对比"

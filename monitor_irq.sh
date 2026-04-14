#!/usr/bin/env bash
# monitor_irq.sh - Real-time IRQ interrupt load monitor for X3522 (EfCt)
# Usage: ./monitor_irq.sh --iface <ifname> --app-cpus <cpu-list> [--interval <sec>]
#
# Watches /proc/interrupts every N seconds and highlights:
#   - sfc/efct IRQs landing on application CPUs (bad)
#   - Total interrupt counts per CPU
# Press Ctrl-C to stop and print a summary.

set -euo pipefail

OPT_IFACE=""
OPT_APP_CPUS=""
OPT_INTERVAL=1
OPT_COUNT=0        # 0 = run forever

if [[ -t 1 ]]; then
    C_RED="\033[0;31m"; C_GREEN="\033[0;32m"; C_YELLOW="\033[0;33m"
    C_CYAN="\033[0;36m"; C_BOLD="\033[1m"; C_RESET="\033[0m"
else
    C_RED=""; C_GREEN=""; C_YELLOW=""; C_CYAN=""; C_BOLD=""; C_RESET=""
fi

usage() {
    cat <<EOF
Usage: $0 --iface <ifname> --app-cpus <cpu-list> [--interval <sec>] [--count <n>]

Options:
  --iface <ifname>       NIC interface name (e.g. eth0)
  --app-cpus <list>      Application CPU list (e.g. "2,3" or "2-5")
  --interval <sec>       Polling interval in seconds (default: 1)
  --count <n>            Number of samples before exit (default: 0=forever)
  -h, --help             Show this help
EOF
}

expand_cpulist() {
    local cpulist=$1
    local result=()
    IFS=',' read -ra parts <<< "$cpulist"
    for part in "${parts[@]}"; do
        if [[ "$part" =~ ^([0-9]+)-([0-9]+)$ ]]; then
            for ((c=${BASH_REMATCH[1]}; c<=${BASH_REMATCH[2]}; c++)); do
                result+=("$c")
            done
        elif [[ "$part" =~ ^[0-9]+$ ]]; then
            result+=("$part")
        fi
    done
    echo "${result[*]}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iface)    OPT_IFACE="$2";    shift 2 ;;
        --app-cpus) OPT_APP_CPUS="$2"; shift 2 ;;
        --interval) OPT_INTERVAL="$2"; shift 2 ;;
        --count)    OPT_COUNT="$2";    shift 2 ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -n "$OPT_IFACE"    ]] || { echo "ERROR: --iface required" >&2; exit 1; }
[[ -n "$OPT_APP_CPUS" ]] || { echo "ERROR: --app-cpus required" >&2; exit 1; }

APP_CPUS_EXPANDED=$(expand_cpulist "$OPT_APP_CPUS")
APP_CPU_ARRAY=( $APP_CPUS_EXPANDED )

# Get number of online CPUs to determine header column positions
NUM_CPUS=$(nproc 2>/dev/null || grep -c '^processor' /proc/cpuinfo)

# Read /proc/interrupts and extract column index for each CPU
get_cpu_col_indices() {
    # Header line: "           CPU0       CPU1       CPU2 ..."
    # Returns associative array CPU_COL[cpu_num] = 1-based field index
    local header
    header=$(head -1 /proc/interrupts)
    local idx=2  # field 1 is IRQ name, fields 2+ are CPU counts
    for token in $header; do
        if [[ "$token" =~ ^CPU([0-9]+)$ ]]; then
            echo "${BASH_REMATCH[1]}:${idx}"
        fi
        (( idx++ ))
    done
}

declare -A CPU_COL
while IFS=':' read -r cpu col; do
    CPU_COL["$cpu"]="$col"
done < <(get_cpu_col_indices)

# Snapshot: irq -> array of per-cpu counts
declare -A PREV_COUNTS

take_snapshot() {
    # Returns lines of: irq_name cpu0_count cpu1_count ...
    grep -E "sfc|efct|${OPT_IFACE}" /proc/interrupts 2>/dev/null || true
}

print_header() {
    printf "\n%b%s%b\n" "$C_BOLD" "$(date '+%Y-%m-%d %H:%M:%S') — IRQ 监控  接口:${OPT_IFACE}  应用核:${OPT_APP_CPUS}" "$C_RESET"
    # Build column header for app CPUs
    printf "%b%-8s%b" "$C_BOLD" "IRQ" "$C_RESET"
    for cpu in "${APP_CPU_ARRAY[@]}"; do
        printf " %b%-10s%b" "$C_BOLD" "CPU${cpu}/s" "$C_RESET"
    done
    printf " %b%-12s%b  %s\n" "$C_BOLD" "Total/s" "$C_RESET" "描述"
    printf '%s\n' "$(printf '─%.0s' {1..60})"
}

ITERATION=0
ALERTS=0

cleanup() {
    printf "\n%b监控结束: %d 次采样, %d 次警报 (应用核收到中断)%b\n" \
        "$C_BOLD" "$ITERATION" "$ALERTS" "$C_RESET"
    exit 0
}
trap cleanup INT TERM

while true; do
    (( ITERATION++ ))

    declare -A CUR_COUNTS
    while IFS= read -r line; do
        irq=$(echo "$line" | awk '{print $1}' | tr -d ':')
        desc=$(echo "$line" | awk '{for(i=NF;i>1;i--) if($i ~ /[a-zA-Z]/) {print $i; break}}')
        # Extract per-cpu counts using field indices
        local_counts=()
        total=0
        for cpu in $(seq 0 $(( NUM_CPUS - 1 ))); do
            col="${CPU_COL[$cpu]:-0}"
            if [[ $col -gt 0 ]]; then
                cnt=$(echo "$line" | awk -v c="$col" '{print $c+0}')
                local_counts+=("$cnt")
                total=$(( total + cnt ))
            fi
        done
        CUR_COUNTS["${irq}_total"]="$total"
        for i in "${!APP_CPU_ARRAY[@]}"; do
            cpu="${APP_CPU_ARRAY[$i]}"
            col="${CPU_COL[$cpu]:-0}"
            cnt=0
            [[ $col -gt 0 ]] && cnt=$(echo "$line" | awk -v c="$col" '{print $c+0}')
            CUR_COUNTS["${irq}_cpu${cpu}"]="$cnt"
        done
        CUR_COUNTS["${irq}_desc"]="$desc"
    done < <(take_snapshot)

    if [[ $ITERATION -gt 1 ]]; then
        print_header
        for irq_key in $(echo "${!CUR_COUNTS[@]}" | tr ' ' '\n' | grep '_total$' | sort); do
            irq="${irq_key%_total}"
            cur_total="${CUR_COUNTS[${irq}_total]:-0}"
            prev_total="${PREV_COUNTS[${irq}_total]:-0}"
            delta_total=$(( cur_total - prev_total ))

            app_cpu_deltas=()
            app_cpu_hit=0
            for cpu in "${APP_CPU_ARRAY[@]}"; do
                cur="${CUR_COUNTS[${irq}_cpu${cpu}]:-0}"
                prev="${PREV_COUNTS[${irq}_cpu${cpu}]:-0}"
                delta=$(( cur - prev ))
                app_cpu_deltas+=("$delta")
                [[ $delta -gt 0 ]] && (( app_cpu_hit++ )) || true
            done

            desc="${CUR_COUNTS[${irq}_desc]:-}"

            # Colour: red if any app CPU received interrupts
            if [[ $app_cpu_hit -gt 0 ]]; then
                (( ALERTS++ )) || true
                printf "%b" "$C_RED"
            fi

            printf "%-8s" "$irq"
            for delta in "${app_cpu_deltas[@]}"; do
                printf " %-10s" "$delta"
            done
            printf " %-12s  %s" "$delta_total" "$desc"

            if [[ $app_cpu_hit -gt 0 ]]; then
                printf "  ⚠️  应用核收到中断!"
                printf "%b" "$C_RESET"
            fi
            printf "\n"
        done
    fi

    # Save current as previous
    unset PREV_COUNTS
    declare -A PREV_COUNTS
    for k in "${!CUR_COUNTS[@]}"; do
        PREV_COUNTS["$k"]="${CUR_COUNTS[$k]}"
    done
    unset CUR_COUNTS

    [[ $OPT_COUNT -gt 0 && $ITERATION -ge $OPT_COUNT ]] && cleanup
    sleep "$OPT_INTERVAL"
done

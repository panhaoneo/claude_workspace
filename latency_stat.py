#!/usr/bin/env python3
"""
latency_stat.py - Latency statistics analysis tool for TAP timestamp data.

Usage:
    python3 latency_stat.py \
        --data-dir /path/to/data \
        --date 20241226 \
        --exchange sh \
        --mode all \
        --baseline "10.0.1.1_10.0.1.2" \
        --output report_20241226.md
"""
import argparse
import os
import sys
import tempfile

import file_scanner
import parser as _parser
import joiner
import stats as _stats
import reporter


# ---------------------------------------------------------------------------
# Single-route streaming helpers
# ---------------------------------------------------------------------------

def _collect_md_single(
    route_files: list,
    use_reservoir: bool,
    reservoir_size: int,
    outlier_ns: int,
) -> dict:
    """
    Stream all md files and collect per-route latency lists.
    Returns {route_id: stats_dict}.
    """
    route_stats = {}
    parse_errors_total = 0

    for rf in route_files:
        latencies = []
        sampler = _stats.ReservoirSampler(reservoir_size) if use_reservoir else None
        parse_errors = 0

        print(f"[INFO] Processing md single-route {rf.route_id}...", file=sys.stderr)

        with open(rf.path, 'r', buffering=1 << 20) as f:
            for line in f:
                parsed = _parser.parse_md_line(line)
                if parsed is None:
                    parse_errors += 1
                    continue
                tap_ts, ticker, exch_raw, exch_ps = parsed
                lat_ns = (tap_ts - exch_ps) // 1000
                if outlier_ns > 0 and abs(lat_ns) > outlier_ns:
                    continue
                if use_reservoir:
                    sampler.add(lat_ns)
                else:
                    latencies.append(lat_ns)

        parse_errors_total += parse_errors
        if parse_errors:
            print(
                f"[WARN] {rf.path}: {parse_errors} parse error(s) skipped",
                file=sys.stderr,
            )

        if use_reservoir:
            s = sampler.get_stats()
        else:
            s = _stats.calc_stats(latencies)

        if s:
            route_stats[rf.route_id] = s

    return route_stats


def _collect_tbt_single(
    route_files: list,
    use_reservoir: bool,
    reservoir_size: int,
    outlier_ns: int,
) -> dict:
    """
    Stream all tbt files and collect per-route, per-channel latency.
    Returns {route_id: {channel: stats_dict}}.
    """
    route_channel_stats = {}
    parse_errors_total = 0

    for rf in route_files:
        print(f"[INFO] Processing tbt single-route {rf.route_id}...", file=sys.stderr)
        # Accumulate per channel
        if use_reservoir:
            channel_data: dict = {}  # channel -> ReservoirSampler
        else:
            channel_data: dict = {}  # channel -> list[int]

        parse_errors = 0

        with open(rf.path, 'r', buffering=1 << 20) as f:
            for line in f:
                parsed = _parser.parse_tbt_line(line)
                if parsed is None:
                    parse_errors += 1
                    continue
                tap_ts, channel, seq, ticker, exch_ps = parsed
                lat_ns = (tap_ts - exch_ps) // 1000
                if outlier_ns > 0 and abs(lat_ns) > outlier_ns:
                    continue

                if channel not in channel_data:
                    channel_data[channel] = (
                        _stats.ReservoirSampler(reservoir_size)
                        if use_reservoir else []
                    )
                if use_reservoir:
                    channel_data[channel].add(lat_ns)
                else:
                    channel_data[channel].append(lat_ns)

        parse_errors_total += parse_errors
        if parse_errors:
            print(
                f"[WARN] {rf.path}: {parse_errors} parse error(s) skipped",
                file=sys.stderr,
            )

        ch_stats = {}
        for ch, data in channel_data.items():
            if use_reservoir:
                s = data.get_stats()
            else:
                s = _stats.calc_stats(data)
            if s:
                ch_stats[ch] = s

        if ch_stats:
            route_channel_stats[rf.route_id] = ch_stats

    return route_channel_stats


# ---------------------------------------------------------------------------
# Compare helpers (diff_ns from joiner)
# ---------------------------------------------------------------------------

def _collect_md_compare(
    route_files: list,
    baseline_id: str,
    tmp_dir: str,
    outlier_ns: int,
) -> dict:
    """
    Returns {route_id: stats_dict} for all non-baseline routes.
    """
    diff_result = joiner.build_diff_ns(
        route_files, baseline_id, 'md', tmp_dir, outlier_ns
    )
    route_stats = {}
    for rid, data in diff_result.items():
        # For md, diffs keyed by route_id itself (single group)
        all_diffs = []
        for group_diffs in data['diffs'].values():
            all_diffs.extend(group_diffs)
        s = _stats.calc_stats(all_diffs)
        if s:
            route_stats[rid] = s
    return route_stats


def _collect_tbt_compare(
    route_files: list,
    baseline_id: str,
    tmp_dir: str,
    outlier_ns: int,
) -> dict:
    """
    Returns {route_id: {channel: stats_dict}} for all non-baseline routes.
    """
    diff_result = joiner.build_diff_ns(
        route_files, baseline_id, 'tbt', tmp_dir, outlier_ns
    )
    route_channel_stats = {}
    for rid, data in diff_result.items():
        ch_stats = {}
        for channel, diffs in data['diffs'].items():
            s = _stats.calc_stats(diffs)
            if s:
                ch_stats[channel] = s
        if ch_stats:
            route_channel_stats[rid] = ch_stats
    return route_channel_stats


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args():
    p = argparse.ArgumentParser(
        description='Latency statistics analysis for TAP timestamp files.'
    )
    p.add_argument('--data-dir',  required=True, help='Data file directory')
    p.add_argument('--date',      required=True, help='Date YYYYMMDD')
    p.add_argument('--exchange',  required=True, help='sh / sz / all')
    p.add_argument('--baseline',  required=True, help='Baseline route src_ip_dst_ip')
    p.add_argument('--mode',      default='all',       help='md / tbt / all')
    p.add_argument('--output',    default=None,         help='Output file (default: stdout)')
    p.add_argument('--reservoir', action='store_true',  help='Enable Reservoir Sampling')
    p.add_argument('--reservoir-size', type=int, default=1_000_000,
                   help='Reservoir capacity (default: 1000000)')
    p.add_argument('--tmp-dir',   default='/tmp',       help='Temp dir for external sort')
    p.add_argument('--outlier-ns', type=int, default=0,
                   help='Filter diffs/latencies > N ns (0 = disabled)')
    return p.parse_args()


def main():
    args = parse_args()

    # Scan files
    routes = file_scanner.scan_files(
        data_dir=args.data_dir,
        date=args.date,
        exchange=args.exchange,
        mode=args.mode,
    )

    md_files  = routes.get('md', [])
    tbt_files = routes.get('tbt', [])

    if not md_files and not tbt_files:
        print("[ERROR] No matching files found.", file=sys.stderr)
        sys.exit(1)

    print(
        f"[INFO] Found {len(md_files)} md file(s), {len(tbt_files)} tbt file(s).",
        file=sys.stderr,
    )

    sections = []

    # ------------------------------------------------------------------
    # Section 1: md compare
    # ------------------------------------------------------------------
    if args.mode in ('md', 'all') and md_files:
        has_baseline = any(rf.route_id == args.baseline for rf in md_files)
        if not has_baseline:
            print(
                f"[WARN] Baseline {args.baseline!r} not found in md files.",
                file=sys.stderr,
            )
            md_compare_stats = {}
        elif len(md_files) == 1:
            md_compare_stats = {args.baseline: None}
        else:
            print("[INFO] Computing md compare...", file=sys.stderr)
            md_compare_stats = _collect_md_compare(
                md_files, args.baseline, args.tmp_dir, args.outlier_ns
            )
            md_compare_stats[args.baseline] = None  # baseline placeholder

        sections.append(reporter.section_md_compare(
            md_compare_stats, args.baseline, args.exchange
        ))

    # ------------------------------------------------------------------
    # Section 2: tbt compare
    # ------------------------------------------------------------------
    if args.mode in ('tbt', 'all') and tbt_files:
        has_baseline = any(rf.route_id == args.baseline for rf in tbt_files)
        if not has_baseline:
            print(
                f"[WARN] Baseline {args.baseline!r} not found in tbt files.",
                file=sys.stderr,
            )
            tbt_compare_stats = {}
        elif len(tbt_files) == 1:
            tbt_compare_stats = {}
        else:
            print("[INFO] Computing tbt compare...", file=sys.stderr)
            tbt_compare_stats = _collect_tbt_compare(
                tbt_files, args.baseline, args.tmp_dir, args.outlier_ns
            )

        sections.append(reporter.section_tbt_compare(
            tbt_compare_stats, args.baseline, args.exchange
        ))

    # ------------------------------------------------------------------
    # Section 3: md single
    # ------------------------------------------------------------------
    if args.mode in ('md', 'all') and md_files:
        print("[INFO] Computing md single-route latency...", file=sys.stderr)
        md_single_stats = _collect_md_single(
            md_files, args.reservoir, args.reservoir_size, args.outlier_ns
        )
        sections.append(reporter.section_md_single(md_single_stats))

    # ------------------------------------------------------------------
    # Section 4: tbt single
    # ------------------------------------------------------------------
    if args.mode in ('tbt', 'all') and tbt_files:
        print("[INFO] Computing tbt single-route latency...", file=sys.stderr)
        tbt_single_stats = _collect_tbt_single(
            tbt_files, args.reservoir, args.reservoir_size, args.outlier_ns
        )
        sections.append(reporter.section_tbt_single(tbt_single_stats))

    report = reporter.build_report(sections)

    if args.output:
        with open(args.output, 'w') as f:
            f.write(report)
        print(f"[INFO] Report written to {args.output}", file=sys.stderr)
    else:
        print(report)


if __name__ == '__main__':
    main()

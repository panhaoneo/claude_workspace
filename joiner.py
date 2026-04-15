"""
joiner.py - Multi-way merge join for diff_ns calculation.

Implements the external-sort + heapq.merge approach described in the spec.
"""
import heapq
import os
import sys
from typing import Iterator

import parser as _parser
import sorter as _sorter
from file_scanner import RouteFile


def _md_key_records(rf: RouteFile) -> Iterator[tuple[str, int]]:
    """
    Yield (key_str, tap_ts_ps) for each valid md line.
    key = "ticker\texchange_ts_raw"
    """
    parse_errors = 0
    with open(rf.path, 'r', buffering=1 << 20) as f:
        for line in f:
            parsed = _parser.parse_md_line(line)
            if parsed is None:
                parse_errors += 1
                continue
            tap_ts, ticker, exch_raw, _ = parsed
            key = f"{ticker}\x01{exch_raw}"
            yield key, tap_ts
    if parse_errors:
        print(
            f"[WARN] {rf.path}: {parse_errors} parse error(s) skipped",
            file=sys.stderr,
        )


def _tbt_key_records(rf: RouteFile) -> Iterator[tuple[str, int]]:
    """
    Yield (key_str, tap_ts_ps) for each valid tbt line.
    key = "channel\x01seq"
    """
    parse_errors = 0
    with open(rf.path, 'r', buffering=1 << 20) as f:
        for line in f:
            parsed = _parser.parse_tbt_line(line)
            if parsed is None:
                parse_errors += 1
                continue
            tap_ts, channel, seq, _, _ = parsed
            key = f"{channel}\x01{seq}"
            yield key, tap_ts
    if parse_errors:
        print(
            f"[WARN] {rf.path}: {parse_errors} parse error(s) skipped",
            file=sys.stderr,
        )


def build_diff_ns(
    route_files: list[RouteFile],
    baseline_id: str,
    file_type: str,     # 'md' or 'tbt'
    tmp_dir: str,
    outlier_ns: int = 0,
) -> dict[str, dict]:
    """
    Compute diff_ns for all non-baseline routes vs baseline.

    Returns:
        {
            route_id: {
                'diffs': {group_key: [diff_ns, ...]},  # group_key = channel for tbt, route_id for md
            }
        }

    For md: group_key is the route_id itself (single group).
    For tbt: group_key is channel string.
    """
    if not route_files:
        return {}

    sorted_paths: dict[str, str] = {}
    unsorted_paths: dict[str, str] = {}

    try:
        # Step 1: write sort-input files
        for rf in route_files:
            if file_type == 'md':
                records = _md_key_records(rf)
            else:
                records = _tbt_key_records(rf)

            print(
                f"[INFO] Writing sort input for {rf.route_id} ({file_type})...",
                file=sys.stderr,
            )
            path = _sorter.write_sort_input(rf.route_id, records, tmp_dir)
            unsorted_paths[rf.route_id] = path

        # Step 2: external sort
        for route_id, upath in unsorted_paths.items():
            print(
                f"[INFO] Sorting {route_id}...",
                file=sys.stderr,
            )
            sorted_paths[route_id] = _sorter.sort_file(upath, tmp_dir)
            os.unlink(upath)

        # Step 3: multi-way merge join
        result: dict[str, dict] = {
            rf.route_id: {'diffs': {}}
            for rf in route_files
            if rf.route_id != baseline_id
        }

        # Build sorted iterators tagged with route_id
        # Each item in heap: (key, tap_ts, route_id)
        def tagged_iter(route_id: str):
            for key, tap_ts in _sorter.iter_sorted_file(sorted_paths[route_id]):
                yield key, tap_ts, route_id

        heap_iters = [tagged_iter(rf.route_id) for rf in route_files]
        merged = heapq.merge(*heap_iters, key=lambda x: x[0])

        current_key = None
        group: dict[str, int] = {}  # route_id -> min tap_ts for current key

        def _flush(key: str, group: dict[str, int]):
            if baseline_id not in group:
                return
            base_ts = group[baseline_id]
            for route_id, ts in group.items():
                if route_id == baseline_id:
                    continue
                diff_ns = (ts - base_ts) // 1000

                if outlier_ns > 0 and abs(diff_ns) > outlier_ns:
                    continue

                # Determine group_key
                if file_type == 'tbt':
                    # key format: "channel\x01seq"
                    group_key = key.split('\x01')[0]
                else:
                    group_key = route_id  # single group for md

                diffs = result[route_id]['diffs']
                if group_key not in diffs:
                    diffs[group_key] = []
                diffs[group_key].append(diff_ns)

        for key, tap_ts, route_id in merged:
            if key != current_key:
                if current_key is not None:
                    _flush(current_key, group)
                current_key = key
                group = {route_id: tap_ts}
            else:
                # Keep minimum tap_ts per route for this key
                if route_id not in group or tap_ts < group[route_id]:
                    group[route_id] = tap_ts

        if current_key is not None:
            _flush(current_key, group)

    finally:
        # Clean up temp files
        for p in unsorted_paths.values():
            if os.path.exists(p):
                os.unlink(p)
        for p in sorted_paths.values():
            if os.path.exists(p):
                os.unlink(p)

    return result

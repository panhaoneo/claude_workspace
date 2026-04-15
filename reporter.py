"""
reporter.py - Markdown report generation.

Builds the four sections described in the spec with right-aligned number columns
and auto-sizing column widths.
"""
from typing import Optional

_STAT_COLS = [
    ('cnt',    'cnt'),
    ('min',    'min(ns)'),
    ('max',    'max(ns)'),
    ('avg',    'avg(ns)'),
    ('stddev', 'stddev'),
    ('p25',    'p25(ns)'),
    ('p50',    'p50(ns)'),
    ('p75',    'p75(ns)'),
    ('p90',    'p90(ns)'),
    ('p95',    'p95(ns)'),
    ('p99',    'p99(ns)'),
]


def _fmt_val(v) -> str:
    if v is None or v == '—':
        return '—'
    return str(v)


def _make_table(
    first_col_header: str,
    first_col_align: str,   # ':---' or '---:'
    rows: list[tuple],      # (first_col_value, stats_dict_or_None)
    baseline_label: str = None,
) -> list[str]:
    """
    Build a Markdown table.

    rows is a list of (label, stats) where stats is a dict from calc_stats()
    or None for the baseline row (which shows '—' in all stat columns).
    baseline_label: if set, appended as "(基准)" to that row's label.
    """
    stat_keys = [k for k, _ in _STAT_COLS]
    stat_headers = [h for _, h in _STAT_COLS]

    # Compute column widths
    col_widths = [len(first_col_header)] + [len(h) for h in stat_headers]

    data_rows: list[list[str]] = []
    for label, stats in rows:
        display_label = label
        if baseline_label and label == baseline_label:
            display_label = f"{label}（基准）"
        col_widths[0] = max(col_widths[0], len(display_label))

        if stats is None:
            cells = ['—'] * len(stat_keys)
        else:
            cells = [_fmt_val(stats.get(k)) for k in stat_keys]

        for i, cell in enumerate(cells):
            col_widths[i + 1] = max(col_widths[i + 1], len(cell))

        data_rows.append([display_label] + cells)

    # Header row
    header_cells = [first_col_header] + stat_headers
    # Pad header cells
    padded_header = [
        header_cells[0].ljust(col_widths[0]),
    ] + [
        header_cells[i + 1].rjust(col_widths[i + 1])
        for i in range(len(stat_headers))
    ]
    header_line = '| ' + ' | '.join(padded_header) + ' |'

    # Separator row
    sep_cells = [first_col_align.replace('-' * 3, '-' * max(col_widths[0], 3))]
    for i in range(len(stat_headers)):
        sep_cells.append('---:'.replace('---', '-' * max(col_widths[i + 1] - 1, 3)))
    sep_line = '|' + '|'.join(sep_cells) + '|'

    lines = [header_line, sep_line]
    for row in data_rows:
        padded = [
            row[0].ljust(col_widths[0]),
        ] + [
            row[i + 1].rjust(col_widths[i + 1])
            for i in range(len(stat_headers))
        ]
        lines.append('| ' + ' | '.join(padded) + ' |')

    return lines


def section_md_compare(
    route_stats: dict,      # {route_id: stats_dict_or_None}
    baseline_id: str,
    exchange: str,
) -> list[str]:
    """
    Section 1: 快照多路对比
    route_stats maps route_id -> stats dict (or None for baseline).
    """
    lines = [f"## 快照对比（按交易所时间对齐，基准路：{baseline_id}）", ""]

    if not route_stats:
        lines += ["（无数据）", ""]
        return lines

    # baseline first, then sorted others
    ordered = []
    if baseline_id in route_stats:
        ordered.append((baseline_id, None))
    for rid, stats in sorted(route_stats.items()):
        if rid != baseline_id:
            ordered.append((rid, stats))

    if len(ordered) <= 1:
        lines += ["（仅一路数据，跳过对比）", ""]
        return lines

    table = _make_table(
        first_col_header='路次',
        first_col_align=':---',
        rows=ordered,
        baseline_label=baseline_id,
    )
    lines += table + [""]
    return lines


def section_tbt_compare(
    # {route_id: {channel: stats_dict}}
    route_channel_stats: dict,
    baseline_id: str,
    exchange: str,
) -> list[str]:
    """
    Section 2: 逐笔多路对比（按通道号）
    """
    lines = [f"## 逐笔对比（按通道号对齐，基准路：{baseline_id}）", ""]

    non_baseline = {
        rid: ch_stats
        for rid, ch_stats in route_channel_stats.items()
        if rid != baseline_id
    }

    if not non_baseline:
        lines += ["（仅一路数据，跳过对比）", ""]
        return lines

    for rid in sorted(non_baseline.keys()):
        ch_stats = non_baseline[rid]
        lines.append(f"### {rid} vs 基准路 {baseline_id}")
        lines.append("")

        if not ch_stats:
            lines += ["（无数据）", ""]
            continue

        rows = [
            (ch, stats)
            for ch, stats in sorted(ch_stats.items(), key=lambda x: int(x[0]) if x[0].isdigit() else x[0])
        ]
        table = _make_table(
            first_col_header='channel',
            first_col_align='---:',
            rows=rows,
        )
        lines += table + [""]

    return lines


def section_md_single(
    route_stats: dict,      # {route_id: stats_dict}
) -> list[str]:
    """
    Section 3: 快照单路绝对延时
    """
    lines = ["## 快照单路延时（TAP − 交易所时间）", ""]

    if not route_stats:
        lines += ["（无数据）", ""]
        return lines

    rows = [
        (rid, stats)
        for rid, stats in sorted(route_stats.items())
    ]
    table = _make_table(
        first_col_header='路次',
        first_col_align=':---',
        rows=rows,
    )
    lines += table + [""]
    return lines


def section_tbt_single(
    # {route_id: {channel: stats_dict}}
    route_channel_stats: dict,
) -> list[str]:
    """
    Section 4: 逐笔单路绝对延时（按通道分组）
    """
    lines = ["## 逐笔单路延时（TAP − 交易所时间）", ""]

    if not route_channel_stats:
        lines += ["（无数据）", ""]
        return lines

    for rid in sorted(route_channel_stats.keys()):
        ch_stats = route_channel_stats[rid]
        lines.append(f"### {rid}")
        lines.append("")

        if not ch_stats:
            lines += ["（无数据）", ""]
            continue

        rows = [
            (ch, stats)
            for ch, stats in sorted(ch_stats.items(), key=lambda x: int(x[0]) if x[0].isdigit() else x[0])
        ]
        table = _make_table(
            first_col_header='channel',
            first_col_align='---:',
            rows=rows,
        )
        lines += table + [""]

    return lines


def build_report(sections: list[list[str]]) -> str:
    """Join all sections into a single Markdown string."""
    all_lines = []
    for section in sections:
        all_lines += section
    return '\n'.join(all_lines)

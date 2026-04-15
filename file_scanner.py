"""
file_scanner.py - Scan data directory and identify route files.
"""
import os
import re
from dataclasses import dataclass, field
from typing import Optional

FILE_PATTERN = re.compile(
    r'^(md|tbt)_(sh|sz)_(.+?)_(.+?)_(\d{8})_tap_tmp\.txt$'
)


@dataclass
class RouteFile:
    file_type: str      # 'md' or 'tbt'
    exchange: str       # 'sh' or 'sz'
    src_ip: str
    dst_ip: str
    date: str
    path: str

    @property
    def route_id(self) -> str:
        return f"{self.src_ip}_{self.dst_ip}"


def scan_files(
    data_dir: str,
    date: str,
    exchange: str,
    mode: str = 'all',
) -> dict[str, list[RouteFile]]:
    """
    Scan data_dir and return a dict mapping file_type -> list[RouteFile].

    Args:
        data_dir:  Directory to scan.
        date:      Date string YYYYMMDD.
        exchange:  'sh', 'sz', or 'all'.
        mode:      'md', 'tbt', or 'all'.

    Returns:
        {'md': [...], 'tbt': [...]}
    """
    result: dict[str, list[RouteFile]] = {'md': [], 'tbt': []}

    try:
        entries = os.listdir(data_dir)
    except OSError as e:
        raise RuntimeError(f"Cannot list directory {data_dir!r}: {e}") from e

    for name in sorted(entries):
        m = FILE_PATTERN.match(name)
        if not m:
            continue
        ftype, exch, src_ip, dst_ip, fdate = m.groups()

        if fdate != date:
            continue
        if exchange != 'all' and exch != exchange:
            continue
        if mode != 'all' and ftype != mode:
            continue

        path = os.path.join(data_dir, name)
        rf = RouteFile(
            file_type=ftype,
            exchange=exch,
            src_ip=src_ip,
            dst_ip=dst_ip,
            date=fdate,
            path=path,
        )
        result[ftype].append(rf)

    return result

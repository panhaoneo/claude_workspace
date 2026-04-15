"""
sorter.py - External sort wrapper using system sort command.

Writes intermediate key files per route, sorts them, returns sorted file paths.
"""
import os
import subprocess
import tempfile
import sys
from typing import Iterator

# Separator used inside temporary sort files (not comma, to avoid conflicts
# with IP addresses).  We use TAB.
_SEP = '\t'


def write_sort_input(
    route_id: str,
    records: Iterator[tuple],   # (sort_key_str, tap_ts_ps_int)
    tmp_dir: str,
) -> str:
    """
    Write (sort_key, tap_ts_ps) pairs to a temp file.
    Returns the temp file path (unsorted).
    """
    fd, path = tempfile.mkstemp(
        prefix=f'lat_sort_{route_id.replace(".", "_")}_',
        suffix='.tmp',
        dir=tmp_dir,
    )
    try:
        with os.fdopen(fd, 'w', buffering=1 << 20) as f:
            for key, tap_ts in records:
                f.write(f"{key}{_SEP}{tap_ts}\n")
    except Exception:
        os.unlink(path)
        raise
    return path


def sort_file(input_path: str, tmp_dir: str) -> str:
    """
    Sort input_path by the first field (key).
    Returns path to sorted temp file.
    """
    fd, sorted_path = tempfile.mkstemp(
        prefix='lat_sorted_',
        suffix='.tmp',
        dir=tmp_dir,
    )
    os.close(fd)
    try:
        subprocess.run(
            [
                'sort',
                '-t', _SEP,
                '-k1,1',        # sort by key field
                '-o', sorted_path,
                input_path,
            ],
            check=True,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as e:
        os.unlink(sorted_path)
        raise RuntimeError(
            f"sort failed for {input_path}: {e.stderr.decode()}"
        ) from e
    return sorted_path


def iter_sorted_file(path: str) -> Iterator[tuple[str, int]]:
    """
    Iterate over a sorted temp file, yielding (key_str, tap_ts_ps).
    """
    with open(path, 'r', buffering=1 << 20) as f:
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue
            sep_idx = line.index(_SEP)
            key = line[:sep_idx]
            tap_ts = int(line[sep_idx + 1:])
            yield key, tap_ts

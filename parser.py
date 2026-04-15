"""
parser.py - Parse md/tbt lines into structured records.
"""
from typing import Optional, Tuple

# exchange_ts is 17-char string: YYYYMMDDHHMMSSmmm
_PS_PER_HOUR = 3_600_000_000_000_000   # 3600 * 1e12
_PS_PER_MIN  =    60_000_000_000_000   # 60   * 1e12
_PS_PER_SEC  =     1_000_000_000_000   # 1    * 1e12
_PS_PER_MS   =         1_000_000_000   # 1e9


def parse_exchange_ts(raw: str) -> Optional[int]:
    """
    Parse 17-char exchange timestamp to picoseconds (intraday).

    raw format: YYYYMMDDHHMMSSmmm
    Returns None if format is invalid.
    """
    if len(raw) != 17 or not raw.isdigit():
        return None
    # We only use intraday portion (H:M:S:ms), date part ignored.
    H  = int(raw[8:10])
    M  = int(raw[10:12])
    S  = int(raw[12:14])
    ms = int(raw[14:17])
    return (H * _PS_PER_HOUR
            + M * _PS_PER_MIN
            + S * _PS_PER_SEC
            + ms * _PS_PER_MS)


def parse_md_line(line: str) -> Optional[Tuple[int, str, str, int]]:
    """
    Parse a snapshot (md) line.

    Format: tap_ts_ps,ticker,exchange_ts[,...]

    Returns: (tap_ts_ps, ticker, exchange_ts_raw, exchange_ts_ps)
    or None on parse error.
    """
    parts = line.rstrip('\n').split(',')
    if len(parts) < 3:
        return None
    try:
        tap_ts = int(parts[0])
    except ValueError:
        return None
    ticker = parts[1].strip()
    exch_raw = parts[2].strip()
    exch_ps = parse_exchange_ts(exch_raw)
    if exch_ps is None:
        return None
    return (tap_ts, ticker, exch_raw, exch_ps)


def parse_tbt_line(line: str) -> Optional[Tuple[int, str, str, str, int]]:
    """
    Parse a tick-by-tick (tbt) line.

    Format: tap_ts_ps,channel,seq,ticker,exchange_ts[,...]

    Returns: (tap_ts_ps, channel, seq, ticker, exchange_ts_ps)
    or None on parse error.
    """
    parts = line.rstrip('\n').split(',')
    if len(parts) < 5:
        return None
    try:
        tap_ts = int(parts[0])
    except ValueError:
        return None
    channel = parts[1].strip()
    seq     = parts[2].strip()
    ticker  = parts[3].strip()
    exch_raw = parts[4].strip()
    exch_ps = parse_exchange_ts(exch_raw)
    if exch_ps is None:
        return None
    return (tap_ts, channel, seq, ticker, exch_ps)

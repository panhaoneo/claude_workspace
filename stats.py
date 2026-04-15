"""
stats.py - Statistical calculations for latency values.

Supports numpy (preferred) and pure-Python fallback.
Also supports Reservoir Sampling for memory-constrained accumulation.
"""
import math
import random
from typing import Optional

try:
    import numpy as np
    _HAS_NUMPY = True
except ImportError:
    _HAS_NUMPY = False


def calc_stats(values: list) -> dict:
    """
    Calculate statistics over a list of integer latency values (ns).

    Returns dict with keys:
        cnt, min, max, avg, stddev, p25, p50, p75, p90, p95, p99
    All values are integers.
    """
    n = len(values)
    if n == 0:
        return None

    if _HAS_NUMPY:
        arr = np.array(values, dtype=np.int64)
        pcts = np.percentile(arr, [25, 50, 75, 90, 95, 99])
        return {
            'cnt':    int(n),
            'min':    int(arr.min()),
            'max':    int(arr.max()),
            'avg':    int(round(float(arr.mean()))),
            'stddev': int(round(float(arr.std()))),
            'p25':    int(round(float(pcts[0]))),
            'p50':    int(round(float(pcts[1]))),
            'p75':    int(round(float(pcts[2]))),
            'p90':    int(round(float(pcts[3]))),
            'p95':    int(round(float(pcts[4]))),
            'p99':    int(round(float(pcts[5]))),
        }
    else:
        # Pure Python fallback
        sorted_vals = sorted(values)
        total = sum(sorted_vals)
        avg = total / n
        variance = sum((v - avg) ** 2 for v in sorted_vals) / n
        stddev = math.sqrt(variance)

        def percentile(p):
            idx = (p / 100) * (n - 1)
            lo = int(idx)
            hi = lo + 1
            if hi >= n:
                return sorted_vals[-1]
            frac = idx - lo
            return sorted_vals[lo] + frac * (sorted_vals[hi] - sorted_vals[lo])

        return {
            'cnt':    n,
            'min':    sorted_vals[0],
            'max':    sorted_vals[-1],
            'avg':    int(round(avg)),
            'stddev': int(round(stddev)),
            'p25':    int(round(percentile(25))),
            'p50':    int(round(percentile(50))),
            'p75':    int(round(percentile(75))),
            'p90':    int(round(percentile(90))),
            'p95':    int(round(percentile(95))),
            'p99':    int(round(percentile(99))),
        }


class ReservoirSampler:
    """
    Reservoir sampling for memory-limited latency accumulation.

    Maintains a fixed-size random sample of the input stream.
    """

    def __init__(self, capacity: int = 1_000_000):
        self.capacity = capacity
        self._reservoir: list = []
        self._count = 0

    def add(self, value: int) -> None:
        self._count += 1
        if len(self._reservoir) < self.capacity:
            self._reservoir.append(value)
        else:
            j = random.randint(0, self._count - 1)
            if j < self.capacity:
                self._reservoir[j] = value

    @property
    def count(self) -> int:
        return self._count

    def get_stats(self) -> Optional[dict]:
        if not self._reservoir:
            return None
        stats = calc_stats(self._reservoir)
        if stats:
            # Override cnt with actual stream count, not sample size
            stats['cnt'] = self._count
        return stats

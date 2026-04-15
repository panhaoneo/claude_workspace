#!/usr/bin/env python3
"""
Unit and integration tests for the latency analysis tool.
"""
import os
import sys
import tempfile
import unittest

# Make sure local modules are importable
sys.path.insert(0, os.path.dirname(__file__))

import parser as _parser
import stats as _stats
from file_scanner import scan_files, RouteFile
import reporter


# ---------------------------------------------------------------------------
# parser tests
# ---------------------------------------------------------------------------

class TestParseExchangeTs(unittest.TestCase):
    def test_valid(self):
        ps = _parser.parse_exchange_ts("20241226153500000")
        # H=15, M=35, S=0, ms=0
        expected = (15 * 3600 + 35 * 60) * 1_000_000_000_000
        self.assertEqual(ps, expected)

    def test_with_ms(self):
        ps = _parser.parse_exchange_ts("20241226153500123")
        expected = (15 * 3600 + 35 * 60) * 1_000_000_000_000 + 123 * 1_000_000_000
        self.assertEqual(ps, expected)

    def test_invalid_length(self):
        self.assertIsNone(_parser.parse_exchange_ts("2024122615350000"))

    def test_invalid_chars(self):
        self.assertIsNone(_parser.parse_exchange_ts("2024122615350000X"))


class TestParseMdLine(unittest.TestCase):
    def test_valid(self):
        line = "82313384932802080,563500,20241226153500000\n"
        result = _parser.parse_md_line(line)
        self.assertIsNotNone(result)
        tap_ts, ticker, exch_raw, exch_ps = result
        self.assertEqual(tap_ts, 82313384932802080)
        self.assertEqual(ticker, "563500")
        self.assertEqual(exch_raw, "20241226153500000")
        # lat_ns should be positive and reasonable
        lat_ns = (tap_ts - exch_ps) // 1000
        self.assertGreater(lat_ns, 0)

    def test_extra_fields(self):
        line = "82313384932802080,563500,20241226153500000,extra1,extra2\n"
        result = _parser.parse_md_line(line)
        self.assertIsNotNone(result)

    def test_too_few_fields(self):
        line = "82313384932802080,563500\n"
        self.assertIsNone(_parser.parse_md_line(line))

    def test_bad_tap_ts(self):
        line = "notanumber,563500,20241226153500000\n"
        self.assertIsNone(_parser.parse_md_line(line))


class TestParseTbtLine(unittest.TestCase):
    def test_valid(self):
        line = "82313384932802080,2021,34464517,204001,20241226153500000\n"
        result = _parser.parse_tbt_line(line)
        self.assertIsNotNone(result)
        tap_ts, channel, seq, ticker, exch_ps = result
        self.assertEqual(tap_ts, 82313384932802080)
        self.assertEqual(channel, "2021")
        self.assertEqual(seq, "34464517")
        self.assertEqual(ticker, "204001")

    def test_too_few_fields(self):
        line = "82313384932802080,2021,34464517,204001\n"
        self.assertIsNone(_parser.parse_tbt_line(line))


# ---------------------------------------------------------------------------
# stats tests
# ---------------------------------------------------------------------------

class TestCalcStats(unittest.TestCase):
    def test_basic(self):
        values = [10, 20, 30, 40, 50]
        s = _stats.calc_stats(values)
        self.assertEqual(s['cnt'], 5)
        self.assertEqual(s['min'], 10)
        self.assertEqual(s['max'], 50)
        self.assertEqual(s['avg'], 30)

    def test_empty(self):
        self.assertIsNone(_stats.calc_stats([]))

    def test_single(self):
        s = _stats.calc_stats([42])
        self.assertEqual(s['cnt'], 1)
        self.assertEqual(s['min'], 42)
        self.assertEqual(s['max'], 42)


class TestReservoirSampler(unittest.TestCase):
    def test_below_capacity(self):
        rs = _stats.ReservoirSampler(100)
        for i in range(50):
            rs.add(i)
        self.assertEqual(rs.count, 50)
        s = rs.get_stats()
        self.assertEqual(s['cnt'], 50)

    def test_above_capacity_count(self):
        rs = _stats.ReservoirSampler(10)
        for i in range(1000):
            rs.add(i)
        self.assertEqual(rs.count, 1000)
        s = rs.get_stats()
        # cnt should reflect actual stream size
        self.assertEqual(s['cnt'], 1000)
        # reservoir holds at most 10 samples
        self.assertLessEqual(len(rs._reservoir), 10)


# ---------------------------------------------------------------------------
# file_scanner tests
# ---------------------------------------------------------------------------

class TestFileScanner(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        # Create dummy files
        self.files = [
            "md_sh_10.0.1.1_10.0.1.2_20241226_tap_tmp.txt",
            "md_sh_10.0.1.3_10.0.1.4_20241226_tap_tmp.txt",
            "tbt_sh_10.0.1.1_10.0.1.2_20241226_tap_tmp.txt",
            "md_sz_10.0.1.5_10.0.1.6_20241226_tap_tmp.txt",
            "md_sh_10.0.1.1_10.0.1.2_20241225_tap_tmp.txt",  # different date
            "other_file.txt",
        ]
        for fn in self.files:
            open(os.path.join(self.tmpdir, fn), 'w').close()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir)

    def test_scan_date_filter(self):
        result = scan_files(self.tmpdir, '20241226', 'all', 'all')
        all_files = result['md'] + result['tbt']
        dates = {rf.date for rf in all_files}
        self.assertEqual(dates, {'20241226'})

    def test_scan_exchange_filter(self):
        result = scan_files(self.tmpdir, '20241226', 'sh', 'all')
        for rf in result['md'] + result['tbt']:
            self.assertEqual(rf.exchange, 'sh')

    def test_scan_mode_md(self):
        result = scan_files(self.tmpdir, '20241226', 'all', 'md')
        self.assertEqual(len(result['tbt']), 0)
        self.assertGreater(len(result['md']), 0)

    def test_route_id(self):
        result = scan_files(self.tmpdir, '20241226', 'sh', 'md')
        ids = {rf.route_id for rf in result['md']}
        self.assertIn('10.0.1.1_10.0.1.2', ids)
        self.assertIn('10.0.1.3_10.0.1.4', ids)


# ---------------------------------------------------------------------------
# Integration test: end-to-end with synthetic data
# ---------------------------------------------------------------------------

class TestIntegration(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.datadir = os.path.join(self.tmpdir, 'data')
        os.makedirs(self.datadir)

        # baseline route: 10.0.1.1_10.0.1.2
        # other route:    10.0.1.3_10.0.1.4
        # exchange_ts = "20241226090000000" -> H=9, M=0, S=0, ms=0
        #   exch_ps = 9*3600*1e12 = 32400000000000000
        # tap_ts baseline = exch_ps + 500_000 (500 us = 500000 ns) * 1000 = +500_000_000 ps
        #   tap_ts = 32400000000000000 + 500_000_000 = 32400000500000000
        # tap_ts other    = exch_ps + 600_000_000 ps (100 us later than baseline)
        #   tap_ts = 32400000600000000

        exch_ps = 9 * 3600 * 1_000_000_000_000  # 32400000000000000
        base_tap   = exch_ps + 500_000_000       # 500 us absolute latency
        other_tap  = exch_ps + 600_000_000       # 600 us absolute latency (100 us diff)
        other_tap2 = exch_ps + 400_000_000       # 400 us absolute latency (-100 us diff, faster)

        exch_str = "20241226090000000"
        date = "20241226"

        # Write md files
        base_md = os.path.join(self.datadir, f"md_sh_10.0.1.1_10.0.1.2_{date}_tap_tmp.txt")
        other_md = os.path.join(self.datadir, f"md_sh_10.0.1.3_10.0.1.4_{date}_tap_tmp.txt")

        with open(base_md, 'w') as f:
            for ticker in ["000001", "000002", "000003"]:
                f.write(f"{base_tap},{ticker},{exch_str}\n")

        with open(other_md, 'w') as f:
            # 000001 is slower, 000002 is faster, 000003 same as base
            f.write(f"{other_tap},000001,{exch_str}\n")
            f.write(f"{other_tap2},000002,{exch_str}\n")
            f.write(f"{base_tap},000003,{exch_str}\n")

        # Write tbt files
        base_tbt = os.path.join(self.datadir, f"tbt_sh_10.0.1.1_10.0.1.2_{date}_tap_tmp.txt")
        other_tbt = os.path.join(self.datadir, f"tbt_sh_10.0.1.3_10.0.1.4_{date}_tap_tmp.txt")

        with open(base_tbt, 'w') as f:
            for seq in range(1, 4):
                f.write(f"{base_tap},2021,{seq},000001,{exch_str}\n")

        with open(other_tbt, 'w') as f:
            for seq in range(1, 4):
                f.write(f"{other_tap},2021,{seq},000001,{exch_str}\n")

        self.datadir_val = self.datadir
        self.baseline = "10.0.1.1_10.0.1.2"
        self.exch_ps = exch_ps
        self.base_tap = base_tap
        self.other_tap = other_tap
        self.other_tap2 = other_tap2

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir)

    def test_md_single_latency(self):
        """baseline route should have ~500000 ns absolute latency."""
        from latency_stat import _collect_md_single
        from file_scanner import scan_files

        routes = scan_files(self.datadir_val, '20241226', 'sh', 'md')
        result = _collect_md_single(routes['md'], False, 0, 0)

        base_stats = result.get(self.baseline)
        self.assertIsNotNone(base_stats)
        # base latency = 500_000_000 ps / 1000 = 500000 ns
        self.assertEqual(base_stats['min'], 500_000)
        self.assertEqual(base_stats['max'], 500_000)
        self.assertEqual(base_stats['cnt'], 3)

    def test_md_compare_diff(self):
        """non-baseline route diff should be +100000 ns or -100000 ns."""
        from latency_stat import _collect_md_compare
        from file_scanner import scan_files

        routes = scan_files(self.datadir_val, '20241226', 'sh', 'md')
        result = _collect_md_compare(
            routes['md'], self.baseline, self.tmpdir, 0
        )

        other_stats = result.get('10.0.1.3_10.0.1.4')
        self.assertIsNotNone(other_stats)
        self.assertEqual(other_stats['cnt'], 3)
        # diffs: +100000, -100000, 0 ns
        self.assertEqual(other_stats['min'], -100_000)
        self.assertEqual(other_stats['max'], 100_000)
        self.assertEqual(other_stats['avg'], 0)

    def test_tbt_compare_diff(self):
        """tbt non-baseline route diff should be +100000 ns for all seqs."""
        from latency_stat import _collect_tbt_compare
        from file_scanner import scan_files

        routes = scan_files(self.datadir_val, '20241226', 'sh', 'tbt')
        result = _collect_tbt_compare(
            routes['tbt'], self.baseline, self.tmpdir, 0
        )

        other_ch_stats = result.get('10.0.1.3_10.0.1.4')
        self.assertIsNotNone(other_ch_stats)
        ch2021 = other_ch_stats.get('2021')
        self.assertIsNotNone(ch2021)
        self.assertEqual(ch2021['cnt'], 3)
        self.assertEqual(ch2021['min'], 100_000)
        self.assertEqual(ch2021['max'], 100_000)

    def test_reporter_md_compare(self):
        """Section 1 should render properly with baseline row showing —."""
        route_stats = {
            '10.0.1.1_10.0.1.2': None,
            '10.0.1.3_10.0.1.4': {
                'cnt': 160, 'min': -20, 'max': 683, 'avg': 96,
                'stddev': 45, 'p25': 40, 'p50': 80, 'p75': 130,
                'p90': 200, 'p95': 300, 'p99': 550,
            }
        }
        lines = reporter.section_md_compare(
            route_stats, '10.0.1.1_10.0.1.2', 'sh'
        )
        text = '\n'.join(lines)
        self.assertIn('基准', text)
        self.assertIn('—', text)
        self.assertIn('160', text)
        self.assertIn('-20', text)

    def test_full_pipeline(self):
        """Run the full main() pipeline and check output is non-empty Markdown."""
        import subprocess
        result = subprocess.run(
            [
                sys.executable,
                os.path.join(os.path.dirname(__file__), 'latency_stat.py'),
                '--data-dir', self.datadir_val,
                '--date', '20241226',
                '--exchange', 'sh',
                '--baseline', self.baseline,
                '--mode', 'all',
                '--tmp-dir', self.tmpdir,
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print("STDERR:", result.stderr)
        self.assertEqual(result.returncode, 0)
        output = result.stdout
        self.assertIn('## 快照对比', output)
        self.assertIn('## 逐笔对比', output)
        self.assertIn('## 快照单路延时', output)
        self.assertIn('## 逐笔单路延时', output)
        # baseline row should show —
        self.assertIn('基准', output)


if __name__ == '__main__':
    unittest.main(verbosity=2)

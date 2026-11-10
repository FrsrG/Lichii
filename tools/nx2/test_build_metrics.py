#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

import datetime as dt
import unittest

import build_metrics


class BuildMetricsTests(unittest.TestCase):
    def test_actual_workflow_step_names_keep_checkout_out_of_tests(self):
        self.assertEqual(build_metrics.phase("Check out source"), "setup")
        self.assertEqual(build_metrics.phase("Check nxformat.dll is delay-loaded"), "verification")
        self.assertEqual(build_metrics.phase("Run tests"), "tests")
        self.assertEqual(build_metrics.phase("Restore dependencies and compiler objects"), "cache_restore")
        self.assertEqual(build_metrics.phase("Save dependencies and compiler objects"), "cache_save")

    def test_pending_step_is_elapsed_not_finished(self):
        job = {"steps": [{"name": "Build production and tests", "status": "in_progress",
                          "started_at": "2026-11-10T22:04:41Z", "completed_at": None}]}
        result = build_metrics.report(job, as_of=dt.datetime(2026, 11, 10, 22, 5, 41, tzinfo=dt.timezone.utc))
        self.assertIsNone(result["steps"][0]["seconds"])
        self.assertEqual(result["steps"][0]["elapsed_as_of_report_s"], 60)
        self.assertNotIn("compile_and_link", result["completed_phase_seconds"])
        self.assertIsNone(result["peak_memory_bytes"])

    def test_skipped_tests_are_not_reported_as_zero_second_validation(self):
        job = {"steps": [{"name": "Run tests", "status": "completed", "conclusion": "skipped",
                          "started_at": "2026-11-10T22:58:33Z", "completed_at": "2026-11-10T22:58:33Z"}]}
        result = build_metrics.report(job)
        self.assertIsNone(result["steps"][0]["seconds"])
        self.assertNotIn("tests", result["completed_phase_seconds"])

    def test_real_ccache_format_preserves_unknown_counters(self):
        result = build_metrics.parse_log("""2026-11-10T22:26:24.6Z Cacheable calls: 1990 / 2027 (98.17%)
2026-11-10T22:26:24.6Z   Hits: 1556 / 1990 (78.19%)
2026-11-10T22:26:24.6Z     Direct: 1556 / 1556 (100.00%)
2026-11-10T22:26:24.6Z     Preprocessed: 0 / 1556 (0.00%)
2026-11-10T22:26:24.6Z   Misses: 434 / 1990 (21.81%)
2026-11-10T22:26:24.6Z   Cache size (GB): 0.7 / 1.0 (73.80%)
""")
        stats = result["ccache_reports"][0]
        self.assertEqual(stats["compiler_calls"], 2027)
        self.assertEqual(stats["hits"], 1556)
        self.assertEqual(stats["misses"], 434)
        self.assertEqual(stats["size_gb_reported"], 0.7)
        self.assertNotIn("evictions", stats)
        self.assertNotIn("cleanups", stats)

    def test_noop_does_not_invent_cache_hits(self):
        result = build_metrics.parse_log("ninja: no work to do.\n")
        self.assertEqual(result["ccache_reports"], [])
        self.assertEqual(result["ninja"]["observed_compile_commands"], 0)

    def test_interleaved_ninja_completions_are_counted_not_timed(self):
        result = build_metrics.parse_log("""2026-11-10T22:01:01.0Z [1/3] Building CXX object a.obj
2026-11-10T22:01:02.0Z [2/3] Linking CXX static library a.lib
2026-11-10T22:01:03.0Z [3/3] Building RC object b.res
""")
        self.assertEqual(result["ninja"]["observed_compile_commands"], 2)
        self.assertEqual(result["ninja"]["observed_link_commands"], 1)
        self.assertEqual(result["ninja"]["last_progress"], {"completed": 3, "total": 3})


if __name__ == "__main__":
    unittest.main()

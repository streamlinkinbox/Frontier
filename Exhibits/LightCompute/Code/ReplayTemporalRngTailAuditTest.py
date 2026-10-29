#!/usr/bin/env python3
"""Synthetic and recorded-data tests for ReplayTemporalRngTailAudit.py."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest

MODULE_PATH = Path(__file__).with_name("ReplayTemporalRngTailAudit.py")
SPEC = importlib.util.spec_from_file_location("replay_temporal_rng_tail_audit", MODULE_PATH)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
ROOT = MODULE_PATH.parents[1]
SELECTION = ROOT / "Results/ReplayTemporalSelectionStream.txt"
PATH = ROOT / "Results/ReplayTemporalPathStream.txt"

class ReplayTemporalRngTailAuditTests(unittest.TestCase):
    def test_quantile_interpolates(self):
        self.assertAlmostEqual(AUDIT.quantile([0.0, 10.0], 0.25), 2.5)

    def test_single_extreme_cohort_dominates_tail_share(self):
        rows = [{"mean": "0.8", "mse": "1", "max_pixel": "1"} for _ in range(127)]
        rows.append({"mean": "0.8", "mse": "100", "max_pixel": "50"})
        result = AUDIT.summarize(rows, 0.82)
        self.assertAlmostEqual(result["top5_share"], 106.0 / 227.0)
        self.assertAlmostEqual(result["largest_share"], 100.0 / 227.0)

    def test_small_control_distribution(self):
        rows = [{"mean": "1", "mse": "0.01", "max_pixel": "1"} for _ in range(128)]
        result = AUDIT.summarize(rows, 1.0)
        self.assertAlmostEqual(result["mean"], 1.0)
        self.assertEqual(result["mean_se"], 0.0)
        self.assertTrue(result["gate"])
        self.assertAlmostEqual(result["top5_share"], 7.0 / 128.0)

    def test_recorded_cross_study_controls_and_expected_failures(self):
        self.assertTrue(SELECTION.is_file())
        self.assertTrue(PATH.is_file())
        capture = io.StringIO()
        with contextlib.redirect_stdout(capture):
            AUDIT.run(SELECTION, PATH)
        output = capture.getvalue()
        self.assertIn("same_baseline_image_hashes=1280/1280", output)
        self.assertIn("path_arm_selection_hashes_equal=1280/1280", output)
        self.assertIn("path_hashes_changed=1280/1280", output)
        self.assertIn("prior_selection_hashes_changed=1280/1280", output)
        self.assertIn("selection_hash/temporal4/lower, path_hash/temporal1/lower", output)

    def test_chart_is_valid_svg_and_marks_both_failed_cells(self):
        with contextlib.redirect_stdout(io.StringIO()):
            summary, reference = AUDIT.run(SELECTION, PATH)
        from tempfile import TemporaryDirectory
        with TemporaryDirectory() as temp:
            chart = Path(temp) / "audit.svg"
            AUDIT.write_chart(chart, summary, reference)
            svg = chart.read_text()
            self.assertTrue(svg.startswith("<svg "))
            self.assertIn("red rings mark failed", svg.lower())
            self.assertIn("Path RNG · Temporal 1", svg)
            self.assertIn("Selection RNG · Temporal 4", svg)

    def test_incomplete_log_is_rejected(self):
        with self.assertRaises(ValueError):
            AUDIT.load(Path(__file__), "REPLAY_SELECTION_STREAM")

if __name__ == "__main__":
    unittest.main(verbosity=2)

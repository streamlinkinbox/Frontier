#!/usr/bin/env python3
"""Synthetic tests for temporal reservoir provenance controls and summary math."""
import contextlib
import importlib.util
import io
import xml.etree.ElementTree as ET
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

CODE = Path(__file__).parent

def load_module(filename, name):
    spec = importlib.util.spec_from_file_location(name, CODE / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

SUMMARY = load_module("ReplayTemporalProvenanceSummary.py", "provenance_summary")
CANDIDATE = load_module("ReplayTemporalProvenanceCandidate.py", "provenance_candidate")
ROOT = CODE.parents[2]

class ReplayTemporalProvenanceSummaryTests(unittest.TestCase):
    def synthetic_log(self, rows_per_arm=128):
        lines = []
        for arm in SUMMARY.ARMS:
            for mode in SUMMARY.MODES:
                for cohort in range(rows_per_arm):
                    changed = arm != "baseline"
                    path_hash = f"{cohort + (1000 if arm == 'path_hash' else 0):016x}"
                    selection_hash = f"{cohort + (2000 if arm == 'selection_hash' else 0):016x}"
                    image_hash = f"{cohort + (3000 if changed else 0):016x}"
                    lines.append(
                        "REPLAY_TEMPORAL_PROVENANCE_BATCH "
                        f"arm={arm} scenario=motion_visibility_far group=far mode={mode} floor=lower batch={cohort} "
                        f"pixels=512 mean=0.8 mse=0.01 hash={image_hash} path_hash={path_hash} "
                        f"selection_hash={selection_hash} accepted=100 selected=25 expected_selected=25 "
                        "fresh_weight=512 history_weight=256 history_target=200 retained=100 weight_share=0.333333333"
                    )
                lines.append(
                    "REPLAY_TEMPORAL_PROVENANCE_RESULT "
                    f"arm={arm} scenario=motion_visibility_far group=far mode={mode} floor=lower "
                    "reference=0.82 mean=0.8 mean_se=0 rmse=0.1 mse=0.01 accepted=12800 selected=3200 "
                    "accepted_rate=0.1953125 selected_given_accepted=0.25 expected_selected_given_accepted=0.25 "
                    "selection_residual_se=0.01 selection_z_approx=0 fresh_weight=65536 history_weight=32768 "
                    "history_weight_share=0.333333333 history_target=25600 retained=12800 pixels=65536 cohorts=128"
                )
            lines.append(f"REPLAY_TEMPORAL_PROVENANCE_ARM_EXIT arm={arm} status=0")
        return "\n".join(lines) + "\n"

    def test_moments_and_paired_delta(self):
        mean, se = SUMMARY.moments([1.0, 3.0])
        self.assertEqual(mean, 2.0)
        self.assertAlmostEqual(se, 1.0)

    def test_complete_synthetic_log_coverage(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "synthetic.log"
            path.write_text(self.synthetic_log())
            rows, results = SUMMARY.load_provenance(path)
        self.assertEqual(len(rows), 3 * 2 * 128)
        self.assertEqual(len(results), 3 * 2)
        cell = SUMMARY.arm_summary(rows, results, "baseline", "temporal1")
        self.assertEqual(cell["accepted_rate"], 100 / 512)
        self.assertEqual(cell["selected_given_accepted"], .25)
        self.assertEqual(cell["history_weight_share"], 1 / 3)

    def test_incomplete_log_fails_closed(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "incomplete.log"
            path.write_text(self.synthetic_log(rows_per_arm=127))
            with self.assertRaises(ValueError):
                SUMMARY.load_provenance(path)

    def test_nonzero_arm_exit_fails_closed(self):
        text = self.synthetic_log().replace("arm=selection_hash status=0", "arm=selection_hash status=139")
        with TemporaryDirectory() as temp:
            path = Path(temp) / "failed.log"
            path.write_text(text)
            with self.assertRaises(ValueError):
                SUMMARY.load_provenance(path)

    def test_svg_generation_emits_parseable_chart(self):
        with TemporaryDirectory() as temp:
            log = Path(temp) / "synthetic.log"
            svg = Path(temp) / "chart.svg"
            log.write_text(self.synthetic_log())
            rows, results = SUMMARY.load_provenance(log)
            summaries = {(arm, mode): SUMMARY.arm_summary(rows, results, arm, mode)
                         for arm in SUMMARY.ARMS for mode in SUMMARY.MODES}
            SUMMARY.write_chart(svg, summaries)
            root = ET.parse(svg).getroot()
        self.assertTrue(root.tag.endswith("svg"))
        self.assertEqual(root.attrib["viewBox"], "0 0 1400 950")

    def test_candidate_variants_preserve_shared_instrumentation(self):
        with TemporaryDirectory() as temp:
            outputs = [str(Path(temp) / f"{name}.slang") for name in ("baseline", "path", "selection")]
            with contextlib.redirect_stdout(io.StringIO()):
                CANDIDATE.generate(outputs)
            baseline, path, selection = (Path(p).read_text() for p in outputs)
        self.assertEqual(baseline.count("OutputColour[ReplaySlot(7, Pixel)]"), 2)
        self.assertIn("AuditHistorySelected = 1u", baseline)
        self.assertNotEqual(baseline, path)
        self.assertNotEqual(baseline, selection)
        self.assertEqual(path.count("AuditHistorySelected = 1u"), 1)
        self.assertEqual(selection.count("AuditHistorySelected = 1u"), 1)

    def test_candidate_refuses_duplicate_destinations(self):
        with self.assertRaises(ValueError):
            CANDIDATE.generate(["/tmp/prov-same.slang"] * 3)

if __name__ == "__main__":
    unittest.main(verbosity=2)

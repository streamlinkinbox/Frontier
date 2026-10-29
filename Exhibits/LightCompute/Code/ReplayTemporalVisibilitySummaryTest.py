#!/usr/bin/env python3
"""Synthetic validation for temporal visibility instrumentation and attribution."""
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
import xml.etree.ElementTree as ET

CODE = Path(__file__).parent

def load(name):
    spec = importlib.util.spec_from_file_location(name, CODE / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

SUMMARY = load("ReplayTemporalVisibilitySummary")
CANDIDATE = load("ReplayTemporalVisibilityCandidate")

class ReplayTemporalVisibilityTests(unittest.TestCase):
    def setUp(self):
        SUMMARY.COHORTS = 2
        SUMMARY.GRID = 2
        SUMMARY.PIXELS = 2

    def fixture(self):
        lines = []
        for mode in SUMMARY.MODES:
            for cohort, cohort_mean, mse in ((0, .8, .01), (1, .9, .04)):
                for pixel in range(SUMMARY.GRID * SUMMARY.GRID):
                    if (pixel % SUMMARY.GRID + pixel // SUMMARY.GRID) % 2 != 1:
                        continue
                    visible = pixel == 1
                    nee = .1 if visible else 0
                    path_luma = .1999 if visible else 0.0
                    target = path_luma + .0001
                    history_weight = target * 4
                    probability = history_weight / (3 + history_weight)
                    lines.append(
                        "REPLAY_TEMPORAL_VISIBILITY_PIXEL "
                        f"mode={mode} cohort={cohort} pixel={pixel} x={pixel % SUMMARY.GRID} y={pixel // SUMMARY.GRID} seed=1234 "
                        f"direct_rays=0 direct_visible=0 bsdf_miss={int(visible)} first_opaque_hit={int(not visible)} "
                        f"nee_luma={nee} emitter_hits=0 emitter_luma=0 path_luma={path_luma} target={target} "
                        f"source_weight=2 source_count=2 retained=2 history_weight={history_weight} fresh_weight=3 "
                        f"previous_path_luma=.1999 selection_probability={probability} selected={int(pixel == 1)}"
                    )
                lines.append(
                    "REPLAY_TEMPORAL_VISIBILITY_COHORT "
                    f"mode={mode} cohort={cohort} pixels=2 mean={cohort_mean} mse={mse} hash={cohort:016x} "
                    "direct_rays=0 direct_visible=0 bsdf_miss=1 first_opaque_hit=1 emitter_hits=0 "
                    "nee_luma=.1 emitter_luma=0 history_weight=.8004"
                )
            lines.append(
                "REPLAY_TEMPORAL_VISIBILITY_RESULT "
                f"mode={mode} scenario=motion_visibility_far reference=.82 pixels=4 direct_rays=0 direct_visible=0 "
                "direct_visibility_rate=0 bsdf_miss=2 first_opaque_hit=2 emitter_hits=0 selected=2 "
                "visible_samples=2 occluded_samples=2 prior_lit_current_blocked=2 "
                "prior_blocked_current_lit=0 stable_lit=2 stable_blocked=0 mean=.85 "
                "mean_se=.05 rmse=.158113883"
            )
        lines.append("REPLAY_TEMPORAL_VISIBILITY_EXIT status=0")
        return "\n".join(lines) + "\n"

    def prior(self, prefix):
        lines = []
        for mode in SUMMARY.MODES:
            for cohort, m, mse in ((0, .8, .01), (1, .9, .04)):
                lines.append(f"{prefix}_BATCH arm=baseline mode={mode} floor=lower batch={cohort} "
                             f"hash={cohort:016x} mean={m} mse={mse}")
        return "\n".join(lines) + "\n"

    def test_full_synthetic_visibility_attribution_and_svg(self):
        with TemporaryDirectory() as temp:
            temp = Path(temp)
            log, path, selection, svg = (temp / name for name in ("run.log", "path.log", "selection.log", "chart.svg"))
            log.write_text(self.fixture())
            path.write_text(self.prior("REPLAY_PATH_STREAM"))
            selection.write_text(self.prior("REPLAY_SELECTION_STREAM"))
            pixels, cohorts, results = SUMMARY.load_run(log)
            summaries = SUMMARY.summarize(pixels, cohorts, results)
            SUMMARY.compare_prior(cohorts, path, selection)
            SUMMARY.chart(svg, summaries)
            root = ET.parse(svg).getroot()
        self.assertTrue(root.tag.endswith("svg"))
        self.assertAlmostEqual(summaries["temporal1"]["path_visible_rate"], .5)
        self.assertEqual(summaries["temporal1"]["occluded_samples"], 2)
        self.assertEqual(summaries["temporal1"]["prior_lit_current_blocked"], 2)
        self.assertEqual(summaries["temporal1"]["occluded_nee"], 0)

    def test_incomplete_or_failed_run_is_rejected(self):
        for text in (self.fixture().replace("seed=1234", "seed=1234", 1).replace("REPLAY_TEMPORAL_VISIBILITY_PIXEL", "DROPPED_PIXEL", 1),
                     self.fixture().replace("REPLAY_TEMPORAL_VISIBILITY_EXIT status=0", "REPLAY_TEMPORAL_VISIBILITY_EXIT status=139")):
            with TemporaryDirectory() as temp:
                path = Path(temp) / "bad.log"
                path.write_text(text)
                with self.assertRaises(ValueError):
                    SUMMARY.load_run(path)

    def test_bad_weight_identity_is_rejected(self):
        text = self.fixture().replace("history_weight=0.7999999999999999", "history_weight=0.7", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            pixels, cohorts, results = SUMMARY.load_run(path)
            with self.assertRaises(ValueError):
                SUMMARY.summarize(pixels, cohorts, results)

    def test_generator_keeps_instrumentation_test_only_and_covers_all_evaluator_calls(self):
        with TemporaryDirectory() as temp:
            output = Path(temp) / "Visibility.slang"
            CANDIDATE.generate(output)
            shader = output.read_text()
        self.assertIn("out float4 VisibilityAudit", shader)
        self.assertIn("HistorySelectionProbability", shader)
        self.assertIn("ReplaySlot(8, Pixel)", shader)
        self.assertEqual(shader.count("EvaluateReplayPath(R,"), 6)
        self.assertIn("EncodeReplaySeed(Candidate), Source.z, Source.w", shader)
        self.assertIn("ReplaySlot(10, Pixel)", shader)
        self.assertIn("ReplaySlot(11, Pixel)", shader)
        self.assertIn("PreviousPathRecord.y", shader)
        self.assertIn("PreviousPathRecord.zw", shader)
        self.assertIn("OutputColour[ReplaySlot(9, Pixel)] = float4(Power", shader)

if __name__ == "__main__":
    unittest.main(verbosity=2)

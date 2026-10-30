#!/usr/bin/env python3
"""Synthetic coverage and invariants for blocker-specific direct-visibility attribution."""
import importlib.util
import re
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).parent
SPEC = importlib.util.spec_from_file_location("ReplayTemporalDirectVisibilityAblationSummary",
                                              HERE / "ReplayTemporalDirectVisibilityAblationSummary.py")
SUMMARY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUMMARY)


class VisibilityAblationSummaryTests(unittest.TestCase):
    def setUp(self):
        SUMMARY.GRID = 2
        SUMMARY.PIXEL_IDS = (1, 2)

    def fixture(self):
        lines = ["REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION fixture=overhead_emitter "
                 "motion=moving scenarios=base_only,added_only",
                 "REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION pipelines_ready=1 visibility=base_only "
                 "cohorts=2 pixels_per_cohort=2",
                 "REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION pipelines_ready=1 visibility=added_only "
                 "cohorts=2 pixels_per_cohort=2"]
        for scenario in SUMMARY.SCENARIOS:
            for mode in SUMMARY.MODES:
                transition_counts = [0] * 9
                history_sums = [0.0] * 9
                nee_sums = [0.0] * 9
                rays = visible = blocked = 0
                for cohort in range(2):
                    states = ((2, 2), (2, 2)) if scenario == "base_only" else ((2, 1), (1, 2))
                    for pixel, (previous, current) in zip(SUMMARY.PIXEL_IDS, states):
                        direct_visible = int(current == 2)
                        nee, target, source_weight, retained = .1 * direct_visible, .2, 2.0, 1.0
                        history_weight, fresh_weight = target * source_weight, .6
                        probability = history_weight / (history_weight + fresh_weight)
                        transition = previous * 3 + current
                        transition_counts[transition] += 1
                        history_sums[transition] += history_weight
                        nee_sums[transition] += nee
                        rays += 1
                        visible += direct_visible
                        blocked += int(current == 1)
                        lines.append(
                            "REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_PIXEL "
                            f"visibility={scenario} mode={mode} cohort={cohort} pixel={pixel} "
                            f"x={pixel % 2} y={pixel // 2} seed=000004d2 "
                            "seed_sequence=000004d2:000004d2:000004d2:000004d2 path_seed=1234 "
                            f"prior_direct_code={previous} "
                            f"current_direct_code={current} direct_rays=1 direct_visible={direct_visible} "
                            f"nee_luma={nee} target={target} source_weight={source_weight} "
                            f"source_count=2 retained={retained} history_weight={history_weight} "
                            f"fresh_weight={fresh_weight} selection_probability={probability} "
                            "previous_path_luma=.1 current_path_luma=.2")
                lines.append(
                    f"REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_RESULT visibility={scenario} mode={mode} "
                    f"rows=4 direct_rays={rays} direct_visible={visible} no_ray=0 blocked={blocked} visible={visible} "
                    + " ".join(f"transition_{i // 3}{i % 3}={transition_counts[i]} "
                               f"history_weight_mean_{i // 3}{i % 3}="
                               f"{history_sums[i] / transition_counts[i] if transition_counts[i] else 0} "
                               f"nee_mean_{i // 3}{i % 3}="
                               f"{nee_sums[i] / transition_counts[i] if transition_counts[i] else 0}"
                               for i in range(9)))
            lines.append(f"PASS ReplayTemporalDirectVisibilityAblationValidation visibility={scenario} rows=8 "
                         "same_seed_prior_current_pairs=verified matched_frame_seed_sequences=verified "
                         "estimator_math_changed=0")
        return "\n".join(lines) + "\n"

    def test_paired_scenarios_transition_weights_and_chart(self):
        with TemporaryDirectory() as temp:
            log, svg = Path(temp) / "ablation.log", Path(temp) / "ablation.svg"
            log.write_text(self.fixture())
            summaries = SUMMARY.summarize(SUMMARY.load(log))
            SUMMARY.chart(svg, summaries)
            root = ET.parse(svg).getroot()
        self.assertTrue(root.tag.endswith("svg"))
        self.assertEqual(summaries["base_only"]["temporal1"]["transitions"][8], 4)
        self.assertEqual(summaries["added_only"]["temporal1"]["transitions"][5], 2)
        self.assertEqual(summaries["added_only"]["temporal1"]["transitions"][7], 2)
        self.assertAlmostEqual(summaries["added_only"]["temporal1"]["history_weight_means"][5], .4)

    def test_missing_pixels_or_pass_marker_rejected(self):
        for text in (self.fixture().replace("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_PIXEL", "DROPPED", 1),
                     self.fixture().replace("PASS ReplayTemporalDirectVisibilityAblationValidation", "MISSING_PASS", 1)):
            with TemporaryDirectory() as temp:
                path = Path(temp) / "bad.log"
                path.write_text(text)
                with self.assertRaises(ValueError):
                    SUMMARY.load(path)

    def test_cross_arm_seed_mismatch_rejected(self):
        text = re.sub(
            r"(visibility=added_only mode=\w+ cohort=0 pixel=\d+ x=\d+ y=\d+ seed=)000004d2 "
            r"seed_sequence=000004d2:000004d2:000004d2:000004d2",
            r"\g<1>000010e1 seed_sequence=000010e1:000010e1:000010e1:000010e1",
            self.fixture())
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "matched host seed sequence mismatch"):
                SUMMARY.load(path)

    def test_bad_history_identity_rejected(self):
        text = self.fixture().replace("history_weight=0.4", "history_weight=0.3", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "history-weight identity"):
                SUMMARY.summarize(SUMMARY.load(path))

    def test_base_only_blocked_transition_rejected(self):
        text = self.fixture().replace("prior_direct_code=2 current_direct_code=2 direct_rays=1 direct_visible=1",
                                      "prior_direct_code=2 current_direct_code=1 direct_rays=1 direct_visible=0", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            parsed = SUMMARY.load(path)
            with self.assertRaises(ValueError):
                SUMMARY.summarize(parsed)


if __name__ == "__main__":
    unittest.main(verbosity=2)

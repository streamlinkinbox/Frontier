#!/usr/bin/env python3
"""Synthetic checks for paired direct-NEE visibility summaries."""
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).parent
SPEC = importlib.util.spec_from_file_location("ReplayTemporalDirectVisibilitySummary",
                                              HERE / "ReplayTemporalDirectVisibilitySummary.py")
SUMMARY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUMMARY)


class DirectVisibilitySummaryTests(unittest.TestCase):
    def setUp(self):
        SUMMARY.COHORTS = 2
        SUMMARY.GRID = 2
        SUMMARY.PIXEL_IDS = (1, 2)

    def fixture(self):
        lines = [f"REPLAY_TEMPORAL_VISIBILITY_DIRECT pipelines_ready=1 cohorts={SUMMARY.COHORTS} "
                 f"pixels_per_cohort={len(SUMMARY.PIXEL_IDS)} bounces=1 source_sampling=uniform"]
        for mode in SUMMARY.MODES:
            totals = {"transitions": [0] * 9, "outcomes": [0] * 3, "nee_luma": [0.0] * 3,
                      "target": [0.0] * 3, "history_weight": [0.0] * 3}
            for cohort in range(SUMMARY.COHORTS):
                for pixel, previous, current in ((1, 1, 2), (2, 2, 1)):
                    rays, visible = 1, int(current == 2)
                    nee, target, source_weight, retained = (.1 if visible else 0.0), .2, 2.0, 1.0
                    history_weight, fresh_weight = target * source_weight * retained, 3.0
                    probability = history_weight / (history_weight + fresh_weight)
                    lines.append(
                        "REPLAY_TEMPORAL_VISIBILITY_DIRECT_PIXEL "
                        f"mode={mode} cohort={cohort} pixel={pixel} x={pixel % 2} y={pixel // 2} seed=1234 "
                        f"prior_direct_code={previous} current_direct_code={current} direct_rays={rays} "
                        f"direct_visible={visible} nee_luma={nee} target={target} source_weight={source_weight} "
                        f"source_count=2 retained={retained} history_weight={history_weight} "
                        f"fresh_weight={fresh_weight} selection_probability={probability} previous_path_luma=.1 current_path_luma=.2"
                    )
                    totals["transitions"][previous * 3 + current] += 1
                    totals["outcomes"][current] += 1
                    totals["nee_luma"][current] += nee
                    totals["target"][current] += target
                    totals["history_weight"][current] += history_weight
            means = {key: [totals[key][i] / totals["outcomes"][i] if totals["outcomes"][i] else 0
                           for i in range(3)] for key in ("nee_luma", "target", "history_weight")}
            labels = ("no_ray", "blocked", "visible")
            line = ("REPLAY_TEMPORAL_VISIBILITY_DIRECT_RESULT "
                    f"mode={mode} pixels=4 direct_rays=4 direct_visible=2 "
                    f"no_ray={totals['outcomes'][0]} blocked={totals['outcomes'][1]} visible={totals['outcomes'][2]} "
                    + " ".join(f"transition_{i//3}{i%3}={totals['transitions'][i]}" for i in range(9)) + " "
                    + " ".join(f"{labels[i]}_mean_{metric}={means[metric][i]}"
                               for i in range(3) for metric in ("nee_luma", "target", "history_weight"))
            )
            lines.append(line)
        lines.append("PASS ReplayTemporalDirectVisibilityValidation rows=8 same_seed_prior_current_pairs=verified")
        return "\n".join(lines) + "\n"

    def test_transition_aggregate_and_visual(self):
        with TemporaryDirectory() as temp:
            log, svg = Path(temp) / "direct.log", Path(temp) / "direct.svg"
            log.write_text(self.fixture())
            pixels, results = SUMMARY.load(log)
            summaries = SUMMARY.summarize(pixels, results)
            SUMMARY.chart(svg, summaries)
            root = ET.parse(svg).getroot()
        self.assertTrue(root.tag.endswith("svg"))
        self.assertEqual(summaries["temporal1"]["transitions"][5], 2)
        self.assertEqual(summaries["temporal1"]["transitions"][7], 2)
        self.assertEqual(summaries["temporal1"]["outcomes"], [0, 2, 2])
        self.assertAlmostEqual(summaries["temporal1"]["means"]["nee_luma"][2], .1)

    def test_missing_rows_or_pass_marker_rejected(self):
        for text in (self.fixture().replace("REPLAY_TEMPORAL_VISIBILITY_DIRECT_PIXEL", "DROPPED", 1),
                     self.fixture().replace("PASS ReplayTemporalDirectVisibilityValidation", "MISSING_PASS")):
            with TemporaryDirectory() as temp:
                path = Path(temp) / "bad.log"
                path.write_text(text)
                with self.assertRaises(ValueError):
                    SUMMARY.load(path)

    def test_invalid_weight_identity_rejected(self):
        text = self.fixture().replace("history_weight=0.4", "history_weight=0.3", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            pixels, results = SUMMARY.load(path)
            with self.assertRaises(ValueError):
                SUMMARY.summarize(pixels, results)


if __name__ == "__main__":
    unittest.main(verbosity=2)

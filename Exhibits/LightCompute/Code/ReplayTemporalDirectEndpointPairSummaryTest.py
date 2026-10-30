#!/usr/bin/env python3
"""Synthetic tests for exact-endpoint visibility re-traces and per-cohort associations."""
from pathlib import Path
from tempfile import TemporaryDirectory
import importlib.util
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).parent
SPEC = importlib.util.spec_from_file_location(
    "ReplayTemporalDirectEndpointPairSummary",
    HERE / "ReplayTemporalDirectEndpointPairSummary.py")
SUMMARY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUMMARY)

BASE_SPEC = importlib.util.spec_from_file_location(
    "ReplayTemporalDirectVisibilityAblationSummaryTest",
    HERE / "ReplayTemporalDirectVisibilityAblationSummaryTest.py")
BASE_TEST = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE_TEST)


class EndpointPairSummaryTests(unittest.TestCase):
    def setUp(self):
        SUMMARY.VISIBILITY.GRID = 2
        SUMMARY.VISIBILITY.PIXEL_IDS = (1, 2)
        SUMMARY.PIXEL_IDS = (1, 2)

    def fixture(self):
        base = BASE_TEST.VisibilityAblationSummaryTests()
        base.setUp()
        input_lines = base.fixture().splitlines()
        out = []
        samples = {}
        aggregate = {}
        cohort_rows = []
        for line in input_lines:
            if line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION pipelines_ready="):
                line += " reference_luma=1 reference_coarse_luma=1"
            if line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_PIXEL "):
                row = SUMMARY.fields(line)
                scenario, mode = row["visibility"], row["mode"]
                cohort, pixel = int(row["cohort"]), int(row["pixel"])
                previous, current = int(row["prior_direct_code"]), int(row["current_direct_code"])
                visible_nee = float(row["nee_luma"])
                endpoint = "0,0,3,0"
                line += (f" paired_prior_code={previous} paired_current_code={current} paired_ray=1 "
                         f"paired_transmittance={int(current == 2)} prior_endpoint={endpoint} "
                         f"history_endpoint={endpoint} selected_endpoint={endpoint} endpoint_same_replay=1 "
                         f"endpoint_same_selected=1 direct_estimate_luma={visible_nee} "
                         f"fixed_endpoint_mask1_code=2 "
                         f"fixed_endpoint_mask3_code={1 if scenario == 'added_only' and pixel == 2 else 2}")
                key = (scenario, mode, cohort)
                item = samples.setdefault(key, {"means": [], "counts": [0] * 9, "weights": [0.0] * 9})
                cell = previous * 3 + current
                item["counts"][cell] += 1
                item["weights"][cell] += .4
                item["means"].append(visible_nee)
            elif line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_RESULT "):
                row = SUMMARY.fields(line)
                scenario, mode = row["visibility"], row["mode"]
                lines_for_mode = [x for x in input_lines if x.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_PIXEL ")]
                records = [SUMMARY.fields(x) for x in lines_for_mode
                           if SUMMARY.fields(x)["visibility"] == scenario and SUMMARY.fields(x)["mode"] == mode]
                counts = [0] * 9
                for sample in records:
                    counts[int(sample["prior_direct_code"]) * 3 + int(sample["current_direct_code"])] += 1
                line += " paired_endpoint_pairs=4 endpoint_matches=4 selected_endpoint_matches=4"
                for cell, n in enumerate(counts):
                    p, c = divmod(cell, 3)
                    line += f" paired_transition_{p}{c}={n} paired_history_weight_mean_{p}{c}={.4 if n else 0}"
            elif line.startswith("PASS ReplayTemporalDirectVisibilityAblationValidation "):
                scenario = SUMMARY.fields(line)["visibility"]
                line += (" matched_endpoint_retraces=verified fixed_endpoint_mask1_vs_mask3=verified "
                         f"mask1_gpu_cpu_match={'verified' if scenario == 'base_only' else 'crosscheck_only'}")
            out.append(line)

        for (scenario, mode, cohort), item in samples.items():
            record = (f"REPLAY_TEMPORAL_DIRECT_ENDPOINT_COHORT visibility={scenario} mode={mode} cohort={cohort} "
                      f"samples=2 estimate_mean_luma={sum(item['means']) / 2} endpoint_pairs=2 endpoint_matches=2 "
                      f"selected_endpoint_matches=2")
            for cell, n in enumerate(item["counts"]):
                p, c = divmod(cell, 3)
                mean_weight = item["weights"][cell] / n if n else 0
                record += f" pair_transition_{p}{c}={n} pair_history_mean_{p}{c}={mean_weight}"
            cohort_rows.append(record)
        return "\n".join(out + cohort_rows) + "\n"

    def test_endpoint_matrix_cohort_means_correlations_and_chart(self):
        with TemporaryDirectory() as temp:
            log, svg = Path(temp) / "endpoint.log", Path(temp) / "endpoint.svg"
            log.write_text(self.fixture())
            summaries = SUMMARY.parse(log)
            SUMMARY.chart(svg, summaries)
            root = ET.parse(svg).getroot()
        added = summaries["added_only"]["temporal1"]
        self.assertEqual(added["transitions"][5], 2)
        self.assertEqual(added["transitions"][7], 2)
        self.assertEqual(added["endpoint_matches"], added["endpoint_pairs"])
        self.assertEqual(added["selected_endpoint_matches"], added["endpoint_pairs"])
        self.assertAlmostEqual(added["mean"], .05)
        self.assertFalse(added["mean_gate"])
        self.assertTrue(root.tag.endswith("svg"))

    def test_endpoint_coordinate_mismatch_rejected(self):
        text = self.fixture().replace("history_endpoint=0,0,3,0", "history_endpoint=0.1,0,3,0", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "endpoint equality flag"):
                SUMMARY.parse(path)

    def test_selected_endpoint_coordinate_mismatch_rejected(self):
        text = self.fixture().replace("selected_endpoint=0,0,3,0", "selected_endpoint=0.1,0,3,0", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "selected endpoint equality flag"):
                SUMMARY.parse(path)

    def test_missing_cohort_summary_rejected(self):
        text = self.fixture()
        line = next(x for x in text.splitlines() if x.startswith("REPLAY_TEMPORAL_DIRECT_ENDPOINT_COHORT "))
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text.replace(line + "\n", "", 1))
            with self.assertRaisesRegex(ValueError, "missing endpoint-pair cohort record"):
                SUMMARY.parse(path)

    def test_paired_transition_aggregate_disagreement_rejected(self):
        text = self.fixture().replace("paired_transition_21=2", "paired_transition_21=3", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            with self.assertRaises(ValueError):
                SUMMARY.parse(path)

    def test_pearson_handles_constant_and_inverse_series(self):
        self.assertIsNone(SUMMARY.pearson([1, 1, 1], [2, 3, 4]))
        self.assertAlmostEqual(SUMMARY.pearson([1, 2, 3], [3, 2, 1]), -1)


if __name__ == "__main__":
    unittest.main(verbosity=2)

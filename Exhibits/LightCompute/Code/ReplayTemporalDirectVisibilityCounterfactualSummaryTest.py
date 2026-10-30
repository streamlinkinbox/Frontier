#!/usr/bin/env python3
"""Synthetic tests for fixed-origin, fixed-endpoint mask-1/mask-3 traces."""
from pathlib import Path
from tempfile import TemporaryDirectory
import importlib.util
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).parent
PAIR_TEST_SPEC = importlib.util.spec_from_file_location(
    "ReplayTemporalDirectEndpointPairSummaryTest",
    HERE / "ReplayTemporalDirectEndpointPairSummaryTest.py")
PAIR_TEST = importlib.util.module_from_spec(PAIR_TEST_SPEC)
PAIR_TEST_SPEC.loader.exec_module(PAIR_TEST)
SUMMARY_SPEC = importlib.util.spec_from_file_location(
    "ReplayTemporalDirectVisibilityCounterfactualSummary",
    HERE / "ReplayTemporalDirectVisibilityCounterfactualSummary.py")
SUMMARY = importlib.util.module_from_spec(SUMMARY_SPEC)
SUMMARY_SPEC.loader.exec_module(SUMMARY)


class CounterfactualSummaryTests(unittest.TestCase):
    def setUp(self):
        SUMMARY.VISIBILITY.GRID = 2
        SUMMARY.VISIBILITY.PIXEL_IDS = (1, 2)
        SUMMARY.PIXEL_IDS = (1, 2)
        SUMMARY.PAIR.PIXEL_IDS = (1, 2)
        PAIR_TEST.SUMMARY.VISIBILITY.GRID = 2
        PAIR_TEST.SUMMARY.VISIBILITY.PIXEL_IDS = (1, 2)
        PAIR_TEST.SUMMARY.PIXEL_IDS = (1, 2)

    def fixture(self):
        source = PAIR_TEST.EndpointPairSummaryTests()
        source.setUp()
        return source.fixture()

    def test_fixed_endpoint_transitions_and_chart(self):
        with TemporaryDirectory() as temp:
            log, svg = Path(temp) / "counterfactual.log", Path(temp) / "counterfactual.svg"
            log.write_text(self.fixture())
            summaries = SUMMARY.parse(log)
            SUMMARY.chart(svg, summaries)
            root = ET.parse(svg).getroot()
        self.assertEqual(summaries["base_only"]["temporal1"]["counts"], [0, 0, 0, 4])
        added = summaries["added_only"]["temporal1"]
        self.assertEqual(added["counts"], [0, 0, 2, 2])
        self.assertEqual(added["added_blocker_visibility_losses"], 2)
        self.assertEqual(added["pairs"], 4)
        self.assertTrue(root.tag.endswith("svg"))

    def test_mask1_gpu_cpu_disagreement_rejected(self):
        text = self.fixture().replace("fixed_endpoint_mask1_code=2", "fixed_endpoint_mask1_code=1", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "mismatch.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "CPU mask-1 trace disagrees"):
                SUMMARY.parse(path)

    def test_invalid_counterfactual_code_rejected(self):
        text = self.fixture().replace("fixed_endpoint_mask3_code=2", "fixed_endpoint_mask3_code=3", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "invalid.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "trace is missing or invalid"):
                SUMMARY.parse(path)

    def test_counterfactual_marker_required(self):
        text = self.fixture().replace("fixed_endpoint_mask1_vs_mask3=verified", "fixed_endpoint_mask1_vs_mask3=missing", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "marker.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "mask-1/mask-3 marker missing"):
                SUMMARY.parse(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)

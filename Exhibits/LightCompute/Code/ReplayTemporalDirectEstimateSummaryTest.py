#!/usr/bin/env python3
"""Synthetic coverage and negative controls for direct-emitter estimate summaries."""
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).parent
SPEC = importlib.util.spec_from_file_location("ReplayTemporalDirectEstimateSummary",
                                              HERE / "ReplayTemporalDirectEstimateSummary.py")
SUMMARY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUMMARY)


class DirectEstimateSummaryTests(unittest.TestCase):
    def setUp(self):
        self.batches = 3
        self.reference = 1.0
        self.coarse = .9999
        self.tolerance = .005

    def fixture(self):
        lines = [
            "REPLAY_TEMPORAL_DIRECT_ESTIMATE pipelines_ready=1 scope=nee_only "
            "bsdf_continuation_excluded=1 total_radiance_claim=0 cohorts=3 pixels_per_cohort=512 "
            f"reference_luma={self.reference} reference_coarse_luma={self.coarse} "
            f"reference_delta={abs(self.reference-self.coarse)} gate_tolerance={self.tolerance} "
            "receiver_x=1.35 emission_red=10 occluders=2"
        ]
        all_means = {}
        all_mses = {}
        for mode in SUMMARY.MODES:
            offset = .02 if mode == "temporal4" else 0.0
            means = [self.reference + offset + delta for delta in (-.001, 0.0, .001)]
            mses = [.04 + (value - self.reference) ** 2 for value in means]
            all_means[mode], all_mses[mode] = means, mses
            for cohort, (mean, mse) in enumerate(zip(means, mses)):
                lines.append(f"REPLAY_TEMPORAL_DIRECT_ESTIMATE_COHORT mode={mode} cohort={cohort} "
                             f"pixels=512 mean_luma={mean} mse={mse}")
            average = sum(means) / self.batches
            variance = sum((x-average)**2 for x in means) / (self.batches-1)
            batch_se = (variance / self.batches) ** .5
            rmse = (sum(mses) / self.batches) ** .5
            gate = int(abs(average-self.reference) <= 6*batch_se+self.tolerance)
            lines.append("REPLAY_TEMPORAL_DIRECT_ESTIMATE_RESULT "
                         f"mode={mode} cohorts={self.batches} pixels_per_cohort=512 "
                         f"reference_luma={self.reference} reference_coarse_luma={self.coarse} "
                         f"mean_luma={average} batch_se={batch_se} rmse={rmse} "
                         f"gate_tolerance={self.tolerance} mean_gate={gate}")
        all_gates = int(all(abs(sum(values)/len(values)-self.reference) <=
                            6*((sum((x-sum(values)/len(values))**2 for x in values)/(len(values)-1))/len(values))**.5+
                            self.tolerance for values in all_means.values()))
        lines.append("PASS ReplayTemporalDirectEmitterEstimateValidation cohorts=3 modes=4 scope=nee_only "
                     f"independent_area_reference=1 estimator_math_changed=0 all_mean_gates={all_gates}")
        return "\n".join(lines) + "\n"

    def scenario_fixture(self, scenario, motion="moving"):
        text = self.fixture()
        replacements = {
            "REPLAY_TEMPORAL_DIRECT_ESTIMATE pipelines_ready=1 ":
                f"REPLAY_TEMPORAL_DIRECT_ESTIMATE pipelines_ready=1 motion={motion} visibility={scenario} ",
            "REPLAY_TEMPORAL_DIRECT_ESTIMATE_COHORT mode=":
                f"REPLAY_TEMPORAL_DIRECT_ESTIMATE_COHORT visibility={scenario} mode=",
            "REPLAY_TEMPORAL_DIRECT_ESTIMATE_RESULT mode=":
                f"REPLAY_TEMPORAL_DIRECT_ESTIMATE_RESULT visibility={scenario} mode=",
            "PASS ReplayTemporalDirectEmitterEstimateValidation cohorts=":
                f"PASS ReplayTemporalDirectEmitterEstimateValidation visibility={scenario} cohorts=",
        }
        for old, new in replacements.items():
            text = text.replace(old, new)
        return (f"REPLAY_TEMPORAL_DIRECT_ESTIMATE fixture=overhead_emitter motion={motion} "
                f"visibility={scenario}\n" + text)

    def test_summary_and_chart_include_failed_gate_without_promoting_it(self):
        with TemporaryDirectory() as temp:
            log, svg = Path(temp) / "estimate.log", Path(temp) / "estimate.svg"
            log.write_text(self.fixture())
            result = SUMMARY.load(log)
            SUMMARY.chart(svg, result)
            root = ET.parse(svg).getroot()
        self.assertTrue(root.tag.endswith("svg"))
        self.assertTrue(result["fresh1"]["gate"])
        self.assertFalse(result["temporal4"]["gate"])
        self.assertAlmostEqual(result["fresh1"]["mean"], self.reference)

    def test_missing_cohort_or_pass_is_rejected(self):
        for text in (self.fixture().replace("REPLAY_TEMPORAL_DIRECT_ESTIMATE_COHORT", "DROPPED", 1),
                     self.fixture().replace("PASS ReplayTemporalDirectEmitterEstimateValidation", "MISSING_PASS")):
            with TemporaryDirectory() as temp:
                path = Path(temp) / "bad.log"
                path.write_text(text)
                with self.assertRaises(ValueError):
                    SUMMARY.load(path)

    def test_aggregate_disagreement_is_rejected(self):
        text = self.fixture().replace("mean_luma=1.0 batch_se=", "mean_luma=1.01 batch_se=", 1)
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "aggregate mean_luma"):
                SUMMARY.load(path)

    def test_nonconverged_reference_is_rejected(self):
        text = self.fixture().replace("reference_coarse_luma=0.9999", "reference_coarse_luma=0.9")
        with TemporaryDirectory() as temp:
            path = Path(temp) / "bad.log"
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "coarse/fine convergence"):
                SUMMARY.load(path)

    def test_stationary_scenario_is_labeled_and_motion_metadata_validated(self):
        stationary = self.fixture().replace("pipelines_ready=1 scope=nee_only",
                                            "pipelines_ready=1 motion=stationary scope=nee_only")
        with TemporaryDirectory() as temp:
            path, svg = Path(temp) / "stationary.log", Path(temp) / "stationary.svg"
            path.write_text(stationary)
            values = SUMMARY.load(path)
            SUMMARY.chart(svg, values)
            self.assertIn("stationary / blocked", svg.read_text())
            path.write_text(stationary.replace("motion=stationary", "motion=unmodeled"))
            with self.assertRaisesRegex(ValueError, "unknown reference motion"):
                SUMMARY.load(path)
            conflict = stationary.replace(
                "REPLAY_TEMPORAL_DIRECT_ESTIMATE pipelines_ready=",
                "REPLAY_TEMPORAL_DIRECT_ESTIMATE fixture=controlled motion=moving\n"
                "REPLAY_TEMPORAL_DIRECT_ESTIMATE pipelines_ready=", 1)
            path.write_text(conflict)
            with self.assertRaisesRegex(ValueError, "disagree about receiver motion"):
                SUMMARY.load(path)
            clear = self.scenario_fixture("clear")
            path.write_text(clear)
            clear_values = SUMMARY.load(path)
            SUMMARY.chart(svg, clear_values)
            self.assertIn("moving / clear", svg.read_text())
            path.write_text(clear.replace("fixture=overhead_emitter motion=moving visibility=clear",
                                          "fixture=overhead_emitter motion=moving visibility=blocked"))
            with self.assertRaisesRegex(ValueError, "disagree about direct visibility"):
                SUMMARY.load(path)
            path.write_text(clear.replace("visibility=clear", "visibility=unspecified"))
            with self.assertRaisesRegex(ValueError, "unknown direct-visibility"):
                SUMMARY.load(path)

    def test_multi_scenario_occluder_ablation_is_separated_and_labeled(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "ablation.log"
            svg = Path(temp) / "ablation.svg"
            path.write_text(self.scenario_fixture("base_only") + self.scenario_fixture("added_only"))
            base = SUMMARY.load(path, "base_only")
            added = SUMMARY.load(path, "added_only")
            SUMMARY.chart(svg, base)
            self.assertIn("moving / base_only", svg.read_text())
            SUMMARY.chart(svg, added)
            self.assertIn("moving / added_only", svg.read_text())
            with self.assertRaisesRegex(ValueError, "multiple scenarios"):
                SUMMARY.load(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""Check exact span joins, rejected-guide exclusion, parallel decisions and editable document provenance."""

import hashlib
import json
from pathlib import Path
import unittest
import numpy as np
from scipy.interpolate import BSpline
from ConsolidationSequence import Join, ReadCurves


class ConsolidationVerification(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        Folder = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"
        cls.Source = (Folder / "Liger_Guide_Candidates.arc").read_text()
        cls.Document = (Folder / "Liger_Consolidated.arc").read_text()
        cls.Metadata = json.loads((Folder / "Liger_Consolidated.guides.json").read_text())
        cls.Original = ReadCurves(cls.Source)
        cls.Current = ReadCurves(cls.Document)

    def testSourceJournalAndHash(self):
        self.assertTrue(self.Document.startswith(self.Source.rstrip()))
        self.assertEqual(hashlib.sha256(self.Source.encode()).hexdigest(), self.Metadata["sourceSha256"])
        self.assertEqual(hashlib.sha256(self.Document.encode()).hexdigest(), self.Metadata["documentSha256"])
        self.assertFalse(self.Metadata["bodyChanged"])
        for Name, (Poles, Knots) in self.Original.items():
            np.testing.assert_array_equal(self.Current[Name][0], Poles)
            np.testing.assert_array_equal(self.Current[Name][1], Knots)

    def testVisibleSelection(self):
        Visible = {C["code"] for C in self.Metadata["curves"]}
        self.assertEqual(len(Visible), 37)
        self.assertEqual(self.Metadata["visibleCurveCount"], 53)
        self.assertTrue({"C01", "C02", "B20", "B22", "G23", "R01", "R02"} <= Visible)
        self.assertFalse(set(self.Metadata["rejected"] + self.Metadata["dependentExcluded"]) & Visible)
        for Curve in self.Metadata["curves"]:
            self.assertTrue(Curve["defaultVisible"])
        Decisions = {D["code"]: D for D in self.Metadata["decisions"]}
        self.assertEqual(Decisions["G24"]["representative"], "B22")
        self.assertEqual(Decisions["G31"]["representative"], "C01")
        self.assertEqual(Decisions["G43"]["representative"], "B19")
        self.assertEqual(Decisions["G50"]["action"], "joined")
        for Decision in Decisions.values():
            if Decision["action"] == "parallel suppressed":
                self.assertIn(Decision["representative"], Visible)
                self.assertLessEqual(Decision["sampledMedianPathSeparationMm"], 40)

    def testOriginalSpansAndJunctions(self):
        OriginalNames = {N.split("_")[1]: N for N in self.Original if not N.endswith("_Right")}
        for Record in self.Metadata["curves"]:
            if not Record["code"].startswith("C"):
                continue
            Poles, Knots = self.Current[Record["names"][0]]
            Current = BSpline(Knots, Poles, 3)
            Stops = [0] + Record["joinParameters"] + [1]
            for Code, Reverse, A, B in zip(Record["connects"], Record["reversed"], Stops[:-1], Stops[1:]):
                P, K = self.Original[OriginalNames[Code]]
                Samples = np.linspace(0, 1, 513)
                np.testing.assert_allclose(
                    Current(A + (B - A) * Samples), BSpline(K, P, 3)(1 - Samples if Reverse else Samples), atol=2e-12
                )
            self.assertEqual(max(Record["joinGapsMm"]), 0)
            for Stop in Stops[1:-1]:
                self.assertEqual(np.count_nonzero(Knots == Stop), 3)
                self.assertLess(np.linalg.norm(Current(Stop - 1e-10) - Current(Stop + 1e-10)), 1e-7)

    def testBilateralJoins(self):
        for Record in self.Metadata["curves"]:
            if not Record["code"].startswith("C"):
                continue
            Left, Right = [self.Current[N] for N in Record["names"]]
            np.testing.assert_array_equal(Left[0] * [1, -1, 1], Right[0])
            np.testing.assert_array_equal(Left[1], Right[1])

    def testDisconnectedJoinRefused(self):
        Knots = np.r_[np.zeros(4), np.ones(4)]
        First = np.column_stack([np.linspace(0, 1, 4), np.zeros((4, 2))])
        with self.assertRaises(ValueError):
            Join([(First, Knots), (First + [3, 0, 0], Knots)])

    def testReversedJoinWithoutMutation(self):
        Knots = np.r_[np.zeros(4), np.ones(4)]
        First = np.column_stack([np.linspace(0, 1, 4), np.zeros((4, 2))])
        Second = (First + [1, 0, 0])[::-1].copy()
        Saved = Second.copy()
        Poles, Combined, Proof = Join([(First, Knots), (Second, Knots)])
        np.testing.assert_array_equal(Second, Saved)
        np.testing.assert_allclose(BSpline(Combined, Poles, 3)(np.linspace(0, 1, 101))[:, 0], np.linspace(0, 2, 101))
        self.assertEqual(Proof["reversed"], [False, True])


if __name__ == "__main__":
    unittest.main()

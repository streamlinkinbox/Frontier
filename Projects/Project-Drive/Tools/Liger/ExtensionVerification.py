#!/usr/bin/env python3
"""Independent source-coverage, coefficient-bound and connection checks for the first guide-aligned layout."""

import hashlib
import json
from pathlib import Path
import re
import unittest
import numpy as np
from scipy.interpolate import BSpline
from ConsolidationSequence import ReadCurves
from ExtensionSequence import Nearest, Spline
from MirrorSequence import Read
from RoofSequence import Crop


def Supports(Path):
    Lines = Path.read_text().splitlines()
    Names = [re.search(r"--name=(\S+)", L)[1] for L in Lines if L.startswith("patch ")]
    Patches = dict(zip(Names, Read(Path)))
    Sewn = [T for T in [L for L in Lines if L.startswith("sew ")][-1].split()[1:] if not T.startswith("--")]
    return [Patches[N] for N in Sewn]


class ExtensionVerification(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"
        cls.Source = Root / "Liger_Consolidated.arc"
        cls.Destination = Root / "Liger_Layout.arc"
        cls.Metadata = json.loads(cls.Destination.with_suffix(".guides.json").read_text())
        cls.Old = ReadCurves(cls.Source.read_text())
        cls.New = ReadCurves(cls.Destination.read_text())
        cls.Prior = Supports(cls.Source)
        cls.Current = Supports(cls.Destination)
        cls.ByCode = {C["code"]: C for C in cls.Metadata["curves"]}

    def testDocumentProvenance(self):
        self.assertEqual(hashlib.sha256(self.Source.read_bytes()).hexdigest(), self.Metadata["sourceSha256"])
        self.assertEqual(hashlib.sha256(self.Destination.read_bytes()).hexdigest(), self.Metadata["documentSha256"])
        self.assertTrue(self.Metadata["bodyChanged"])
        self.assertFalse(self.Metadata["geometryRefitted"])
        for Name, (Poles, Knots) in self.Old.items():
            np.testing.assert_array_equal(self.New[Name][0], Poles)
            np.testing.assert_array_equal(self.New[Name][1], Knots)

    def testExactSurfaceRestrictions(self):
        Maximum = 0.0
        Area = np.zeros(len(self.Prior))
        Seen = set()
        for Old, New, U0, U1, V0, V1 in self.Metadata["faceRestrictions"]:
            self.assertNotIn(New, Seen)
            Seen.add(New)
            Area[Old] += (U1 - U0) * (V1 - V0)
            Expected = self.Prior[Old]
            if U0 != 0 or U1 != 1:
                Expected = Crop(Expected, 0, U0, U1)
            if V0 != 0 or V1 != 1:
                Expected = Crop(Expected, 1, V0, V1)
            Actual = self.Current[New]
            np.testing.assert_array_equal(Expected.KnotsU, Actual.KnotsU)
            np.testing.assert_array_equal(Expected.KnotsV, Actual.KnotsV)
            Maximum = max(Maximum, float(np.linalg.norm(Expected.Poles - Actual.Poles, axis=2).max()))
        self.assertEqual(len(Seen), 1082)
        np.testing.assert_allclose(Area, 1, atol=1e-14)
        # Identical nonnegative partition-of-unity bases bound the error everywhere, not just at samples.
        self.assertLess(Maximum, 2e-14)
        print("CONTINUOUS_RESTRICTION_COEFFICIENT_BOUND_METRES", Maximum)

    def testNoOverlappingFaceInteriors(self):
        Rows = self.Metadata["faceRestrictions"]
        for Index, A in enumerate(Rows):
            for B in Rows[Index + 1 :]:
                if A[0] != B[0]:
                    continue
                Area = max(0, min(A[3], B[3]) - max(A[2], B[2])) * max(0, min(A[5], B[5]) - max(A[4], B[4]))
                self.assertEqual(Area, 0)

    def testExistingGuideSpansRetained(self):
        for Record in self.Metadata["curves"]:
            if "previousNames" not in Record:
                continue
            for Before, After in zip(Record["previousNames"], Record["names"]):
                P, K = self.Old[Before]
                Q, T = self.New[After]
                Found = False
                for Reverse in [False, True]:
                    Poles = P[::-1] if Reverse else P
                    for Start in range(len(Q) - len(P) + 1):
                        if not np.allclose(Q[Start : Start + len(P)], Poles, atol=2e-12, rtol=0):
                            continue
                        A, B = T[Start + 3], T[Start + len(P)]
                        Samples = np.linspace(0, 1, 1025)
                        np.testing.assert_allclose(
                            BSpline(T, Q, 3)(A + (B - A) * Samples),
                            BSpline(K, P, 3)(1 - Samples if Reverse else Samples),
                            atol=3e-12,
                            rtol=0,
                        )
                        Found = True
                self.assertTrue(Found, Before)

    def testConnectedEndpoints(self):
        for Code, Ends, Target in [
            ("B41", [0, 1], "B03"),
            ("B42", [0, 1], "B03"),
            ("C01", [1], "B03"),
            ("B17", [0, 1], "B22"),
            ("B52", [1], "B17"),
        ]:
            Curve = Spline(self.New[self.ByCode[Code]["names"][0]])
            Other = Spline(self.New[self.ByCode[Target]["names"][0]])
            for End in Ends:
                Point = Curve(End)
                if Target == "B22":
                    Point[1] = abs(Point[1])
                self.assertLess(Nearest(Other, Point)[1], 2e-7, Code)
        Side = Spline(self.New[self.ByCode["B45"]["names"][0]])
        Roof = Spline(self.New[self.ByCode["B29"]["names"][0]])
        np.testing.assert_allclose(Side(0), Roof(0), atol=1e-12)
        Belt = Spline(self.New[self.ByCode["R02"]["names"][0]])
        for End, Target in [(0, "C02"), (1, "B22")]:
            self.assertLess(Nearest(Spline(self.New[self.ByCode[Target]["names"][0]]), Belt(End))[1], 2e-7)

    def testMirroredFaceLayout(self):
        Names = [
            T
            for T in [L for L in self.Destination.read_text().splitlines() if L.startswith("sew ")][-1].split()[1:]
            if not T.startswith("--")
        ]
        Surfaces = dict(zip(Names, self.Current))
        for Index, Axis in [(14, 0), (156, 1), (164, 0), (302, 0)]:
            for Piece in [0, 1]:
                Left = Surfaces[f"Layout_Main_{Index:04d}_{Piece}"]
                Right = Surfaces[f"Layout_Main_{Index+1:04d}_{1-Piece if Axis == 0 else Piece}"]
                np.testing.assert_allclose(Left.Poles[::-1] * [1, -1, 1], Right.Poles, atol=2e-13, rtol=0)
                np.testing.assert_allclose(1 - Left.KnotsU[::-1], Right.KnotsU, atol=1e-13, rtol=0)
                np.testing.assert_allclose(Left.KnotsV, Right.KnotsV, atol=1e-13, rtol=0)

    def testFullWidthExtensionReflection(self):
        for Code in ["B41", "B42", "B17"]:
            Curve = Spline(self.New[self.ByCode[Code]["names"][0]])
            T = np.linspace(0, 1, 4097)
            np.testing.assert_allclose(Curve(T) * [1, -1, 1], Curve(1 - T), atol=1e-10, rtol=0)

    def testMirroredGuidePoles(self):
        for Curve in self.Metadata["curves"]:
            if len(Curve["names"]) != 2:
                continue
            A, B = [self.New[N] for N in Curve["names"]]
            np.testing.assert_array_equal(A[0] * [1, -1, 1], B[0])
            np.testing.assert_array_equal(A[1], B[1])


if __name__ == "__main__":
    unittest.main()

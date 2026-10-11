#!/usr/bin/env python3
"""Independent polynomial-parent distance bounds and provenance checks for the live roof offset."""

import csv
import hashlib
import json
from pathlib import Path
import unittest

import numpy as np
from scipy.interpolate import PPoly
from ConsolidationSequence import ReadCurves

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"
Prefix = "Liger_Surface_Offset"


def ChordDistances(Poles, Knots, Queries):
    Pieces = [PPoly.from_spline((Knots, Poles[:, Axis], 3)) for Axis in range(3)]
    Spans = []
    for Index in range(len(Knots) - 1):
        Length = Knots[Index + 1] - Knots[Index]
        if Length <= 0:
            continue
        Coefficients = np.stack([Piece.c[:, Index] for Piece in Pieces], axis=-1)
        Coefficients *= np.array([Length**3, Length**2, Length, 1])[:, None]
        A, B, C, D = Coefficients
        Controls = np.array([D, D + C / 3, D + 2 * C / 3 + B / 3, D + C + B + A])
        Spans.append((Coefficients, Controls.min(axis=0), Controls.max(axis=0)))
    Minimum = np.array([Span[1] for Span in Spans])
    Maximum = np.array([Span[2] for Span in Spans])
    Distances = []
    for Query in Queries:
        Bounds = np.linalg.norm(np.maximum(Minimum, np.minimum(Maximum, Query)) - Query, axis=1)
        Best = np.inf
        for Index in np.argsort(Bounds):
            if Bounds[Index] > Best + 1e-10:
                break
            Coefficients = Spans[Index][0][::-1].copy()
            Coefficients[0] -= Query
            Derivative = np.zeros(6)
            for Axis in range(3):
                Derivative += np.polynomial.polynomial.polymul(
                    Coefficients[:, Axis], Coefficients[1:, Axis] * np.arange(1, 4)
                )
            Roots = np.polynomial.polynomial.polyroots(Derivative)
            Parameters = [0.0, 1.0] + [R.real for R in Roots if abs(R.imag) < 1e-8 and 0 <= R.real <= 1]
            for Parameter in Parameters:
                Best = min(Best, np.linalg.norm(np.polynomial.polynomial.polyval(Parameter, Coefficients)))
        Distances.append(float(Best))
    return Distances


class SurfaceOffsetVerification(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.Source = Root / "Liger_Layout.arc"
        cls.Document = Root / f"{Prefix}.arc"
        cls.Metadata = json.loads((Root / f"{Prefix}.json").read_text())
        cls.Measures = json.loads((Root / f"{Prefix}_SurfaceOffsetMeasures.json").read_text())
        with (Root / f"{Prefix}_SurfaceOffsetSamples.csv").open() as Stream:
            cls.Rows = list(csv.DictReader(Stream))
        if any(any(Value is None for Value in Row.values()) for Row in cls.Rows):
            raise ValueError("Native station export is incomplete")
        Poles, Knots = ReadCurves(cls.Source.read_text())[cls.Metadata["parent"]]
        Queries = [np.array([float(Row["offset" + Axis]) for Axis in "XYZ"]) for Row in cls.Rows]
        cls.Distances = ChordDistances(Poles, Knots, Queries)

    def testUnmodifiedModelAndLiveRecipe(self):
        Source = self.Source.read_text()
        Current = self.Document.read_text()
        self.assertTrue(Current.startswith(Source))
        self.assertEqual(hashlib.sha256(self.Source.read_bytes()).hexdigest(), self.Metadata["sourceSha256"])
        self.assertEqual(hashlib.sha256(self.Document.read_bytes()).hexdigest(), self.Metadata["documentSha256"])
        Commands = [Line for Line in Current[len(Source) :].splitlines() if Line and not Line.startswith("#")]
        self.assertEqual(Commands[0], "require surface-offset")
        self.assertEqual(len(Commands), 2)
        self.assertEqual(
            Commands[1],
            "surface-offset Guide_B03_UpperCanopy_Across Liger_Main_Body -0.025 --name=Guide_O01_RoofSurfaceOffset_Across",
        )
        self.assertFalse(self.Metadata["bodyChanged"])
        self.assertFalse(self.Metadata["parentCurveChanged"])
        self.assertFalse(self.Metadata["orangeRepairsChanged"])

    def testMeasuredWidthChoice(self):
        Widths = []
        for Values in self.Metadata["stripLengthsMetres"].values():
            self.assertEqual(len(Values), 65)
            Widths.extend(Values)
        self.assertAlmostEqual(float(np.median(Widths)), self.Metadata["stripMedianMetres"], places=12)
        self.assertEqual(round(float(np.median(Widths)), 3), abs(self.Metadata["signedDistanceMetres"]))
        self.assertLess(self.Metadata["stripLengthRefinementMetres"], 1e-9)

    def testGlobalParentChordLowerBoundAtStations(self):
        # All polynomial spans and their distance-stationary roots are considered, with Bezier-hull pruning.
        # Euclidean distance is a lower bound on intrinsic distance, not an intrinsic distance measurement.
        self.assertEqual(len(self.Distances), self.Measures["intervals"] + 1)
        self.assertGreater(len(self.Distances), 250)
        self.assertGreaterEqual(min(self.Distances), 0.025 - 0.000005)
        self.assertLessEqual(max(self.Distances), 0.025 + 0.00005)
        Report = {
            "stationCount": len(self.Distances),
            "minimumChordMetres": min(self.Distances),
            "maximumChordMetres": max(self.Distances),
            "method": "Stationary quintic roots on every polynomial parent span, with Bernstein control-hull pruning.",
            "scope": "Numerical Euclidean lower bound on intrinsic distance at exported stations only; not a continuous or exact shortest-geodesic certificate.",
        }
        (Root / f"{Prefix}_ChordBounds.json").write_text(json.dumps(Report, indent=2) + "\n")
        print("INDEPENDENT_PARENT_CHORD_RANGE_MM", min(self.Distances) * 1000, max(self.Distances) * 1000)

    def testNativeRefinementMeasures(self):
        self.assertLessEqual(self.Measures["marchRefinementMetres"], 0.00001)
        self.assertLessEqual(self.Measures["sampledInterpolationMetres"], 0.000025)
        self.assertLessEqual(self.Measures["sourceSupportDiscrepancyMetres"], 0.00005)
        self.assertLessEqual(self.Measures["cornerRefinementMetres"], 0.00001)
        self.assertLessEqual(self.Measures["cornerDistanceResidualMetres"], 0.00001)
        self.assertLessEqual(self.Measures["bilateralDiscrepancyMetres"], 0.0001)


if __name__ == "__main__":
    unittest.main()

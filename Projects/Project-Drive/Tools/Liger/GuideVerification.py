#!/usr/bin/env python3
"""Numerical regressions for candidate relations, exact trace composition and surface-seed rays."""

import unittest
import numpy as np
from scipy.interpolate import BSpline
from GuideSequence import Families, Hermite, Parallel, SurfaceProjection, TraceSpline


class GuideVerification(unittest.TestCase):
    def testParallelAndCrossing(self):
        X = np.linspace(0, 1, 501)
        First = np.column_stack([X, X * 0, X * 0])
        Second = First + [0, 0.02, 0]
        Fraction, Separation, Alignment = Parallel(First, Second)
        self.assertGreater(Fraction, 0.98)
        self.assertAlmostEqual(Separation, 0.02)
        self.assertGreater(Alignment, 0.99)
        Cross = np.column_stack([X * 0 + 0.5, X - 0.5, X * 0])
        self.assertLess(Parallel(First, Cross)[0], 0.05)
        self.assertEqual(Parallel(First, First)[0], 0)

    def testFarCurvesNotParallelCandidates(self):
        First = np.column_stack([np.linspace(0, 1, 101), np.zeros((101, 2))])
        self.assertEqual(Parallel(First, First + [0, 0.25, 0])[0], 0)

    def testMirroredFamilies(self):
        First = np.array([[0, 0.5, 0], [1, 0.5, 0]])
        Second = First * [1, -1, 1]
        Result = Families([{"points": First, "lengthMetres": 1.0}, {"points": Second[::-1], "lengthMetres": 1.0}])
        self.assertEqual(len(Result), 1)
        self.assertEqual(Result[0]["mirroredChains"], [1])

    def testExactReversedTrace(self):
        Knots = np.array([0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0])
        First = np.array([[0, 0, 0], [0.2, 0.1, 0], [0.4, 0.1, 0], [0.5, 0, 0]])
        Second = np.array([[1, 0, 0], [0.8, -0.1, 0], [0.7, -0.1, 0], [0.5, 0, 0]])
        Edges = []
        for Poles in [First, Second]:
            Edges.append(
                {
                    "degree": 3,
                    "knots": Knots.tolist(),
                    "poles": np.column_stack([Poles, np.ones(4)]).tolist(),
                    "points": BSpline(Knots, Poles, 3)(np.linspace(0, 1, 17)).tolist(),
                }
            )
        Poles, Combined, Metrics = TraceSpline({"edges": [(0, 0), (1, 1)]}, Edges)
        self.assertEqual(Metrics["sourceJoinMaximumMm"], 0)
        Curve = BSpline(Combined, Poles, 3)
        Split = Combined[4]
        Parameters = np.linspace(0, 1, 101)
        np.testing.assert_allclose(Curve(Parameters * Split), BSpline(Knots, First, 3)(Parameters), atol=1e-13)
        np.testing.assert_allclose(
            Curve(Split + (1 - Split) * Parameters), BSpline(Knots, Second, 3)(1 - Parameters), atol=1e-13
        )

    def testSurfaceSeedRayAndMissingSkin(self):
        Triangles = np.array(
            [[[0, 0, 1], [1, 0, 1], [0, 1, 1]], [[1, 0, 1], [1, 1, 1], [0, 1, 1]], [[0, 0, 0], [1, 0, 0], [0, 1, 0]]],
            dtype=float,
        )
        Projection = SurfaceProjection(Triangles)
        np.testing.assert_allclose(
            Projection.Ray(np.array([[0.2, 0.3], [0.8, 0.7]]), (0, 1)), [[0.2, 0.3, 1], [0.8, 0.7, 1]]
        )
        with self.assertRaises(ValueError):
            Projection.Ray(np.array([[2.0, 2.0]]), (0, 1))

    def testJoinEndpoints(self):
        Begin, End = np.array([0.0, 0.0, 0.0]), np.array([1.0, 0.3, 0.2])
        Points = Hermite(Begin, End, np.array([1.0, 0.0, 0.0]), np.array([1.0, 0.0, 0.0]))
        np.testing.assert_array_equal(Points[0], Begin)
        np.testing.assert_array_equal(Points[-1], End)
        self.assertLess(abs(Points[1, 1]), 1e-4)


if __name__ == "__main__":
    unittest.main()

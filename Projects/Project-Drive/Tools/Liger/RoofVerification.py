#!/usr/bin/env python3
"""Small independent numerical regressions for the roof replacement helpers."""

import unittest
import numpy as np
from scipy.interpolate import BSpline
from PatchSequence import Patch
from RoofSequence import AlignedStops, Crop, Cut, Evaluate, Gradient, JoinCurves, Mirror, Parameters, Reverse


class RoofVerification(unittest.TestCase):
    def setUp(self):
        self.Knots = np.array([0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0])
        self.Curve = BSpline(self.Knots, np.array([[0, 0, 0], [0.3, 0.2, 0.1], [0.6, -0.1, 0.2], [1, 0, 0.3]]), 3)
        self.Parameters = np.linspace(0, 1, 97)
        Poles = np.array([[[U, V, 0.08 * U + 0.03 * V] for V in np.linspace(0, 1, 4)] for U in np.linspace(0, 1, 4)])
        self.Surface = Patch(Poles, self.Knots, self.Knots, np.zeros((2, 2), int), {})

    def testExactCurveRestriction(self):
        Restricted = Cut(self.Curve, 0.17, 0.79)
        np.testing.assert_allclose(Restricted(self.Parameters), self.Curve(0.17 + 0.62 * self.Parameters), atol=1e-13)

    def testCurveReversal(self):
        np.testing.assert_allclose(Reverse(self.Curve)(self.Parameters), self.Curve(1 - self.Parameters), atol=1e-13)

    def testConcatenationRetainsCurvedGeometry(self):
        First, Second = Cut(self.Curve, 0, 0.37), Cut(self.Curve, 0.37, 1)
        Joined, Stops = JoinCurves([(0, First), (1, Second)], np.array([0, 0.2, 1.0]))
        np.testing.assert_allclose(Joined(0.2 * self.Parameters), First(self.Parameters), atol=1e-13)
        np.testing.assert_allclose(Joined(0.2 + 0.8 * self.Parameters), Second(self.Parameters), atol=1e-13)
        self.assertEqual(sum(Joined.t == Stops[1]), 3)

    def testNonCoincidentConcatenationRefused(self):
        Other = BSpline(self.Knots, self.Curve.c + np.array([1, 1, 1]), 3)
        with self.assertRaises(AssertionError):
            JoinCurves([(0, self.Curve), (1, Other)])

    def testAlignedStopsPreserveOrdering(self):
        First, Second = AlignedStops(np.array([0, 0.201, 0.5, 1]), np.array([0, 0.2, 0.4, 0.503, 1]), 0.01)
        self.assertEqual(First[1], Second[1])
        self.assertEqual(First[2], Second[3])
        self.assertTrue(np.all(np.diff(First) > 0) and np.all(np.diff(Second) > 0))

    def testTensorRestriction(self):
        Restricted = Crop(Crop(self.Surface, 0, 0.2, 0.8), 1, 0.3, 0.9)
        np.testing.assert_allclose(
            Evaluate(Restricted, self.Parameters, self.Parameters),
            Evaluate(self.Surface, 0.2 + 0.6 * self.Parameters, 0.3 + 0.6 * self.Parameters),
            atol=1e-13,
        )

    def testMirrorAndInvolution(self):
        Other = Mirror(self.Surface)
        Expected = Evaluate(self.Surface, self.Parameters, self.Parameters)
        Expected[:, 1] *= -1
        np.testing.assert_allclose(Evaluate(Other, 1 - self.Parameters, self.Parameters), Expected, atol=1e-13)
        np.testing.assert_array_equal(Mirror(Other).Poles, self.Surface.Poles)

    def testPhysicalInverseAndGradient(self):
        Coordinates = np.random.default_rng(72).uniform(0, 1, (123, 2))
        Points = Evaluate(self.Surface, *Coordinates.T)
        np.testing.assert_allclose(Parameters(self.Surface, Points[:, :2]), Coordinates, atol=1e-12)
        np.testing.assert_allclose(
            Gradient(self.Surface, Points[:, :2]), np.tile([0.08, 0.03], (len(Points), 1)), atol=1e-12
        )


if __name__ == "__main__":
    unittest.main()

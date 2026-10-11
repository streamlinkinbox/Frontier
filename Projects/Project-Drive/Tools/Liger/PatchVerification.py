#!/usr/bin/env python3
"""Focused numerical checks for conservative displacement bounds and chart rotations."""
import unittest
import numpy as np
from scipy.interpolate import BSpline
from PatchSequence import BernsteinBound, Patch, Join
from BodySolver import Quantize
from collections import Counter


class PatchVerification(unittest.TestCase):
    def test_interior_oscillation_is_bounded(self):
        Difference = np.zeros((4, 4, 3))
        Difference[1, 1, 0] = 1.
        Bound = BernsteinBound(Difference, np.eye(4), np.eye(4), .2)
        self.assertGreaterEqual(Bound, (4/9)**2)
        self.assertLessEqual(Bound, .2)
        self.assertTrue(np.isinf(BernsteinBound(Difference, np.eye(4), np.eye(4), .1)))

    def test_constant_displacement(self):
        Difference = np.ones((4, 4, 3))*.02
        self.assertAlmostEqual(BernsteinBound(Difference, np.eye(4), np.eye(4), .1), np.sqrt(3)*.02)

    def test_rotation_and_join_preserve_parameter_positions(self):
        Knots = np.r_[np.zeros(4), np.ones(4)]
        Points = np.zeros((4, 4, 3))
        Points[:, :, 0] = np.linspace(0, 1, 4)[:, None]
        Points[:, :, 1] = np.linspace(0, 1, 4)[None, :]
        A = Patch(Points, Knots, Knots, np.array([[0, 1], [2, 3]]), {0: np.eye(3)})
        B = Patch(Points+[1., 0., 0.], Knots, Knots, np.array([[2, 3], [4, 5]]), {1: np.eye(3)})
        for P in [A.Rotate(), A.Rotate().Rotate(), Join(A, B)]:
            for Identity, Transform in P.Charts.items():
                U, V, _ = Transform@np.array([.2, .7, 1.])
                Bu = BSpline(P.KnotsU, np.eye(P.Poles.shape[0]), 3)(U)
                Bv = BSpline(P.KnotsV, np.eye(P.Poles.shape[1]), 3)(V)
                Position = np.einsum('i,ijc,j->c', Bu, P.Poles, Bv)
                np.testing.assert_allclose(Position, [Identity+.2, .7, 0.], atol=1e-14)

    def test_rational_knots_survive_unequal_joins_and_rotations(self):
        Knots = np.r_[np.zeros(4), np.ones(4)]
        Points = np.zeros((4, 4, 3))
        Points[:, :, 0] = np.linspace(0, 1, 4)[:, None]
        Points[:, :, 1] = np.linspace(0, 1, 4)[None, :]
        Joined = Patch(Points, Knots, Knots, np.array([[0, 1], [2, 3]]), {0: np.eye(3)})
        for Identity in range(1, 8):
            Next = Patch(Points+[Identity, 0, 0], Knots, Knots,
                         np.array([[2*Identity, 2*Identity+1], [2*Identity+2, 2*Identity+3]]), {Identity: np.eye(3)})
            Joined = Quantize(Join(Joined, Next))
            for Rotation in range(4):
                Joined = Quantize(Joined.Rotate())
            for Direction in (Joined.KnotsU, Joined.KnotsV):
                self.assertTrue(all(Multiplicity <= 3 for Knot, Multiplicity in Counter(Direction).items() if 0 < Knot < 1))
            for Chart, Transform in Joined.Charts.items():
                Along, Across, _ = Transform@np.array([.2, .7, 1.])
                BasisU = BSpline(Joined.KnotsU, np.eye(Joined.Poles.shape[0]), 3)(Along)
                BasisV = BSpline(Joined.KnotsV, np.eye(Joined.Poles.shape[1]), 3)(Across)
                Position = np.einsum('i,ijc,j->c', BasisU, Joined.Poles, BasisV)
                np.testing.assert_allclose(Position, [Chart+.2, .7, 0], atol=1e-12)

    def test_arbitrary_knots_are_not_silently_quantized(self):
        Knots = np.r_[np.zeros(4), .7123456789, np.ones(4)]
        Candidate = Patch(np.zeros((5, 5, 3)), Knots, Knots, np.array([[0, 1], [2, 3]]), {0: np.eye(3)})
        with self.assertRaises(RuntimeError):
            Quantize(Candidate)


if __name__ == '__main__':
    unittest.main()

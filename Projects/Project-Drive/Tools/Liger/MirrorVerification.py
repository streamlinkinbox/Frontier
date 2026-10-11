#!/usr/bin/env python3
"""Numerical regression checks for rectangular restriction and orientation-preserving reflection."""
import unittest
from copy import deepcopy
import numpy as np
from PatchSequence import Patch, Join
from MirrorSequence import Canonical, Restrict, Reflect, Points, DifferenceBound, Pair


class MirrorVerification(unittest.TestCase):
    def Surface(self):
        Knots = np.r_[np.zeros(4), np.ones(4)]
        Poles = np.zeros((4, 4, 3))
        Poles[:, :, 0] = np.linspace(0, 1, 4)[:, None]
        Poles[:, :, 1] = np.linspace(.2, 1.2, 4)[None, :]
        Poles[:, :, 2] = np.arange(16).reshape(4, 4)**2*.0001
        return Patch(Poles, Knots, Knots, np.array([[0, 1], [2, 3]]), {0: np.eye(3)})

    def Coordinates(self):
        U, V = np.meshgrid(np.linspace(0, 1, 19), np.linspace(0, 1, 17), indexing='ij')
        return np.stack([U.ravel(), V.ravel(), np.ones(U.size)])

    def test_rectangular_restriction_inserts_knots(self):
        Surface = self.Surface()
        Transform = np.array([[.25, 0, .125], [0, .5, .25], [0, 0, 1.]])
        Restricted = Restrict(Surface, Transform, 0)
        Coordinates = self.Coordinates()
        np.testing.assert_allclose(Points(Restricted, Coordinates), Points(Surface, Transform@Coordinates), atol=1e-13)

    def test_rotated_restriction_preserves_original_parameters(self):
        Surface = self.Surface()
        Coordinates = self.Coordinates()
        Rotation = np.array([[0., -1., 1.], [1., 0., 0.], [0., 0., 1.]])
        Transform = np.array([[.25, 0, .125], [0, .5, .25], [0, 0, 1.]])
        for _ in range(4):
            Restricted = Restrict(Surface, Transform, 0)
            np.testing.assert_allclose(Points(Restricted, Coordinates), Points(Surface, Transform@Coordinates), atol=1e-13)
            Transform = Transform@Rotation

    def test_join_then_restrict_recovers_both_splines(self):
        First = self.Surface()
        Second = deepcopy(First)
        Second.Poles[:, :, 0] += 1
        Second.Poles[0] = First.Poles[-1]
        Second.Corners = np.array([[2, 3], [4, 5]])
        Second.Charts = {1: np.eye(3)}
        Joined = Join(First, Second)
        for Identity, Surface in enumerate((First, Second)):
            Recovered = Restrict(Joined, Joined.Charts[Identity], Identity)
            self.assertLess(DifferenceBound(Recovered, Surface), 1e-13)

    def test_reflection_involution(self):
        Surface = self.Surface()
        Twice = Reflect(Reflect(Surface))
        np.testing.assert_array_equal(Twice.Poles, Surface.Poles)
        np.testing.assert_array_equal(Twice.KnotsU, Surface.KnotsU)

    def test_reflection_preserves_outward_orientation(self):
        Surface = self.Surface()
        Mirrored = Reflect(Surface)
        Coordinates = self.Coordinates()
        Reverse = Coordinates.copy()
        Reverse[0] = 1-Reverse[0]
        Expected = Points(Surface, Reverse)
        Expected[:, 1] *= -1
        np.testing.assert_allclose(Points(Mirrored, Coordinates), Expected, atol=1e-13)
        def Normal(PatchSurface, U):
            Epsilon = 1e-5
            Samples = Points(PatchSurface, np.array([[U-Epsilon, U+Epsilon, U, U], [.5, .5, .5-Epsilon, .5+Epsilon], [1, 1, 1, 1]]))
            return np.cross(Samples[1]-Samples[0], Samples[3]-Samples[2])
        ExpectedNormal = Normal(Surface, .6)
        ExpectedNormal[1] *= -1
        np.testing.assert_allclose(Normal(Mirrored, .4), ExpectedNormal, atol=1e-16)

    def test_pair_recovers_all_four_reflected_parameter_orientations(self):
        First = self.Surface()
        Second = Reflect(First)
        for _ in range(4):
            Pairs, Bound = Pair([First, Second])
            self.assertLess(Bound, 1e-13)
            Negative, Transform = Pairs[0]
            self.assertEqual(Negative, 1)
            Expected = Points(First, self.Coordinates())
            Expected[:, 1] *= -1
            np.testing.assert_allclose(Points(Second, Transform@self.Coordinates()), Expected, atol=1e-13)
            Second = Second.Rotate()

    def test_unequal_or_missing_half_is_rejected(self):
        with self.assertRaises(RuntimeError):
            Pair([self.Surface()])

    def test_coefficient_bound_includes_interior_displacement(self):
        First = self.Surface()
        Second = deepcopy(First)
        Second.Poles[1, 2, 2] += .01
        Bound = DifferenceBound(First, Second)
        SampleMaximum = np.linalg.norm(Points(First, self.Coordinates())-Points(Second, self.Coordinates()), axis=1).max()
        self.assertGreaterEqual(Bound, SampleMaximum)
        self.assertAlmostEqual(Bound, .01)

    def test_non_rational_knots_are_rejected(self):
        with self.assertRaises(ValueError):
            Canonical(np.array([0, .123456789123, 1]))


if __name__ == '__main__':
    unittest.main()

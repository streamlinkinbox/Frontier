#!/usr/bin/env python3
"""Regressions for native tangent-chain following, symmetric fitting and crown-to-lower-fade geometry."""
import unittest
import numpy as np
from scipy.interpolate import BSpline
from ContourSequence import Trace, Locate, Fit, Fade, Resample, Circle, NegativeBernstein


def Edge(Start, End, First, Last):
    return {'start': Start, 'end': End, 'points': np.linspace(First, Last, 17).tolist()}


class ContourVerification(unittest.TestCase):
    def test_crossing_does_not_turn_into_a_transverse_patch_edge(self):
        Edges = [Edge(0, 1, [-1, 0, 0], [0, 0, 0]), Edge(1, 2, [0, 0, 0], [1, 0, 0]),
                 Edge(3, 1, [0, -1, 0], [0, 0, 0]), Edge(1, 4, [0, 0, 0], [0, 1, 0])]
        Chains = Trace(Edges)
        self.assertEqual(sorted(sorted(Index for Index, End in Chain['edges']) for Chain in Chains), [[0, 1], [2, 3]])

    def test_stair_step_is_not_accepted_as_a_long_feature(self):
        Chains = Trace([Edge(0, 1, [0, 0, 0], [1, 0, 0]), Edge(1, 2, [1, 0, 0], [1, 1, 0]),
                        Edge(2, 3, [1, 1, 0], [2, 1, 0])])
        self.assertEqual(len(Chains), 3)
        with self.assertRaises(ValueError):
            Locate(Chains, [1, 0, 0], 2)

    def test_missing_geometric_seed_refuses(self):
        Chains = Trace([Edge(0, 1, [0, 0, 0], [1, 0, 0])])
        with self.assertRaises(ValueError):
            Locate(Chains, [0, 1, 0], .5)

    def test_closed_cycle_is_traced_once(self):
        Angles = np.linspace(0, 2*np.pi, 73)
        Points = np.column_stack([np.cos(Angles), np.sin(Angles), np.zeros(len(Angles))])
        Chains = Trace([Edge(Index, (Index+1)%72, Points[Index], Points[Index+1]) for Index in range(72)])
        self.assertEqual(len(Chains), 1)
        self.assertEqual(len(Chains[0]['edges']), 72)

    def test_symmetric_fit_has_reflected_control_poles(self):
        Along = np.linspace(-1, 1, 257)
        Poles, Metrics = Fit(np.column_stack([1-Along**2, Along, .2*Along**2]), True)
        Reflected = Poles[::-1].copy(); Reflected[:, 1] *= -1
        np.testing.assert_array_equal(Poles, Reflected)
        self.assertLessEqual(Metrics['heldOutMaximumFitMm'], 1)

    def test_false_symmetry_refuses(self):
        with self.assertRaises(ValueError):
            Fit(np.column_stack([np.arange(10), np.arange(10), np.zeros(10)]), True)

    def test_fade_matches_circle_curvature_and_tends_to_straight(self):
        Radius = .45
        Angles = np.radians(np.linspace(90, -35, 501))
        Source = np.column_stack([Radius*np.cos(Angles), np.ones(len(Angles)), .43+Radius*np.sin(Angles)])
        Poles, Metrics = Fade(0, .43, Radius, 1, 25, Source, True)
        Curve = BSpline(np.r_[np.zeros(6), np.ones(6)], Poles, 5)
        np.testing.assert_allclose(Curve(0), [Radius*np.cos(np.radians(25)), 1, .43+Radius*np.sin(np.radians(25))])
        self.assertAlmostEqual(Metrics['startCurvaturePerMetre'], 1/Radius, places=10)
        self.assertLess(Metrics['endCurvaturePerMetre'], 1e-10)
        self.assertTrue(np.all(Curve(np.linspace(0, 1, 1001), nu=1)[:, 2] < 0))

    def test_independent_radii_do_not_collapse_inner_and_outer(self):
        Angles = np.radians(np.linspace(20, 160, 2001))
        for Radius in (.45, .49):
            Points = np.column_stack([1.7+Radius*np.cos(Angles), np.ones(len(Angles)), .43+Radius*np.sin(Angles)])
            Parameters, Metrics = Circle(Points, 1.7)
            np.testing.assert_allclose(Parameters, [1.7, .43, Radius], atol=2e-7)
            self.assertLess(Metrics['sourceCrownMaximumRadialMm'], .001)

    def test_negative_derivative_can_be_certified_after_subdivision(self):
        self.assertTrue(NegativeBernstein(np.array([-1., .1, -1.])))

    def test_derivative_with_a_positive_interior_is_rejected(self):
        self.assertFalse(NegativeBernstein(np.array([-1., 3., -1.])))

    def test_nonfinite_derivative_is_rejected(self):
        self.assertFalse(NegativeBernstein(np.array([-1., np.nan, -1.])))

    def test_resampling_does_not_weight_duplicate_knots(self):
        Points = np.array([[0, 0, 0], [0, 0, 0], [1, 0, 0], [1, 0, 0], [2, 0, 0]])
        np.testing.assert_allclose(Resample(Points, 5)[:, 0], np.linspace(0, 2, 5))


if __name__ == '__main__':
    unittest.main()

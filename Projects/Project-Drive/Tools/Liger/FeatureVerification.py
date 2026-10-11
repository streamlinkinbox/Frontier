#!/usr/bin/env python3
"""Projection regressions: use the visible surface, refuse strokes outside it, preserve coordinates."""
import unittest
import numpy as np
from FeatureSequence import ProjectStroke, Pixel


class FeatureVerification(unittest.TestCase):
    def test_top_face_wins_over_nearby_small_underside(self):
        Triangles = np.array([[[0, 0, 0], [.1, 0, 0], [0, .1, 0]],
                              [[-10, -10, 2], [10, -10, 2], [0, 10, 2]]], dtype=float)
        Triangles = np.concatenate([np.repeat(Triangles[:1], 128, axis=0), Triangles[1:]])
        Projection = np.array([[1, 0], [0, 1], [0, 0], [0, 0]])
        np.testing.assert_allclose(ProjectStroke(Triangles, Projection, [[.02, .02]]), [[.02, .02, 2]])

    def test_sloped_triangle_preserves_screen_location(self):
        Triangles = np.array([[[0, 0, 0], [1, 0, 1], [0, 1, 2]]], dtype=float)
        Projection = np.array([[2, 0], [0, -3], [0, 0], [10, 20]])
        np.testing.assert_allclose(ProjectStroke(Triangles, Projection, [[10.4, 19.1]]), [[.2, .3, .8]])

    def test_outside_surface_is_not_silently_projected_to_an_edge(self):
        Triangles = np.array([[[0, 0, 0], [1, 0, 1], [0, 1, 2]]], dtype=float)
        Projection = np.array([[1, 0], [0, 1], [0, 0], [0, 0]])
        with self.assertRaises(RuntimeError):
            ProjectStroke(Triangles, Projection, [[2, 2]])

    def test_annotation_origin_maps_to_native_tile_origin(self):
        np.testing.assert_array_equal(Pixel([[784, 580]]), [[0, 0]])
        np.testing.assert_allclose(Pixel([[1568, 1070]]), [[1600, 1000]])


if __name__ == '__main__':
    unittest.main()

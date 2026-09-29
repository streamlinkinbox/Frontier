// High-Resolution Welded 3D SDF Marching Cubes (256x256x128)
// Uses a 2-Z-Slice Rolling Edge Buffer (<0.8 MB memory), Slope-Adaptive Taubin Relaxation,
// Blended 3D SDF Gradient Normals, and an optional 3D Geological Cutaway Block Skirt.

import { SDFTerrainVolume, getTotalSurfaceHeight } from './terrainState';
import { sample3DStrataProfile } from './erosion';
import { SATMAP_PRESETS, sampleRamp } from './satmaps';
import { SeededNoise } from './noise';

export interface ExtractedSDFMesh {
  positions: Float32Array;
  normals: Float32Array;
  colors: Float32Array;
  indices: Uint32Array;
  vertexCount: number;
  triangleCount: number;
}

const EDGE_TABLE = new Int32Array([
  0x0, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
  0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
  0x190, 0x99, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
  0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
  0x230, 0x339, 0x33, 0x13a, 0x636, 0x73f, 0x435, 0x53c,
  0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
  0x3a0, 0x2a9, 0x1a3, 0xaa, 0x7a6, 0x6af, 0x5a5, 0x4ac,
  0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
  0x460, 0x569, 0x663, 0x76a, 0x66, 0x16f, 0x265, 0x36c,
  0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
  0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff, 0x3f5, 0x2fc,
  0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
  0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55, 0x15c,
  0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
  0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc,
  0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
  0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
  0xcc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
  0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
  0x15c, 0x55, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
  0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
  0x2fc, 0x3f5, 0xff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
  0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
  0x36c, 0x265, 0x16f, 0x66, 0x76a, 0x663, 0x569, 0x460,
  0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
  0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa, 0x1a3, 0x2a9, 0x3a0,
  0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
  0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33, 0x339, 0x230,
  0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
  0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99, 0x190,
  0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
  0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
]);
// Fix octal literal 0859 -> 0x859 at index 145
EDGE_TABLE[145] = 0x859;

const TRI_TABLE: number[][] = [
  [-1],
  [0, 8, 3, -1],
  [0, 1, 9, -1],
  [1, 8, 3, 9, 8, 1, -1],
  [1, 2, 10, -1],
  [0, 8, 3, 1, 2, 10, -1],
  [9, 2, 10, 0, 2, 9, -1],
  [2, 8, 3, 2, 10, 8, 10, 9, 8, -1],
  [3, 11, 2, -1],
  [0, 11, 2, 8, 11, 0, -1],
  [1, 9, 0, 2, 3, 11, -1],
  [1, 11, 2, 1, 9, 11, 9, 8, 11, -1],
  [3, 10, 1, 11, 10, 3, -1],
  [0, 10, 1, 0, 8, 10, 8, 11, 10, -1],
  [3, 9, 0, 3, 11, 9, 11, 10, 9, -1],
  [9, 8, 10, 10, 8, 11, -1],
  [4, 7, 8, -1],
  [4, 3, 0, 7, 3, 4, -1],
  [0, 1, 9, 8, 4, 7, -1],
  [4, 1, 9, 4, 7, 1, 7, 3, 1, -1],
  [1, 2, 10, 8, 4, 7, -1],
  [3, 4, 7, 3, 0, 4, 1, 2, 10, -1],
  [9, 2, 10, 9, 0, 2, 8, 4, 7, -1],
  [2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1],
  [8, 4, 7, 3, 11, 2, -1],
  [11, 4, 7, 11, 2, 4, 2, 0, 4, -1],
  [9, 0, 1, 8, 4, 7, 2, 3, 11, -1],
  [4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1],
  [3, 10, 1, 3, 11, 10, 7, 8, 4, -1],
  [1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1],
  [4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1],
  [4, 7, 11, 4, 11, 9, 9, 11, 10, -1],
  [9, 5, 4, -1],
  [9, 5, 4, 0, 8, 3, -1],
  [0, 5, 4, 1, 5, 0, -1],
  [8, 5, 4, 8, 3, 5, 3, 1, 5, -1],
  [1, 2, 10, 9, 5, 4, -1],
  [3, 0, 8, 1, 2, 10, 4, 9, 5, -1],
  [5, 2, 10, 5, 4, 2, 4, 0, 2, -1],
  [2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1],
  [9, 5, 4, 2, 3, 11, -1],
  [0, 11, 2, 0, 8, 11, 4, 9, 5, -1],
  [0, 5, 4, 0, 1, 5, 2, 3, 11, -1],
  [2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1],
  [10, 3, 11, 10, 1, 3, 9, 5, 4, -1],
  [4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1],
  [5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1],
  [5, 4, 8, 5, 8, 10, 10, 8, 11, -1],
  [9, 7, 8, 5, 7, 9, -1],
  [9, 3, 0, 9, 5, 3, 5, 7, 3, -1],
  [0, 7, 8, 0, 1, 7, 1, 5, 7, -1],
  [1, 5, 3, 3, 5, 7, -1],
  [9, 7, 8, 9, 5, 7, 10, 1, 2, -1],
  [10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1],
  [8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1],
  [2, 10, 5, 2, 5, 3, 3, 5, 7, -1],
  [7, 9, 5, 7, 8, 9, 3, 11, 2, -1],
  [9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1],
  [2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1],
  [11, 2, 1, 11, 1, 7, 7, 1, 5, -1],
  [9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1],
  [5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1],
  [11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1],
  [11, 10, 5, 7, 11, 5, -1],
  [10, 6, 5, -1],
  [0, 8, 3, 5, 10, 6, -1],
  [9, 0, 1, 5, 10, 6, -1],
  [1, 8, 3, 1, 9, 8, 5, 10, 6, -1],
  [1, 6, 5, 2, 6, 1, -1],
  [1, 6, 5, 1, 2, 6, 3, 0, 8, -1],
  [9, 6, 5, 9, 0, 6, 0, 2, 6, -1],
  [5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1],
  [2, 3, 11, 10, 6, 5, -1],
  [11, 0, 8, 11, 2, 0, 10, 6, 5, -1],
  [0, 1, 9, 2, 3, 11, 5, 10, 6, -1],
  [5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1],
  [6, 3, 11, 6, 5, 3, 5, 1, 3, -1],
  [0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1],
  [3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1],
  [6, 5, 9, 6, 9, 11, 11, 9, 8, -1],
  [5, 10, 6, 4, 7, 8, -1],
  [4, 3, 0, 4, 7, 3, 6, 5, 10, -1],
  [1, 9, 0, 5, 10, 6, 8, 4, 7, -1],
  [10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1],
  [6, 1, 2, 6, 5, 1, 4, 7, 8, -1],
  [1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1],
  [8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1],
  [7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1],
  [3, 11, 2, 7, 8, 4, 10, 6, 5, -1],
  [5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1],
  [0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1],
  [9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1],
  [8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1],
  [5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1],
  [0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1],
  [6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1],
  [10, 4, 9, 6, 4, 10, -1],
  [4, 10, 6, 4, 9, 10, 0, 8, 3, -1],
  [10, 0, 1, 10, 6, 0, 6, 4, 0, -1],
  [8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1],
  [1, 4, 9, 1, 2, 4, 2, 6, 4, -1],
  [3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1],
  [0, 2, 4, 4, 2, 6, -1],
  [8, 3, 2, 8, 2, 4, 4, 2, 6, -1],
  [10, 4, 9, 10, 6, 4, 11, 2, 3, -1],
  [0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1],
  [3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1],
  [6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1],
  [9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1],
  [8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1],
  [3, 11, 6, 3, 6, 0, 0, 6, 4, -1],
  [6, 4, 8, 11, 6, 8, -1],
  [7, 10, 6, 7, 8, 10, 8, 9, 10, -1],
  [0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1],
  [10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1],
  [10, 6, 7, 10, 7, 1, 1, 7, 3, -1],
  [1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1],
  [2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1],
  [7, 8, 0, 7, 0, 6, 6, 0, 2, -1],
  [7, 3, 2, 6, 7, 2, -1],
  [2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1],
  [2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1],
  [1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1],
  [11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1],
  [8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1],
  [0, 9, 1, 11, 6, 7, -1],
  [7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1],
  [7, 11, 6, -1],
  [7, 6, 11, -1],
  [3, 0, 8, 11, 7, 6, -1],
  [0, 1, 9, 11, 7, 6, -1],
  [8, 1, 9, 8, 3, 1, 11, 7, 6, -1],
  [10, 1, 2, 6, 11, 7, -1],
  [1, 2, 10, 3, 0, 8, 6, 11, 7, -1],
  [2, 9, 0, 2, 10, 9, 6, 11, 7, -1],
  [6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1],
  [7, 2, 3, 6, 2, 7, -1],
  [7, 0, 8, 7, 6, 0, 6, 2, 0, -1],
  [2, 7, 6, 2, 3, 7, 0, 1, 9, -1],
  [1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1],
  [10, 7, 6, 10, 1, 7, 1, 3, 7, -1],
  [10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1],
  [0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1],
  [7, 6, 10, 7, 10, 8, 8, 10, 9, -1],
  [6, 8, 4, 11, 8, 6, -1],
  [3, 6, 11, 3, 0, 6, 0, 4, 6, -1],
  [8, 6, 11, 8, 4, 6, 9, 0, 1, -1],
  [9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1],
  [6, 8, 4, 6, 11, 8, 2, 10, 1, -1],
  [1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1],
  [4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1],
  [10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1],
  [8, 2, 3, 8, 4, 2, 4, 6, 2, -1],
  [0, 4, 2, 4, 6, 2, -1],
  [1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1],
  [1, 9, 4, 1, 4, 2, 2, 4, 6, -1],
  [8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1],
  [10, 1, 0, 10, 0, 6, 6, 0, 4, -1],
  [4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1],
  [10, 9, 4, 6, 10, 4, -1],
  [4, 9, 5, 7, 6, 11, -1],
  [0, 8, 3, 4, 9, 5, 11, 7, 6, -1],
  [5, 0, 1, 5, 4, 0, 7, 6, 11, -1],
  [11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1],
  [9, 5, 4, 10, 1, 2, 7, 6, 11, -1],
  [6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1],
  [7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1],
  [3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1],
  [7, 2, 3, 7, 6, 2, 5, 4, 9, -1],
  [9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1],
  [3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1],
  [6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1],
  [9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1],
  [1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1],
  [4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1],
  [7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1],
  [6, 9, 5, 6, 11, 9, 11, 8, 9, -1],
  [3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1],
  [0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1],
  [6, 11, 3, 6, 3, 5, 5, 3, 1, -1],
  [1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1],
  [0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1],
  [11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1],
  [6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1],
  [5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1],
  [9, 5, 6, 9, 6, 0, 0, 6, 2, -1],
  [1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1],
  [1, 5, 6, 2, 1, 6, -1],
  [1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1],
  [10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1],
  [0, 3, 8, 5, 6, 10, -1],
  [10, 5, 6, -1],
  [11, 5, 10, 7, 5, 11, -1],
  [11, 5, 10, 11, 7, 5, 8, 3, 0, -1],
  [5, 11, 7, 5, 10, 11, 1, 9, 0, -1],
  [10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1],
  [11, 1, 2, 11, 7, 1, 7, 5, 1, -1],
  [0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1],
  [9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1],
  [7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1],
  [2, 5, 10, 2, 3, 5, 3, 7, 5, -1],
  [8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1],
  [9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1],
  [9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1],
  [1, 3, 5, 3, 7, 5, -1],
  [0, 8, 7, 0, 7, 1, 1, 7, 5, -1],
  [9, 0, 3, 9, 3, 5, 5, 3, 7, -1],
  [9, 8, 7, 5, 9, 7, -1],
  [5, 8, 4, 5, 10, 8, 10, 11, 8, -1],
  [5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1],
  [0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1],
  [10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1],
  [2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1],
  [0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1],
  [0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1],
  [9, 4, 5, 2, 11, 3, -1],
  [2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1],
  [5, 10, 2, 5, 2, 4, 4, 2, 0, -1],
  [3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1],
  [5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1],
  [8, 4, 5, 8, 5, 3, 3, 5, 1, -1],
  [0, 4, 5, 1, 0, 5, -1],
  [8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1],
  [9, 4, 5, -1],
  [4, 11, 7, 4, 9, 11, 9, 10, 11, -1],
  [0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1],
  [1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1],
  [3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1],
  [4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1],
  [9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1],
  [11, 7, 4, 11, 4, 2, 2, 4, 0, -1],
  [11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1],
  [2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1],
  [9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1],
  [3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1],
  [1, 10, 2, 8, 7, 4, -1],
  [4, 9, 1, 4, 1, 7, 7, 1, 3, -1],
  [4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1],
  [4, 0, 3, 7, 4, 3, -1],
  [4, 8, 7, -1],
  [9, 10, 8, 10, 11, 8, -1],
  [3, 0, 9, 3, 9, 11, 11, 9, 10, -1],
  [0, 1, 10, 0, 10, 8, 8, 10, 11, -1],
  [3, 1, 10, 11, 3, 10, -1],
  [1, 2, 11, 1, 11, 9, 9, 11, 8, -1],
  [3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1],
  [0, 2, 11, 8, 0, 11, -1],
  [3, 2, 11, -1],
  [2, 3, 8, 2, 8, 10, 10, 8, 9, -1],
  [9, 10, 2, 0, 9, 2, -1],
  [2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1],
  [1, 10, 2, -1],
  [1, 3, 8, 9, 1, 8, -1],
  [0, 9, 1, -1],
  [0, 3, 8, -1],
  [-1]
];

const CORNER_OFFSETS: ReadonlyArray<[number, number, number]> = [
  [0, 0, 0],
  [1, 0, 0],
  [1, 1, 0],
  [0, 1, 0],
  [0, 0, 1],
  [1, 0, 1],
  [1, 1, 1],
  [0, 1, 1],
];

const EDGE_CORNERS: ReadonlyArray<[number, number]> = [
  [0, 1], [1, 2], [2, 3], [3, 0],
  [4, 5], [5, 6], [6, 7], [7, 4],
  [0, 4], [1, 5], [2, 6], [3, 7],
];

// Canonical grid edge mapping: each of the 12 cube edges maps to a unique (dx, dy, dz, axis)
// where axis: 0 = X-edge, 1 = Y-edge, 2 = Z-edge
const EDGE_CANONICAL: ReadonlyArray<[number, number, number, number]> = [
  [0, 0, 0, 0], // edge 0: (0,0,0)->(1,0,0)
  [1, 0, 0, 1], // edge 1: (1,0,0)->(1,1,0)
  [0, 1, 0, 0], // edge 2: (0,1,0)->(1,1,0)
  [0, 0, 0, 1], // edge 3: (0,0,0)->(0,1,0)
  [0, 0, 1, 0], // edge 4: (0,0,1)->(1,0,1)
  [1, 0, 1, 1], // edge 5: (1,0,1)->(1,1,1)
  [0, 1, 1, 0], // edge 6: (0,1,1)->(1,1,1)
  [0, 0, 1, 1], // edge 7: (0,0,1)->(0,1,1)
  [0, 0, 0, 2], // edge 8: (0,0,0)->(0,0,1)
  [1, 0, 0, 2], // edge 9: (1,0,0)->(1,0,1)
  [1, 1, 0, 2], // edge 10: (1,1,0)->(1,1,1)
  [0, 1, 0, 2], // edge 11: (0,1,0)->(0,1,1)
];

export function extractSDFMesh(vol: SDFTerrainVolume): ExtractedSDFMesh {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, colorMap, bandMinY, bandMaxY } = vol;
  const strideY = nx;
  const strideZ = nx * ny;
  const halfWorld = domain.worldSize * 0.5;

  // 2-Z-Slice Rolling Edge Buffer: only 2 slices of (nx * ny * 3) needed at any time!
  // Uses <0.8 MB instead of 100 MB on a 256x256x128 grid.
  const sliceEdgeCount = strideZ * 3;
  const edgeVertexMap = new Int32Array(sliceEdgeCount * 2);
  edgeVertexMap.fill(-1);

  let vertCap = nx * nz * 3;
  let positions = new Float32Array(vertCap * 3);
  let vertCount = 0;

  let idxCap = nx * nz * 12;
  let indices = new Uint32Array(idxCap);
  let idxCount = 0;

  function ensureVertCap(extra: number) {
    if (vertCount + extra <= vertCap) return;
    vertCap = Math.max(Math.floor(vertCap * 1.6), vertCount + extra + 16384);
    const nextPos = new Float32Array(vertCap * 3);
    nextPos.set(positions);
    positions = nextPos;
  }

  function ensureIdxCap(extra: number) {
    if (idxCount + extra <= idxCap) return;
    idxCap = Math.max(Math.floor(idxCap * 1.6), idxCount + extra + 32768);
    const nextIdx = new Uint32Array(idxCap);
    nextIdx.set(indices);
    indices = nextIdx;
  }

  const cubeVal = new Float32Array(8);
  const edgeVertIndices = new Int32Array(12);

  for (let z = 0; z < nz - 1; z++) {
    // Clear the upcoming (z + 1) rolling slice slot before processing slice z
    if (z > 0) {
      const nextSlotOffset = ((z + 1) & 1) * sliceEdgeCount;
      edgeVertexMap.fill(-1, nextSlotOffset, nextSlotOffset + sliceEdgeCount);
    }

    const zRow = z * nx;
    for (let x = 0; x < nx - 1; x++) {
      const idx2D = zRow + x;
      const yMin = Math.max(0, Math.min(bandMinY[idx2D], bandMinY[idx2D + 1], bandMinY[idx2D + nx]));
      const yMax = Math.min(
        ny - 2,
        Math.max(bandMaxY[idx2D], bandMaxY[idx2D + 1], bandMaxY[idx2D + nx])
      );

      for (let y = yMin; y <= yMax; y++) {
        let cubeIndex = 0;
        for (let i = 0; i < 8; i++) {
          const off = CORNER_OFFSETS[i];
          const val = sdfGrid[(z + off[2]) * strideZ + (y + off[1]) * strideY + (x + off[0])];
          cubeVal[i] = val;
          if (val < 0.0) {
            cubeIndex |= 1 << i;
          }
        }

        const edgeMask = EDGE_TABLE[cubeIndex];
        if (edgeMask === 0) continue;

        for (let e = 0; e < 12; e++) {
          if ((edgeMask & (1 << e)) === 0) continue;

          const can = EDGE_CANONICAL[e];
          const ex = x + can[0];
          const ey = y + can[1];
          const ez = z + can[2];
          const axis = can[3];
          const edgeKey = ((ez & 1) * strideZ + ey * strideY + ex) * 3 + axis;

          let vIdx = edgeVertexMap[edgeKey];
          if (vIdx === -1) {
            const [c0, c1] = EDGE_CORNERS[e];
            const v0 = cubeVal[c0];
            const v1 = cubeVal[c1];
            const denom = v0 - v1;
            const mu = Math.abs(denom) > 1e-6 ? Math.max(0, Math.min(1, v0 / denom)) : 0.5;

            const o0 = CORNER_OFFSETS[c0];
            const o1 = CORNER_OFFSETS[c1];
            const gx = x + o0[0] + mu * (o1[0] - o0[0]);
            const gy = y + o0[1] + mu * (o1[1] - o0[1]);
            const gz = z + o0[2] + mu * (o1[2] - o0[2]);

            ensureVertCap(1);
            vIdx = vertCount++;
            edgeVertexMap[edgeKey] = vIdx;

            const v3 = vIdx * 3;
            positions[v3] = gx * voxelSizeXZ - halfWorld;
            positions[v3 + 1] = gy * voxelSizeY;
            positions[v3 + 2] = gz * voxelSizeXZ - halfWorld;
          }

          edgeVertIndices[e] = vIdx;
        }

        const triRow = TRI_TABLE[cubeIndex];
        for (let t = 0; triRow[t] !== -1; t += 3) {
          ensureIdxCap(3);
          indices[idxCount++] = edgeVertIndices[triRow[t]];
          indices[idxCount++] = edgeVertIndices[triRow[t + 2]];
          indices[idxCount++] = edgeVertIndices[triRow[t + 1]];
        }
      }
    }
  }

  const surfaceVertCount = vertCount;
  const surfaceIdxCount = idxCount;

  // Step 2: 3-Pass Slope-Adaptive Taubin Relaxation on the Welded 3D SDF Mesh
  // - On talus/alluvial slopes & gentle basins: full 3D isotropic relaxation eliminates bumpy grid diamonds!
  // - On steep cliff faces: preserves horizontal Y strata ledges while smoothing XZ alcove waffles!
  const sumPos = new Float32Array(surfaceVertCount * 3);
  const neighborCounts = new Uint16Array(surfaceVertCount);
  const taubinWeights = [0.42, -0.27, 0.34];
  const borderTol = voxelSizeXZ * 0.15;

  for (let pass = 0; pass < 3; pass++) {
    sumPos.fill(0);
    neighborCounts.fill(0);

    for (let i = 0; i < surfaceIdxCount; i += 3) {
      const a = indices[i];
      const b = indices[i + 1];
      const c = indices[i + 2];

      const a3 = a * 3;
      const b3 = b * 3;
      const c3 = c * 3;

      sumPos[a3] += positions[b3] + positions[c3];
      sumPos[a3 + 1] += positions[b3 + 1] + positions[c3 + 1];
      sumPos[a3 + 2] += positions[b3 + 2] + positions[c3 + 2];
      neighborCounts[a] += 2;

      sumPos[b3] += positions[a3] + positions[c3];
      sumPos[b3 + 1] += positions[a3 + 1] + positions[c3 + 1];
      sumPos[b3 + 2] += positions[a3 + 2] + positions[c3 + 2];
      neighborCounts[b] += 2;

      sumPos[c3] += positions[a3] + positions[b3];
      sumPos[c3 + 1] += positions[a3 + 1] + positions[b3 + 1];
      sumPos[c3 + 2] += positions[a3 + 2] + positions[b3 + 2];
      neighborCounts[c] += 2;
    }

    const lambda = taubinWeights[pass];
    for (let v = 0; v < surfaceVertCount; v++) {
      const cnt = neighborCounts[v];
      if (cnt < 4) continue;
      const v3 = v * 3;
      const px = positions[v3];
      const py = positions[v3 + 1];
      const pz = positions[v3 + 2];

      const avgX = sumPos[v3] / cnt;
      const avgY = sumPos[v3 + 1] / cnt;
      const avgZ = sumPos[v3 + 2] / cnt;

      const gx = Math.max(0, Math.min(nx - 1, Math.round((px + halfWorld) / voxelSizeXZ)));
      const gz = Math.max(0, Math.min(nz - 1, Math.round((pz + halfWorld) / voxelSizeXZ)));
      const idx2D = gz * nx + gx;

      const cliff = vol.cliffMask[idx2D];
      const floorPlusLooseY =
        vol.bedrockHeight[idx2D] + vol.talusHeight[idx2D] + vol.sedimentHeight[idx2D];
      const isOnLooseApron = py <= floorPlusLooseY + 3.5 && (vol.talusHeight[idx2D] > 0.4 || cliff < 0.2);

      // Full Y relaxation on talus/scree cones; strong vertical-ledge & caprock-rim preservation on high monoliths!
      const yFactor = isOnLooseApron ? 0.92 : Math.max(0.18, 0.68 - cliff * 0.50);
      const xzFactor = isOnLooseApron ? 1.0 : 0.72;

      if (Math.abs(Math.abs(px) - halfWorld) > borderTol) {
        positions[v3] = px + (avgX - px) * (lambda * xzFactor);
      }
      positions[v3 + 1] = py + (avgY - py) * (lambda * yFactor);
      if (Math.abs(Math.abs(pz) - halfWorld) > borderTol) {
        positions[v3 + 2] = pz + (avgZ - pz) * (lambda * xzFactor);
      }
    }
  }

  // Step 3: Append Optional 3D Geological Block-Diagram Cutaway Skirt around the 4 outer edges
  // Closes the sides of the 3D SDF volume down to a flat baseplate, revealing underground 3D strata!
  const buildSkirt = domain.blockSkirt !== false;
  let skirtStartVert = surfaceVertCount;
  let skirtStartIdx = surfaceIdxCount;

  if (buildSkirt) {
    const skirtLayers = 14;
    const baseY = 2.0;
    const perimeterSegments = (nx - 1) * 2 + (nz - 1) * 2;
    ensureVertCap((perimeterSegments + 4) * (skirtLayers + 1));
    ensureIdxCap(perimeterSegments * skirtLayers * 6);

    function addCutawayWall(
      steps: number,
      getXZAndIdx: (s: number) => { wx: number; wz: number; idx2D: number },
      flipWinding: boolean
    ) {
      const colVertStart = vertCount;
      for (let s = 0; s <= steps; s++) {
        const { wx, wz, idx2D } = getXZAndIdx(s);
        const topY = getTotalSurfaceHeight(vol, idx2D);
        for (let k = 0; k <= skirtLayers; k++) {
          const frac = k / skirtLayers;
          const wy = topY * (1.0 - frac) + baseY * frac;
          const vIdx = vertCount++;
          const v3 = vIdx * 3;
          positions[v3] = wx;
          positions[v3 + 1] = wy;
          positions[v3 + 2] = wz;
        }
      }

      const stride = skirtLayers + 1;
      for (let s = 0; s < steps; s++) {
        const c0 = colVertStart + s * stride;
        const c1 = colVertStart + (s + 1) * stride;
        for (let k = 0; k < skirtLayers; k++) {
          const v00 = c0 + k;
          const v01 = c0 + k + 1;
          const v10 = c1 + k;
          const v11 = c1 + k + 1;
          if (!flipWinding) {
            indices[idxCount++] = v00;
            indices[idxCount++] = v01;
            indices[idxCount++] = v10;
            indices[idxCount++] = v10;
            indices[idxCount++] = v01;
            indices[idxCount++] = v11;
          } else {
            indices[idxCount++] = v00;
            indices[idxCount++] = v10;
            indices[idxCount++] = v01;
            indices[idxCount++] = v10;
            indices[idxCount++] = v11;
            indices[idxCount++] = v01;
          }
        }
      }
    }

    // North wall (z = 0, wz = -halfWorld)
    addCutawayWall(
      nx - 1,
      (s) => ({ wx: s * voxelSizeXZ - halfWorld, wz: -halfWorld, idx2D: s }),
      false
    );
    // South wall (z = nz - 1, wz = +halfWorld)
    addCutawayWall(
      nx - 1,
      (s) => ({
        wx: s * voxelSizeXZ - halfWorld,
        wz: halfWorld,
        idx2D: (nz - 1) * nx + s,
      }),
      true
    );
    // West wall (x = 0, wx = -halfWorld)
    addCutawayWall(
      nz - 1,
      (s) => ({ wx: -halfWorld, wz: s * voxelSizeXZ - halfWorld, idx2D: s * nx }),
      true
    );
    // East wall (x = nx - 1, wx = +halfWorld)
    addCutawayWall(
      nz - 1,
      (s) => ({
        wx: halfWorld,
        wz: s * voxelSizeXZ - halfWorld,
        idx2D: s * nx + (nx - 1),
      }),
      false
    );
  }

  // Step 4: Compute Smooth Area-Weighted Vertex Normals + Blended 3D SDF Gradient Normals
  const normals = new Float32Array(vertCount * 3);
  for (let i = 0; i < idxCount; i += 3) {
    const a = indices[i] * 3;
    const b = indices[i + 1] * 3;
    const c = indices[i + 2] * 3;

    const abx = positions[b] - positions[a];
    const aby = positions[b + 1] - positions[a + 1];
    const abz = positions[b + 2] - positions[a + 2];

    const acx = positions[c] - positions[a];
    const acy = positions[c + 1] - positions[a + 1];
    const acz = positions[c + 2] - positions[a + 2];

    const nxCross = aby * acz - abz * acy;
    const nyCross = abz * acx - abx * acz;
    const nzCross = abx * acy - aby * acx;

    normals[a] += nxCross;
    normals[a + 1] += nyCross;
    normals[a + 2] += nzCross;

    normals[b] += nxCross;
    normals[b + 1] += nyCross;
    normals[b + 2] += nzCross;

    normals[c] += nxCross;
    normals[c + 1] += nyCross;
    normals[c + 2] += nzCross;
  }

  const inv2DX = 1.0 / (2.0 * voxelSizeXZ);
  const inv2DY = 1.0 / (2.0 * voxelSizeY);

  for (let v = 0; v < vertCount; v++) {
    const v3 = v * 3;
    let nxVal = normals[v3];
    let nyVal = normals[v3 + 1];
    let nzVal = normals[v3 + 2];
    let len = Math.sqrt(nxVal * nxVal + nyVal * nyVal + nzVal * nzVal) || 1.0;
    nxVal /= len;
    nyVal /= len;
    nzVal /= len;

    // On steep cliff faces and recessed overhang alcoves, blend with 3D SDF central-difference gradient
    // to eliminate Marching Cubes diagonal triangle faceting
    if (v < skirtStartVert && Math.abs(nyVal) < 0.72) {
      const gx = Math.max(1, Math.min(nx - 2, Math.round((positions[v3] + halfWorld) / voxelSizeXZ)));
      const gy = Math.max(1, Math.min(ny - 2, Math.round(positions[v3 + 1] / voxelSizeY)));
      const gz = Math.max(1, Math.min(nz - 2, Math.round((positions[v3 + 2] + halfWorld) / voxelSizeXZ)));
      const idx3D = gz * strideZ + gy * strideY + gx;

      const sdx = (sdfGrid[idx3D + 1] - sdfGrid[idx3D - 1]) * inv2DX;
      const sdy = (sdfGrid[idx3D + strideY] - sdfGrid[idx3D - strideY]) * inv2DY;
      const sdz = (sdfGrid[idx3D + strideZ] - sdfGrid[idx3D - strideZ]) * inv2DX;
      const sLen = Math.sqrt(sdx * sdx + sdy * sdy + sdz * sdz);

      if (sLen > 0.15) {
        nxVal = nxVal * 0.68 + (sdx / sLen) * 0.32;
        nyVal = nyVal * 0.68 + (sdy / sLen) * 0.32;
        nzVal = nzVal * 0.68 + (sdz / sLen) * 0.32;
        len = Math.sqrt(nxVal * nxVal + nyVal * nyVal + nzVal * nzVal) || 1.0;
        nxVal /= len;
        nyVal /= len;
        nzVal /= len;
      }
    }

    normals[v3] = nxVal;
    normals[v3 + 1] = nyVal;
    normals[v3 + 2] = nzVal;
  }

  // Step 5: Sample Bilinear SatMap Colors + True 3D World-Space (wx, wy, wz) Stratification & Cave AO
  const colors = new Float32Array(vertCount * 3);
  const preset =
    SATMAP_PRESETS.find((p) => p.id === vol.activeSatMapId) || SATMAP_PRESETS[0];
  const strataNoise = new SeededNoise(9012);
  const strataFreq = vol.strataConfig?.strataFrequency || 11;

  for (let v = 0; v < vertCount; v++) {
    const v3 = v * 3;
    const wx = positions[v3];
    const wy = positions[v3 + 1];
    const wz = positions[v3 + 2];

    const gx = (wx + halfWorld) / voxelSizeXZ;
    const gz = (wz + halfWorld) / voxelSizeXZ;
    const nyNorm = normals[v3 + 1];

    const x0 = Math.max(0, Math.min(nx - 1, Math.floor(gx)));
    const z0 = Math.max(0, Math.min(nz - 1, Math.floor(gz)));
    const x1 = Math.min(nx - 1, x0 + 1);
    const z1 = Math.min(nz - 1, z0 + 1);
    const tx = Math.max(0, Math.min(1, gx - x0));
    const tz = Math.max(0, Math.min(1, gz - z0));

    const idx2D = z0 * nx + x0;
    const i00 = idx2D * 3;
    const i10 = (z0 * nx + x1) * 3;
    const i01 = (z1 * nx + x0) * 3;
    const i11 = (z1 * nx + x1) * 3;

    let r =
      (colorMap[i00] * (1 - tx) + colorMap[i10] * tx) * (1 - tz) +
      (colorMap[i01] * (1 - tx) + colorMap[i11] * tx) * tz;
    let g =
      (colorMap[i00 + 1] * (1 - tx) + colorMap[i10 + 1] * tx) * (1 - tz) +
      (colorMap[i01 + 1] * (1 - tx) + colorMap[i11 + 1] * tx) * tz;
    let b =
      (colorMap[i00 + 2] * (1 - tx) + colorMap[i10 + 2] * tx) * (1 - tz) +
      (colorMap[i01 + 2] * (1 - tx) + colorMap[i11 + 2] * tx) * tz;

    const isSkirtWall = v >= skirtStartVert;
    const steepness3D = isSkirtWall
      ? 0.92
      : Math.max(0, Math.min(1, (0.82 - Math.abs(nyNorm)) / 0.55));
    const floorPlusTalusY = vol.bedrockHeight[idx2D] + vol.talusHeight[idx2D];
    const isOnTalusApron =
      !isSkirtWall && vol.talusHeight[idx2D] > 0.8 && wy <= floorPlusTalusY + 3.5;
    const riverHere = isSkirtWall ? 0 : vol.riverFlow[idx2D];

    if (steepness3D > 0.04 && !isOnTalusApron && riverHere < 0.25) {
      const s3d = sample3DStrataProfile(
        wx,
        wy,
        wz,
        domain.maxHeight,
        strataFreq,
        strataNoise,
        vol.strataConfig
      );
      const strataRGB = sampleRamp(preset.cliffStrataRamp, s3d.strataColorValue);
      // Convert cliff ramp to linear-sRGB and shade caprock ledges vs recessed shale seams
      const sr = Math.pow(strataRGB[0], 1.48);
      const sg = Math.pow(strataRGB[1], 1.48);
      const sb = Math.pow(strataRGB[2], 1.48);

      const ledgeLight =
        1.0 + s3d.caprockOutward * 0.14 - s3d.shaleRecess * 0.24 - s3d.faultScarMask * 0.18;
      const blend3D = steepness3D * (isSkirtWall ? 0.92 : 0.88);

      r = r * (1.0 - blend3D) + sr * ledgeLight * blend3D;
      g = g * (1.0 - blend3D) + sg * ledgeLight * blend3D;
      b = b * (1.0 - blend3D) + sb * ledgeLight * blend3D;

      if (isSkirtWall) {
        // Subtle subsurface cross-section darkening toward the base of the geological block
        const depthDarken = 0.72 + 0.24 * Math.min(1.0, wy / (domain.maxHeight * 0.45));
        r *= depthDarken;
        g *= depthDarken;
        b *= depthDarken;
      }
    }

    if (!isSkirtWall) {
      // 3D Overhang underside & Basal Wind-Sapped Alcove Ambient Occlusion
      if (nyNorm < 0.08) {
        const overhangShade = Math.max(0.42, Math.min(1.0, 0.76 + nyNorm * 0.55));
        r *= overhangShade;
        g *= overhangShade;
        b *= overhangShade;
      }

      // 3D Underground Karst Cave interior ambient occlusion
      if (vol.caveMask2D[idx2D] > 0.5) {
        const surfH = getTotalSurfaceHeight(vol, idx2D);
        const depthBelowSurface = surfH - wy;
        if (depthBelowSurface > voxelSizeY * 1.8) {
          const caveAO = Math.max(
            0.32,
            Math.min(1.0, 1.0 - (depthBelowSurface / (voxelSizeY * 6.5)) * 0.62)
          );
          r = Math.max(0.03, r * caveAO);
          g = Math.max(0.03, g * caveAO);
          b = Math.max(0.04, b * caveAO);
        }
      }
    }

    colors[v3] = Math.max(0, Math.min(1, r));
    colors[v3 + 1] = Math.max(0, Math.min(1, g));
    colors[v3 + 2] = Math.max(0, Math.min(1, b));
  }

  void skirtStartIdx;

  return {
    positions: positions.subarray(0, vertCount * 3),
    normals,
    colors,
    indices: indices.subarray(0, idxCount),
    vertexCount: vertCount,
    triangleCount: idxCount / 3,
  };
}

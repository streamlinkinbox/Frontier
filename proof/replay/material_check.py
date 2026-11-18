"""Measure the body-material lookup on actual triangles, independent of the lighting pass."""

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

bundle, cache, dest = map(Path, sys.argv[1:])
data = bundle.read_bytes()
meta = np.frombuffer(data, "<u4", count=16, offset=20).reshape(4, 4)
nodes, tris = np.frombuffer(data, "<u4", count=2, offset=8)
start = 84 + int(nodes) * 32
T = np.frombuffer(data, "<f4", offset=start, count=int(meta[0, 3]) * 16).reshape(-1, 16)
off = start + int(tris) * 64
dims = np.frombuffer(data, "<u4", offset=off, count=3)
lo, hi = np.frombuffer(data, "<f4", offset=off + 16, count=6).reshape(2, 3)
ids = np.fromfile(cache, "u1").reshape(tuple(dims[::-1]))
A, U, V, truth = T[:, :3], T[:, 4:7], T[:, 8:11], T[:, 3].astype(int)
area = np.linalg.norm(np.cross(U, V), axis=1) / 2


def labels(p):
    ix, iy, iz = np.clip(((p - lo) / (hi - lo) * dims).astype(int), 0, dims - 1).T
    x, y, z = p.T
    old = np.where((z > 0.56) & (x > -1.35) & (x < 1.15) & (abs(y) < 0.92), 7, 6)
    old = np.where(
        (abs(x) > 3) & (z > 0.16) & (z < 0.44) & (abs(y) < 1.05),
        np.where(x > 0, 13, 14),
        old,
    )
    return old, ids[iz, iy, ix]


weights = np.zeros((2, 2))
count = 64
for i in range(count):
    r = np.sqrt((i + 0.5) / count)
    vdc = int(f"{i:06b}"[::-1], 2) / count
    for j, result in enumerate(labels(A + U * (r * (1 - vdc)) + V * (r * vdc))):
        weights[j] += [
            np.sum(area * (result != truth)),
            np.sum(
                area * (np.isin(result, [12, 13, 14]) != np.isin(truth, [12, 13, 14]))
            ),
        ]
report = {
    key: dict(zip(["wrong_material_fraction", "wrong_lamp_fraction"], row))
    for key, row in zip(["coordinate_rule", "mesh_r8ui"], weights / (count * sum(area)))
}
print(json.dumps(report, indent=2))
(dest.parent / "material.json").write_text(json.dumps(report, indent=2) + "\n")
# Orthographic body-only inspection; same z buffer for all three ID images.
view = np.array([0.65, 0.95, 0.45])
view /= np.linalg.norm(view)
right = np.cross([0, 0, 1], view)
right /= np.linalg.norm(right)
up = np.cross(view, right)
project = np.stack([right * 70, -up * 70, view], axis=1)
P = A @ project + [240, 157, 0]
u = U @ project
v = V @ project
depth = np.full((270, 480), -np.inf)
images = np.full((3, 270, 480, 3), [16, 23, 31], np.uint8)
palette = np.array([[140, 152, 160]] * 32)
palette[[12, 13]] = [245, 221, 121]
palette[14] = [252, 68, 66]
palette[7] = [56, 103, 121]
palette[[8, 9, 15]] = [74, 76, 83]
palette[16] = [117, 99, 119]
for k, (a, e, f) in enumerate(zip(P, u, v)):
    q = np.stack([a, a + e, a + f])
    x0, y0 = np.maximum(0, np.floor(q[:, :2].min(axis=0)).astype(int))
    x1, y1 = np.minimum([479, 269], np.ceil(q[:, :2].max(axis=0)).astype(int))
    det = e[0] * f[1] - e[1] * f[0]
    if x0 > x1 or y0 > y1 or abs(det) < 1e-8:
        continue
    y, x = np.mgrid[y0 : y1 + 1, x0 : x1 + 1]
    dx = x + 0.5 - a[0]
    dy = y + 0.5 - a[1]
    b = (dx * f[1] - dy * f[0]) / det
    c = (dy * e[0] - dx * e[1]) / det
    z = a[2] + b * e[2] + c * f[2]
    m = (b >= 0) & (c >= 0) & (b + c <= 1) & (z > depth[y, x])
    if not np.any(m):
        continue
    x, y = x[m], y[m]
    depth[y, x] = z[m]
    pos = A[k] + b[m, None] * U[k] + c[m, None] * V[k]
    for im, lab in zip(images, [truth[k], *labels(pos)]):
        im[y, x] = palette[lab]
for im, name in zip(images, ["triangle-ids", "rule-ids", "cache-ids"]):
    Image.fromarray(im).save(dest / (name + ".png"))

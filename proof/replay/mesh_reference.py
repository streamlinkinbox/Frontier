"""Independent opaque-triangle visibility. Same transforms and receiver buffer as GLES."""

import sys
import time
from pathlib import Path

import numpy as np
from rig import load

folder, bundle, dest = map(Path, sys.argv[1:])
dest.mkdir(exist_ok=True)
scene, mesh, pos, norm, mask, lamps = load(folder, bundle)
sun = np.array([0.5758978724479675, 0.5963597297668457, 0.5591928958892822], dtype="f4")
# Shadow front-face cutoff, bias, and glass exclusion match the lab, not a claim of physical glass.
active = mask & (norm @ sun > 0.002)
o = pos[active] + 0.004 * norm[active]
V = np.zeros(len(o), "f4")
tangent = np.cross([0, 0, 1], sun)
tangent /= np.linalg.norm(tangent)
bit = np.cross(sun, tangent)
start = time.perf_counter()
for k in range(512):
    z = 1 - (k + 0.5) / 512 * (1 - np.cos(np.deg2rad(2.4)))
    phi = k * np.pi * (3 - np.sqrt(5))
    direction = np.asarray(
        sun * z + np.sqrt(1 - z * z) * (tangent * np.cos(phi) + bit * np.sin(phi)), "f4"
    )
    direction = np.broadcast_to(direction, o.shape).copy()
    V += (
        scene.run(
            o + 0.003 * direction,
            direction,
            dists=np.full(len(o), 47.997, "f4"),
            query="OCCLUDED",
        )
        < 0
    )
out = (~mask).astype("f4")
out[active] = V / 512
out.tofile(dest / "sun-reference.raw")
print("512-ray sun seconds", time.perf_counter() - start, flush=True)
# Irradiance/pi for a white Lambert receiver from true one-sided vehicle lamp triangles.
# This measures a single emitter->receiver segment, NOT multi-bounce GI.
# Offset only visibility rays; flux uses the geometric endpoints. Ground receivers
# are the cleanest comparison because the G-buffer's shading and geometric normals coincide.
for spp in [1, 16, 64]:
    start = time.perf_counter()
    result = np.zeros((len(pos), 3), "f4")
    for verts, Le in lamps:
        a, b, c = verts
        u = b - a
        v = c - a
        cross = np.cross(u, v)
        area = 0.5 * np.linalg.norm(cross)
        if area <= 1e-12:
            continue
        N = cross / (2 * area)
        candidates = np.flatnonzero(mask & ((pos - a) @ N > 0))
        p = pos[candidates]
        n = norm[candidates]
        total = np.zeros(len(p), "f4")
        for k in range(spp):
            t = np.sqrt((k + 0.5) / spp)
            w = (
                (int(f"{k:06b}"[::-1], 2) + 0.5) / 64
                if spp == 64
                else (int(f"{k:04b}"[::-1], 2) + 0.5) / 16
                if spp == 16
                else 0.5
            )
            light = (
                (a + (u * (1 - w) + v * w) * t).astype("f4")
                if spp != 1
                else (a + b + c) / 3
            )
            d = light - p
            r2 = np.sum(d * d, axis=1)
            l = d / np.sqrt(np.maximum(1e-12, r2))[:, None]
            weight = (
                area
                / np.pi
                * np.maximum(np.sum(l * n, axis=1), 0)
                * np.maximum(-l @ N, 0)
                / np.maximum(r2, 1e-12)
            )
            idx = np.flatnonzero(weight > 0)
            o = p[idx] + 0.004 * n[idx]
            delta = light - o
            dist = np.linalg.norm(delta, axis=1)
            ray = delta / np.maximum(dist[:, None], 1e-10)
            if len(idx):
                total[idx] += weight[idx] * (
                    scene.run(
                        o,
                        ray,
                        dists=np.maximum(dist - 0.001, 0).astype("f4"),
                        query="OCCLUDED",
                    )
                    < 0
                )
        result[candidates] += total[:, None] * Le / spp
    result.tofile(dest / f"lamp{spp}.raw")
    print(spp, "samples per triangle, seconds", time.perf_counter() - start, flush=True)

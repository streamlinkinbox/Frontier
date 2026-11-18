"""An isolated INDIRECT path class: light -> diffuse patch of ground -> white receiver.
No direct camera light, interpolated screenspace pixels, environment or denoising.
This bounded floor-cache reference does not include wall/car interreflection.
"""

import json
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image
from rig import load

fixture, bundle, out = map(Path, sys.argv[1:])
scene, mesh, pos, norm, mask, lamps = load(fixture, bundle)
start = time.perf_counter()
nx, ny = 257, 193
hx, hy = 8.0, 6.0
y, x = np.mgrid[-hy : hy : ny * 1j, -hx : hx : nx * 1j]
floor = np.stack((x, y, 0 * x), axis=-1).astype("f4").reshape(-1, 3)
floor_o = floor.copy()
floor_o[:, 2] = 0.004
cache = np.zeros((len(floor), 4, 3), "f4")


def vis(o, ray, t):
    return (
        scene.run(o, ray, dists=np.maximum(t - 0.001, 0).astype("f4"), query="OCCLUDED")
        < 0
    )


def sample_light(x, Le, slot, A=1, N=None):
    d = x - floor
    d2 = np.sum(d * d, axis=1)
    l = d / np.sqrt(d2[:, None])
    w = A * np.maximum(l[:, 2], 0) / d2
    if N is not None:
        w *= np.maximum(-l @ N, 0)
    use = np.flatnonzero(w > 0)
    o = floor_o[use]
    d = x - o
    dist = np.linalg.norm(d, axis=1)
    v = vis(o, d / dist[:, None], dist)
    cache[use, slot, :] += Le * (w[use] * v)[:, None]


for verts, Le in lamps:
    a, b, c = verts
    e = b - a
    f = c - a
    v = np.cross(e, f)
    area = 0.5 * np.linalg.norm(v)
    if area <= 1.0e-12:
        continue
    for i in range(16):
        t = np.sqrt((i + 0.5) / 16)
        w = (int(f"{i:04b}"[::-1], 2) + 0.5) / 16
        sample_light(
            (a + t * ((1 - w) * e + w * f)).astype("f4"),
            Le,
            0,
            area / 16,
            v / (2 * area),
        )
sample_light(np.array([3.0, 2.0, 3.5], "f4"), np.array([64.0, 42.0, 20.0]), 1)
L = np.array([0.5758978724, 0.5963597298, 0.5591928959])
T = np.cross([0, 0, 1], L)
T /= np.linalg.norm(T)
B = np.cross(L, T)
for i in range(64):
    c = 1 - (i + 0.5) / 64 * (1 - np.cos(np.deg2rad(2.4)))
    a = i * np.pi * (3 - np.sqrt(5))
    l = L * c + np.sqrt(1 - c * c) * (np.cos(a) * T + np.sin(a) * B)
    rd = np.broadcast_to(l, floor.shape).astype("f4")
    cache[:, 2] += (
        3.1 * max(0, l[2]) / 64 * vis(floor_o, rd, np.full(len(rd), 48, "f4"))[:, None]
    )
c = np.array([-0.25, -3.45, 1.32])
N = np.array([0, 1.1, 0.45]) - c
N /= np.linalg.norm(N)
R = np.cross(N, [0, 0, 1])
R /= np.linalg.norm(R)
U = np.cross(R, N)
for i in range(8):
    for j in range(8):
        sample_light(
            (
                c
                + 0.03 * N
                + R * 2.5 * ((i + 0.5) / 8 - 0.5)
                + U * 0.6 * ((j + 0.5) / 8 - 0.5)
            ).astype("f4"),
            np.array([11.0, 26.0, 42.0]),
            3,
            1.5 / 64,
            N,
        )
# L_o = rho E/pi for a matte floor. Store the channels separately.
cache *= np.array([0.32, 0.33, 0.36]) / np.pi
cache = np.concatenate(
    [cache, cache.sum(1, keepdims=True), np.zeros_like(cache[:, :1])], axis=1
)
assert np.isfinite(cache).all()
built = time.perf_counter()
names = ["emission", "point", "sun", "area", "all", "off"]
# Cosine hemisphere quadrature cancels the Lambertian factor in the gather.
# Ray direction = pixel tangent frame * shared low-discrepancy angular point.
ids = np.flatnonzero(mask & (pos[:, 2] > 0.001) & (norm[:, 2] < 0.999999))
P = pos[ids]
N = norm[ids]
O = P + 0.004 * N
helper = np.zeros_like(N)
helper[:, 2] = 1
helper[np.abs(N[:, 2]) > 0.99] = [1, 0, 0]
T = np.cross(helper, N)
T /= np.linalg.norm(T, axis=1)[:, None]
B = np.cross(N, T)
res = np.zeros((len(P), 6, 3), "f4")
kmax = 1024
for k in range(kmax):
    # 2D base-2 net: van der Corput radial coordinate; independent Sobol azimuth.
    gray = k ^ (k >> 1)
    vx = 0x80000000
    vy = vx
    ux = uy = 0
    while gray:
        if gray & 1:
            ux ^= vx
            uy ^= vy
        gray >>= 1
        vx >>= 1
        vy ^= vy >> 1
    u = (ux / 2**32 + 0.5 / kmax) % 1
    a = 2 * np.pi * ((uy / 2**32 + 0.5 / kmax) % 1)
    r = np.sqrt(u)
    D = (r * np.cos(a) * T + r * np.sin(a) * B + np.sqrt(1 - u) * N).astype("f4")
    t = -O[:, 2] / np.where(D[:, 2] < -1e-7, D[:, 2], 1)
    xy = O[:, :2] + D[:, :2] * t[:, None]
    valid = np.flatnonzero((t > 0) & (abs(xy[:, 0]) < hx) & (abs(xy[:, 1]) < hy))
    if len(valid) == 0:
        continue
    valid = valid[vis(O[valid], D[valid], t[valid])]
    uv = (xy[valid] + [hx, hy]) * [(nx - 1) / (2 * hx), (ny - 1) / (2 * hy)]
    ij = np.floor(uv).astype(int)
    a, b = (uv - ij).T
    base = ij[:, 0] + nx * ij[:, 1]
    for offset, w in [
        (0, (1 - a) * (1 - b)),
        (1, a * (1 - b)),
        (nx, (1 - a) * b),
        (nx + 1, a * b),
    ]:
        res[valid] += cache[base + offset] * w[:, None, None]
res /= kmax
full = np.zeros((len(pos), 6, 3), "f4")
full[ids] = res
assert np.isfinite(full).all()
checks = {}
for name, data in zip(names, np.moveaxis(full, 1, 0)):
    assert (data.max() > 0) == (name != "off")
    checks[name] = {
        "linear_max": float(data.max()),
        "nonzero_pixels": int(np.count_nonzero(data.max(1))),
    }
    v = data.reshape(270, 480, 3)[::-1] * 8
    v = v / (1 + v)
    v = np.where(v <= 0.0031308, 12.92 * v, 1.055 * v ** (1 / 2.4) - 0.055)
    Image.fromarray(np.uint8(v * 255 + 0.5)).save(
        out / "frames" / ("bounce-" + name + ".png")
    )
checks["combined_linearity_max_abs"] = float(
    np.max(np.abs(full[:, 4] - full[:, :4].sum(1)))
)
checks.update(
    cache_seconds=built - start,
    gather_seconds=time.perf_counter() - built,
    samples=kmax,
    receiver_count=len(ids),
)
(out / "floor-bounce.json").write_text(json.dumps(checks, indent=2) + "\n")
print(checks)

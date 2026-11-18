"""Body, wheel, near- and far-field XZ sections of the original baked half-float asset."""

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

p, d = map(Path, sys.argv[1:])
b = p.read_bytes()
_, nodes, tris, _ = struct.unpack_from("<IIII", b, 4)
off = 84 + nodes * 32 + tris * 64
for i, name in enumerate(["body", "rim", "tyre", "arena-near", "arena-far"]):
    nx, ny, nz, size, *bounds = struct.unpack_from("<4I6f", b, off)
    off += 40
    channels = 1 if i < 3 else 2
    a = (
        np.frombuffer(b, "<f2", count=nx * ny * nz * channels, offset=off)
        .reshape(nz, ny, nx, channels)[:, :, :, 0]
        .astype("f4")
    )
    off += size
    assert np.isfinite(a).all()
    lo, hi = np.array(bounds).reshape(2, 3)
    m = max((hi[0] - lo[0]) / 480, (hi[2] - lo[2]) / 270)
    x = (lo[0] + hi[0]) / 2 + (np.arange(480) + 0.5 - 240) * m
    z = (lo[2] + hi[2]) / 2 - (np.arange(270) + 0.5 - 135) * m
    ix = np.floor((x - lo[0]) / (hi[0] - lo[0]) * nx).astype(int)
    iz = np.floor((z - lo[2]) / (hi[2] - lo[2]) * nz).astype(int)
    v = a[np.clip(iz, 0, nz - 1)[:, None], ny // 2, np.clip(ix, 0, nx - 1)]
    near = np.exp(-abs(v) / (10 * m))
    im = np.stack(
        [
            np.where(v < 0, 235, 23 + 20 * near),
            np.where(v < 0, 105, 35 + 140 * near),
            np.where(v < 0, 65, 53 + 135 * near),
        ],
        axis=-1,
    ).astype("u1")
    im[abs(v) < m * 1.8] = [245, 239, 218]
    im[:, (ix < 0) | (ix >= nx)] = [13, 17, 22]
    im[(iz < 0) | (iz >= nz)] = [13, 17, 22]
    Image.fromarray(im).save(d / (name + "-xz.png"))

#!/usr/bin/env python3
"""GenerateCloudTextures.py — deterministic tileable cloud coverage maps for Frontier.

What this makes
---------------
Four 2-D grayscale coverage (density) maps in binary PPM (P6) format, the single
source of truth both renderers sample for CLOUD SHADOWS on the ground:

    cloud_scattered.ppm   low coverage  (~0.28) — fair-weather cumulus deck
    cloud_broken.ppm      mid coverage  (~0.55) — the DEFAULT shadow field
    cloud_overcast.ppm    high coverage (~0.82) — heavy stratus deck
    cloud_detail.ppm      unshaped high-frequency FBM — breakup multiplier

Pixel value = cloud density in [0, 1] (0 = clear sky, 1 = fully overcast).
The shadow shaders project a ground point along the sun ray up to the cloud
plane, wrap-sample the coverage map there, and attenuate DIRECT SUN ONLY:

    sun *= mix(1.0, 1.0 - coverage * strength, enabled)

Tileability
-----------
Every octave wraps its lattice modulo its own period, and every sampled field
— including the domain-warp offsets — uses the same wrapped lattice, so each
map tiles seamlessly over [0, base_period)^2: opposing edges match to ~1e-13
(the residual is float reassociation dust, far below half an LSB at 8 bit).
The GPU samples with REPEAT wrap and the CPU wraps by hand; both agree. Run
with --selftest to assert periodicity (tolerance 1e-9) and run-to-run
determinism (byte-identical pixels).

Determinism
-----------
Stdlib only (argparse/hashlib/struct/math/sys), fixed seeds, insertion-ordered
iteration, no wall-clock input: the same flags produce byte-identical files on
any machine. The sha256 manifest printed at the end is recorded in
Projects/Project-Zero/INTEGRATION.md; re-running must reproduce it exactly.

Deliberate non-goals
--------------------
These maps drive SHADOWS, not the visible sky: the sky's own cirrus/cloud
layer stays procedural (an independent high layer, the way real cirrus sits
above the shadow-casting deck). Coverage is DATA, so the GPU uploads it with
Linear=true (never sRGB) and the CPU reads the raw bytes.

Usage
-----
    python3 GenerateCloudTextures.py --out EngineContent/CelestialTextures/Clouds
    python3 GenerateCloudTextures.py --out /tmp/clouds --size 1024 --preset broken
    python3 GenerateCloudTextures.py --selftest
"""

import argparse
import hashlib
import math
import os
import struct
import sys

# ----------------------------------------------------------------------------
# Fixed-point-friendly integer lattice hash (splitmix64 finalizer, 64-bit).
# ----------------------------------------------------------------------------

def _splitmix64(state):
    state = (state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    z = z ^ (z >> 31)
    return z

_HASH_SEEDS = [0x243F6A8885A308D3, 0x452821E638D01377, 0xA4093822299F31D0, 0x13198A2E03707344]

def lattice_hash(ix, iy, seed):
    """Deterministic [0, 1) lattice value for integer (ix, iy) and stream seed."""
    h = _splitmix64((ix & 0xFFFFFFFFFFFFFFFF) ^ _HASH_SEEDS[seed & 3])
    h = _splitmix64((iy & 0xFFFFFFFFFFFFFFFF) ^ h)
    h = _splitmix64((seed & 0xFFFFFFFFFFFFFFFF) ^ h)
    return (h >> 11) * (1.0 / 9007199254740992.0)

def _fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)

def vnoise(x, y, period, seed):
    """Tileable value noise over [0, period)^2. period must be a positive int."""
    xi = int(math.floor(x))
    yi = int(math.floor(y))
    xf = x - xi
    yf = y - yi
    x0 = xi % period
    y0 = yi % period
    x1 = (x0 + 1) % period
    y1 = (y0 + 1) % period
    u = _fade(xf)
    v = _fade(yf)
    a = lattice_hash(x0, y0, seed)
    b = lattice_hash(x1, y0, seed)
    c = lattice_hash(x0, y1, seed)
    d = lattice_hash(x1, y1, seed)
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v

def fbm(x, y, base_period, octaves, seed, lacunarity=2.0, gain=0.5):
    """Tileable fractal Brownian motion. Octave periods stay integral so every
    octave wraps exactly over the tile (lacunarity 2 keeps periods integral)."""
    total = 0.0
    amplitude = 0.5
    norm = 0.0
    freq = 1.0
    period = base_period
    for o in range(octaves):
        total += amplitude * vnoise(x * freq, y * freq, period, seed + o * 101)
        norm += amplitude
        amplitude *= gain
        freq *= lacunarity
        period = int(period * lacunarity)
    return total / norm if norm > 0.0 else 0.0

def billow(x, y, preset):
    """Domain-warped FBM: two offset fields bend the sampling point of the
    main field, which turns blobby value noise into billowing cloud puffs.
    Every field shares the wrapped lattice, so the warp itself tiles."""
    cells = preset["cells"]
    octaves = preset["octaves"]
    seed = preset["seed"]
    warp = preset["warp"]
    qx = fbm(x + 5.2, y + 1.3, cells, 3, seed + 11)
    qy = fbm(x + 1.7, y + 9.1, cells, 3, seed + 12)
    wx = x + warp * cells * (qx - 0.5) * 2.0
    wy = y + warp * cells * (qy - 0.5) * 2.0
    return fbm(wx, wy, cells, octaves, seed)

def smoothstep(lo, hi, x):
    if x <= lo:
        return 0.0
    if x >= hi:
        return 1.0
    t = (x - lo) / (hi - lo)
    return t * t * (3.0 - 2.0 * t)

PRESETS = {
    # name:        coverage, softness, warp, cells, octaves, seed
    "scattered": {"coverage": 0.28, "soft": 0.22, "warp": 0.18, "cells": 6, "octaves": 5, "seed": 1101},
    "broken":    {"coverage": 0.55, "soft": 0.30, "warp": 0.20, "cells": 5, "octaves": 5, "seed": 2202},
    "overcast":  {"coverage": 0.82, "soft": 0.20, "warp": 0.15, "cells": 4, "octaves": 5, "seed": 3303},
    "detail":    {"coverage": -1.0, "soft": 0.0, "warp": 0.15, "cells": 24, "octaves": 6, "seed": 4404},
}

def render_preset(name, size):
    """Render one preset to a bytes object of size*size grayscale samples."""
    preset = PRESETS[name]
    cells = preset["cells"]
    out = bytearray(size * size)
    if preset["coverage"] < 0.0:
        # Detail map: raw warped FBM, full range, no coverage shaping.
        for iy in range(size):
            v = iy / size * cells
            row = iy * size
            for ix in range(size):
                u = ix / size * cells
                w = billow(u, v, preset)
                out[row + ix] = max(0, min(255, int(w * 255.0 + 0.5)))
    else:
        # Calibrated shaping: FBM clusters around 0.5, so absolute thresholds
        # would miss the target coverage. Instead the threshold is the realized
        # (1 - coverage) quantile of THIS field (deterministic: same field, same
        # quantile), with the softness as an absolute edge band around it.
        field = [0.0] * (size * size)
        for iy in range(size):
            v = iy / size * cells
            row = iy * size
            for ix in range(size):
                u = ix / size * cells
                field[row + ix] = billow(u, v, preset)
        ordered = sorted(field)
        t = ordered[min(size * size - 1, int((1.0 - preset["coverage"]) * size * size))]
        edge = preset["soft"] * 0.5
        lo, hi = t - edge, t + edge
        for i, w in enumerate(field):
            d = smoothstep(lo, hi, w)
            out[i] = max(0, min(255, int(d * 255.0 + 0.5)))
    return bytes(out)

def write_ppm_p6(path, size, gray):
    with open(path, "wb") as f:
        f.write(("P6\n%d %d\n255\n" % (size, size)).encode("ascii"))
        rgb = bytearray(3 * len(gray))
        rgb[0::3] = gray
        rgb[1::3] = gray
        rgb[2::3] = gray
        f.write(rgb)

def sha256_of_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def selftest():
    """Assert exact tile periodicity and run-to-run determinism."""
    failures = 0
    for name, preset in PRESETS.items():
        cells = preset["cells"]
        # Periodicity: f(0, y) and f(x, 0) must equal the far edge bit-for-bit.
        for k in range(9):
            t = k / 8.0 * cells
            a, b = billow(0.0, t, preset), billow(float(cells), t, preset)
            c, d = billow(t, 0.0, preset), billow(t, float(cells), preset)
            # Float reassociation across the seam (0+w vs cells+w, then *freq)
            # leaves last-ulp dust (~1e-13); anything under 1e-9 tiles cleanly.
            if abs(a - b) > 1e-9 or abs(c - d) > 1e-9:
                print("FAIL periodicity %s t=%.6f: %r vs %r, %r vs %r" % (name, t, a, b, c, d))
                failures += 1
        # Determinism: same point twice must be identical (no hidden state).
        p1 = billow(1.234, 2.345, preset)
        p2 = billow(1.234, 2.345, preset)
        if p1 != p2:
            print("FAIL determinism %s" % name)
            failures += 1
    # Pixel determinism at small size.
    a = render_preset("broken", 32)
    b = render_preset("broken", 32)
    if a != b:
        print("FAIL pixel determinism")
        failures += 1
    if failures == 0:
        print("selftest: periodicity + determinism OK (%d presets)" % len(PRESETS))
    return failures

def main(argv):
    ap = argparse.ArgumentParser(description="Generate deterministic tileable cloud coverage maps (PPM P6).")
    ap.add_argument("--out", default=".", help="output directory for cloud_*.ppm")
    ap.add_argument("--size", type=int, default=512, help="map edge in pixels (default 512)")
    ap.add_argument("--preset", choices=sorted(PRESETS) + ["all"], default="all")
    ap.add_argument("--quiet", action="store_true", help="print only '<sha>  <file>' lines")
    ap.add_argument("--selftest", action="store_true", help="assert periodicity + determinism, then exit")
    args = ap.parse_args(argv)

    if args.selftest:
        return 1 if selftest() else 0

    if args.size < 16 or args.size > 4096:
        print("error: --size must be in [16, 4096]", file=sys.stderr)
        return 2

    names = sorted(PRESETS) if args.preset == "all" else [args.preset]
    os.makedirs(args.out, exist_ok=True)
    for name in names:
        gray = render_preset(name, args.size)
        path = os.path.join(args.out, "cloud_%s.ppm" % name)
        write_ppm_p6(path, args.size, gray)
        digest = sha256_of_file(path)
        mean = sum(gray) / (len(gray) * 255.0)
        cover = sum(1 for g in gray if g > 127) / len(gray)
        if args.quiet:
            print("%s  %s" % (digest, os.path.basename(path)))
        else:
            print("%s  %s  mean=%.3f cover=%.3f" % (digest, path, mean, cover))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
#================================================================================
# Tools/GenerateMoonTextures.py — deterministic procedural albedo maps for the
# 22 major moons of the solar system (stdlib only: identical bytes on any Python 3).
#================================================================================
# Usage (from the engine checkout root):
#     python Tools/GenerateMoonTextures.py
#         -> EngineContent/CelestialTextures/Moons/<name>_<res>.ppm (22 files)
#     python Tools/GenerateMoonTextures.py --sheet /tmp/moons.ppm
#         -> also writes a contact sheet of every moon (roster order, 6 columns).
#     python Tools/GenerateMoonTextures.py <outdir> [--only luna] [--quiet]
#
# Determinism: every random choice flows from xorshift32 + an integer lattice
# hash seeded by crc32(moon name). No `random`, no `hash()`, no dict ordering,
# no third-party libraries. PPM (P6) output loads directly via stb_image.
#================================================================================

import hashlib
import math
import os
import sys
import zlib

MASK32 = 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Deterministic integers: PRNG + periodic lattice noise (equirect x-wraps).
# ---------------------------------------------------------------------------

class Rng:
    """xorshift32: the only randomness in this file."""

    def __init__(self, seed):
        self.s = seed & MASK32 or 1

    def next(self):
        s = self.s
        s ^= (s << 13) & MASK32
        s ^= s >> 17
        s ^= (s << 5) & MASK32
        self.s = s & MASK32
        return self.s

    def f(self):
        return self.next() / 4294967296.0

    def range(self, a, b):
        return a + (b - a) * self.f()

    def pick(self, n):
        return self.next() % n


def hash2(ix, iy, seed):
    h = (ix * 0x8DA6B343 + iy * 0xD8163841 + seed * 0x9E3779B1) & MASK32
    h ^= h >> 15
    h = (h * 0x85EBCA6B) & MASK32
    h ^= h >> 13
    h = (h * 0xC2B2AE35) & MASK32
    h ^= h >> 16
    return h / 4294967296.0


def smooth(t):
    return t * t * (3.0 - 2.0 * t)


def smoothstep(a, b, t):
    if t <= a:
        return 0.0
    if t >= b:
        return 1.0
    return smooth((t - a) / (b - a))


def vnoise(x, y, xper, seed):
    xi = math.floor(x)
    yi = math.floor(y)
    xf = smooth(x - xi)
    yf = smooth(y - yi)
    x0 = int(xi) % xper
    x1 = (x0 + 1) % xper
    yi = int(yi)
    a = hash2(x0, yi, seed)
    b = hash2(x1, yi, seed)
    c = hash2(x0, yi + 1, seed)
    d = hash2(x1, yi + 1, seed)
    return a + (b - a) * xf + (c - a) * yf + (a - b - c + d) * xf * yf


def fbm(x, y, xper, seed, octaves):
    total = 0.0
    amp = 0.5
    freq = 1
    norm = 0.0
    for o in range(octaves):
        total += amp * vnoise(x * freq, y * freq, xper * freq, seed + o * 101)
        norm += amp
        amp *= 0.5
        freq *= 2
    return total / norm


def mix3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t)


# ---------------------------------------------------------------------------
# Image: bytearray RGB, x-wrapped equirectangular.
# ---------------------------------------------------------------------------

class Img:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.px = bytearray(w * h * 3)

    def save_ppm(self, path):
        with open(path, "wb") as f:
            f.write(("P6\n%d %d\n255\n" % (self.w, self.h)).encode("ascii"))
            f.write(self.px)

    def sha256(self):
        return hashlib.sha256(self.px).hexdigest()


def base_fill(img, seed, fn):
    """fn(u01, v01, lon, lat) -> (r, g, b) floats in 0..1."""
    w, h = img.w, img.h
    px = img.px
    for y in range(h):
        v = (y + 0.5) / h
        lat = 90.0 - v * 180.0
        row = y * w * 3
        for x in range(w):
            u = (x + 0.5) / w
            r, g, b = fn(u, v, u * 360.0, lat)
            i = row + x * 3
            px[i] = 255 if r >= 1.0 else (0 if r <= 0.0 else int(r * 255.0))
            px[i + 1] = 255 if g >= 1.0 else (0 if g <= 0.0 else int(g * 255.0))
            px[i + 2] = 255 if b >= 1.0 else (0 if b <= 0.0 else int(b * 255.0))


def _add(px, w, h, x, y, r, g, b):
    i = (y * w + (x % w)) * 3
    px[i] = 255 if r > 255.0 else (0 if r < 0.0 else int(r))
    px[i + 1] = 255 if g > 255.0 else (0 if g < 0.0 else int(g))
    px[i + 2] = 255 if b > 255.0 else (0 if b < 0.0 else int(b))


def _blend(px, w, h, x, y, tr, tg, tb, alpha):
    """Blend pixel toward target rgb (0..255) by alpha."""
    i = (y * w + (x % w)) * 3
    px[i] = int(px[i] + (tr - px[i]) * alpha)
    px[i + 1] = int(px[i + 1] + (tg - px[i + 1]) * alpha)
    px[i + 2] = int(px[i + 2] + (tb - px[i + 2]) * alpha)


def _mul(px, w, h, x, y, k):
    i = (y * w + (x % w)) * 3
    px[i] = int(px[i] * k)
    px[i + 1] = int(px[i + 1] * k)
    px[i + 2] = int(px[i + 2] * k)


# ---------------------------------------------------------------------------
# Features: craters, blotches, cracks, dots, rings, bands, ellipses, lines.
# ---------------------------------------------------------------------------

def add_crater(img, cx, cy, rad, depth=0.45, rim=0.55):
    """Bowl + sun-side rim. cx/cy/rad in pixels; x-wrapped, y-clamped."""
    w, h = img.w, img.h
    px = img.px
    rr = max(rad, 1.0)
    x0 = int(cx - rr - 1.0)
    x1 = int(cx + rr + 1.0)
    y0 = max(0, int(cy - rr - 1.0))
    y1 = min(h - 1, int(cy + rr + 1.0))
    for y in range(y0, y1 + 1):
        dy = (y - cy) / rr
        for x in range(x0, x1 + 1):
            dx = (x - cx) / rr
            d2 = dx * dx + dy * dy
            if d2 > 1.0:
                continue
            d = math.sqrt(d2)
            xx = x % w
            i = (y * w + xx) * 3
            r, g, b = px[i], px[i + 1], px[i + 2]
            if d < 0.82:
                k = 1.0 - depth * (1.0 - d / 0.82)
                r *= k
                g *= k
                b *= k
            else:
                t = (d - 0.82) / 0.18
                sun = -dx * 0.7 - dy * 0.7
                if sun > 0.0:
                    lift = 1.0 + rim * sun * math.sin(t * math.pi)
                    r *= lift
                    g *= lift
                    b *= lift
                else:
                    drop = 1.0 + depth * 0.8 * sun * math.sin(t * math.pi)
                    r *= drop
                    g *= drop
                    b *= drop
            px[i] = 255 if r > 255.0 else int(r)
            px[i + 1] = 255 if g > 255.0 else int(g)
            px[i + 2] = 255 if b > 255.0 else int(b)


def lonlat_to_xy(img, lon, lat):
    return (lon / 360.0 * img.w, (90.0 - lat) / 180.0 * img.h)


def add_craters(img, rng, count, rmin, rmax, depth=0.45, rim=0.55):
    for _ in range(count):
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        rad = rng.range(rmin, rmax)
        coslat = max(0.3, math.cos(math.radians(lat)))
        cx, cy = lonlat_to_xy(img, lon, lat)
        # Equirect: stretch x-radius near poles so the crater stays round.
        w, h = img.w, img.h
        px = img.px
        rr = max(rad, 1.0)
        rx = rr / coslat
        x0 = int(cx - rx - 1.0)
        x1 = int(cx + rx + 1.0)
        y0 = max(0, int(cy - rr - 1.0))
        y1 = min(h - 1, int(cy + rr + 1.0))
        for y in range(y0, y1 + 1):
            dy = (y - cy) / rr
            for x in range(x0, x1 + 1):
                dx = (x - cx) / rx
                d2 = dx * dx + dy * dy
                if d2 > 1.0:
                    continue
                d = math.sqrt(d2)
                xx = x % w
                i = (y * w + xx) * 3
                r, g, b = px[i], px[i + 1], px[i + 2]
                if d < 0.82:
                    k = 1.0 - depth * (1.0 - d / 0.82)
                    r *= k
                    g *= k
                    b *= k
                else:
                    t = (d - 0.82) / 0.18
                    sun = -dx * 0.7 - dy * 0.7
                    if sun > 0.0:
                        lift = 1.0 + rim * sun * math.sin(t * math.pi)
                        r *= lift
                        g *= lift
                        b *= lift
                    else:
                        drop = 1.0 + depth * 0.8 * sun * math.sin(t * math.pi)
                        r *= drop
                        g *= drop
                        b *= drop
                px[i] = 255 if r > 255.0 else int(r)
                px[i + 1] = 255 if g > 255.0 else int(g)
                px[i + 2] = 255 if b > 255.0 else int(b)


def add_rays(img, cx, cy, rad, count, length, brighten=0.5):
    w, h = img.w, img.h
    px = img.px
    for k in range(count):
        ang = 2.0 * math.pi * (k + 0.5) / count
        dx = math.cos(ang)
        dy = math.sin(ang)
        steps = int(length)
        for s in range(steps):
            t = s / steps
            x = int(cx + dx * (rad + s))
            y = int(cy + dy * (rad + s))
            if y < 0 or y >= h:
                continue
            fade = (1.0 - t) * brighten
            i = (y * w + (x % w)) * 3
            px[i] = min(255, int(px[i] * (1.0 + fade)))
            px[i + 1] = min(255, int(px[i + 1] * (1.0 + fade)))
            px[i + 2] = min(255, int(px[i + 2] * (1.0 + fade)))


def add_blotch(img, lon, lat, rad, target, strength):
    """Soft radial mix toward target rgb (0..1)."""
    w, h = img.w, img.h
    px = img.px
    cx, cy = lonlat_to_xy(img, lon, lat)
    tr, tg, tb = target[0] * 255.0, target[1] * 255.0, target[2] * 255.0
    x0 = int(cx - rad - 1.0)
    x1 = int(cx + rad + 1.0)
    y0 = max(0, int(cy - rad - 1.0))
    y1 = min(h - 1, int(cy + rad + 1.0))
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            d = math.hypot(x - cx, y - cy) / rad
            if d > 1.0:
                continue
            _blend(px, w, h, x, y, tr, tg, tb, strength * smooth(1.0 - d))


def add_dot(img, lon, lat, rad, target, alpha=1.0):
    w, h = img.w, img.h
    px = img.px
    cx, cy = lonlat_to_xy(img, lon, lat)
    tr, tg, tb = target[0] * 255.0, target[1] * 255.0, target[2] * 255.0
    x0 = int(cx - rad - 1.0)
    x1 = int(cx + rad + 1.0)
    y0 = max(0, int(cy - rad - 1.0))
    y1 = min(h - 1, int(cy + rad + 1.0))
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            d = math.hypot(x - cx, y - cy) / max(rad, 0.5)
            if d > 1.0:
                continue
            _blend(px, w, h, x, y, tr, tg, tb, alpha * smooth(1.0 - d))


def add_ring(img, lon, lat, rad, thick, target, alpha=0.9):
    w, h = img.w, img.h
    px = img.px
    cx, cy = lonlat_to_xy(img, lon, lat)
    tr, tg, tb = target[0] * 255.0, target[1] * 255.0, target[2] * 255.0
    x0 = int(cx - rad - thick - 1.0)
    x1 = int(cx + rad + thick + 1.0)
    y0 = max(0, int(cy - rad - thick - 1.0))
    y1 = min(h - 1, int(cy + rad + thick + 1.0))
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            d = abs(math.hypot(x - cx, y - cy) - rad) / max(thick, 0.5)
            if d > 1.0:
                continue
            _blend(px, w, h, x, y, tr, tg, tb, alpha * smooth(1.0 - d))


def add_ellipse(img, lon, lat, rx, ry, target, alpha=0.8):
    w, h = img.w, img.h
    px = img.px
    cx, cy = lonlat_to_xy(img, lon, lat)
    tr, tg, tb = target[0] * 255.0, target[1] * 255.0, target[2] * 255.0
    x0 = int(cx - rx - 1.0)
    x1 = int(cx + rx + 1.0)
    y0 = max(0, int(cy - ry - 1.0))
    y1 = min(h - 1, int(cy + ry + 1.0))
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            d = math.hypot((x - cx) / max(rx, 0.5), (y - cy) / max(ry, 0.5))
            if d > 1.0:
                continue
            _blend(px, w, h, x, y, tr, tg, tb, alpha * smooth(1.0 - d))


def add_crack(img, rng, lon, lat, steps, step_px, width, target, alpha, wander=0.35):
    """Random-walk polyline, x-wrapped."""
    w, h = img.w, img.h
    px = img.px
    x, y = lonlat_to_xy(img, lon, lat)
    ang = rng.f() * 2.0 * math.pi
    tr, tg, tb = target[0] * 255.0, target[1] * 255.0, target[2] * 255.0
    rad = max(width * 0.5, 0.75)
    for _ in range(steps):
        x0 = int(x - rad - 1.0)
        x1 = int(x + rad + 1.0)
        y0 = max(0, int(y - rad - 1.0))
        y1 = min(h - 1, int(y + rad + 1.0))
        for yy in range(y0, y1 + 1):
            for xx in range(x0, x1 + 1):
                if math.hypot(xx - x, yy - y) <= rad:
                    _blend(px, w, h, xx, yy, tr, tg, tb, alpha)
        ang += (rng.f() - 0.5) * 2.0 * wander
        x += math.cos(ang) * step_px
        y += math.sin(ang) * step_px
        if y < 1.0 or y > h - 2.0:
            ang = -ang
            y = min(max(y, 1.0), h - 2.0)


def add_polyline(img, points, width, target, alpha):
    """points: [(lon, lat), ...]. Straight segments, x-wrapped."""
    w, h = img.w, img.h
    px = img.px
    tr, tg, tb = target[0] * 255.0, target[1] * 255.0, target[2] * 255.0
    rad = max(width * 0.5, 0.75)
    for (lon0, lat0), (lon1, lat1) in zip(points, points[1:]):
        x0, y0 = lonlat_to_xy(img, lon0, lat0)
        x1, y1 = lonlat_to_xy(img, lon1, lat1)
        if abs(x1 - x0) > w * 0.5:  # take the short way across the seam
            if x1 > x0:
                x0 += w
            else:
                x1 += w
        steps = max(int(math.hypot(x1 - x0, y1 - y0)), 1)
        for s in range(steps + 1):
            x = x0 + (x1 - x0) * s / steps
            y = y0 + (y1 - y0) * s / steps
            for yy in range(max(0, int(y - rad)), min(h, int(y + rad) + 1)):
                for xx in range(int(x - rad), int(x + rad) + 1):
                    if math.hypot(xx - x, yy - y) <= rad:
                        _blend(px, w, h, xx, yy, tr, tg, tb, alpha)


def add_lat_line(img, lat, lon0, lon1, wobble, width, target, alpha):
    """Wobbly constant-latitude line (tiger stripes, ridges)."""
    w, h = img.w, img.h
    px = img.px
    tr, tg, tb = target[0] * 255.0, target[1] * 255.0, target[2] * 255.0
    rad = max(width * 0.5, 0.75)
    _, ybase = lonlat_to_xy(img, 0.0, lat)
    steps = int(abs(lon1 - lon0) / 360.0 * w)
    for s in range(steps + 1):
        lon = lon0 + (lon1 - lon0) * s / max(steps, 1)
        x = lon / 360.0 * w
        y = ybase + math.sin(lon * 0.05 + lat) * wobble
        for yy in range(max(0, int(y - rad)), min(h, int(y + rad) + 1)):
            for xx in range(int(x - rad), int(x + rad) + 1):
                if math.hypot(xx - x, yy - y) <= rad:
                    _blend(px, w, h, xx, yy, tr, tg, tb, alpha)


# ---------------------------------------------------------------------------
# Recipes: one per moon. (img, rng, S) -> None. Base first, features after.
# ---------------------------------------------------------------------------

def moon_luna(img, rng, S):
    w = img.w
    base = (0.66, 0.645, 0.62)
    dark = (0.26, 0.275, 0.31)

    def fn(u, v, lon, lat):
        n = fbm(u * 8.0, v * 8.0, 8, S + 1, 4)
        m = fbm(u * 3.0 + 7.3, v * 3.0 + 2.1, 3, S + 2, 3)
        t = smoothstep(0.55, 0.63, m)
        vary = 0.86 + 0.28 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        r, g, b = mix3(base, dark, t)
        return (r * vary * grain, g * vary * grain, b * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 200, 1.0, 18.0, 0.50, 0.55)
    for _ in range(6):  # young rayed craters
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        cx, cy = lonlat_to_xy(img, lon, lat)
        rad = rng.range(6.0, 12.0)
        add_crater(img, cx, cy, rad, 0.45, 0.6)
        add_rays(img, cx, cy, rad, 12, rng.range(30.0, 90.0), 0.45)


def moon_phobos(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 8.0, v * 8.0, 8, S + 1, 4)
        vary = 0.86 + 0.28 * n
        grain = 0.95 + 0.10 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.38 * vary * grain, 0.31 * vary * grain, 0.28 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 120, 1.0, 8.0, 0.55, 0.5)
    # Stickney + grooves.
    cx, cy = lonlat_to_xy(img, 200.0, 5.0)
    add_crater(img, cx, cy, 46.0, 0.55, 0.6)
    add_dot(img, 200.0, 5.0, 20.0, (0.16, 0.13, 0.12), 0.5)
    for k in range(12):
        add_polyline(img, [(150.0 + k * 4.0, -25.0), (165.0 + k * 4.0, 30.0)],
                     1.5, (0.16, 0.13, 0.12), 0.6)


def moon_deimos(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 6.0, v * 6.0, 6, S + 1, 3)
        vary = 0.94 + 0.12 * n
        grain = 0.96 + 0.08 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.48 * vary * grain, 0.46 * vary * grain, 0.44 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 60, 1.5, 9.0, 0.25, 0.3)


def moon_io(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 5.0, v * 5.0, 5, S + 1, 4)
        vary = 0.90 + 0.20 * n
        grain = 0.97 + 0.06 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.86 * vary * grain, 0.74 * vary * grain, 0.38 * vary * grain)

    base_fill(img, S, fn)
    for _ in range(8):  # SO2 frost patches
        add_blotch(img, rng.f() * 360.0, math.degrees(math.asin(2.0 * rng.f() - 1.0)),
                   rng.range(15.0, 45.0), (0.92, 0.90, 0.82), 0.7)
    for _ in range(10):  # orange / red diffuse deposits
        add_blotch(img, rng.f() * 360.0, math.degrees(math.asin(2.0 * rng.f() - 1.0)),
                   rng.range(10.0, 35.0), (0.78, 0.42, 0.18) if rng.f() < 0.5 else (0.62, 0.24, 0.12), 0.6)
    for _ in range(70):  # volcanic vents: dark dot + bright halo
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        rad = rng.range(1.0, 4.0)
        add_dot(img, lon, lat, rad * 3.0, (0.95, 0.80, 0.45), 0.7)
        add_dot(img, lon, lat, rad, (0.15, 0.10, 0.08), 1.0)
    add_ring(img, 80.0, -18.0, 22.0, 5.0, (0.70, 0.20, 0.10), 0.8)  # Pele ring
    add_ring(img, 250.0, 25.0, 16.0, 4.0, (0.70, 0.20, 0.10), 0.7)


def moon_europa(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 6.0, v * 6.0, 6, S + 1, 3)
        vary = 0.95 + 0.10 * n
        grain = 0.97 + 0.06 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.84 * vary * grain, 0.82 * vary * grain, 0.76 * vary * grain)

    base_fill(img, S, fn)
    for _ in range(14):  # chaos mottling
        add_blotch(img, rng.f() * 360.0, math.degrees(math.asin(2.0 * rng.f() - 1.0)),
                   rng.range(6.0, 22.0), (0.55, 0.42, 0.30), 0.55)
    for _ in range(45):  # lineae
        add_crack(img, rng, rng.f() * 360.0, math.degrees(math.asin(2.0 * rng.f() - 1.0)),
                  rng.pick(250) + 200, 2.0, 1.0, (0.45, 0.25, 0.15), 0.5, 0.25)
    for _ in range(8):  # wide faint bands
        add_crack(img, rng, rng.f() * 360.0, math.degrees(math.asin(2.0 * rng.f() - 1.0)),
                  rng.pick(150) + 120, 2.0, 3.5, (0.60, 0.42, 0.30), 0.25, 0.2)


def moon_ganymede(img, rng, S):
    w = img.w
    dark = (0.38, 0.36, 0.34)
    light = (0.68, 0.66, 0.62)

    def fn(u, v, lon, lat):
        m = fbm(u * 5.0 + 1.7, v * 5.0 + 8.4, 5, S + 2, 3)
        n = fbm(u * 8.0, v * 8.0, 8, S + 1, 3)
        t = smoothstep(0.47, 0.53, m)
        vary = 0.92 + 0.16 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        r, g, b = mix3(dark, light, t)
        return (r * vary * grain, g * vary * grain, b * vary * grain)

    base_fill(img, S, fn)
    for _ in range(8):  # grooved terrain: furrow clusters
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        for k in range(8):
            add_crack(img, rng, lon + k * 3.0, lat, 60, 2.0, 1.0,
                      (0.52, 0.50, 0.47), 0.65, 0.12)
    add_craters(img, rng, 90, 1.0, 10.0, 0.45, 0.5)
    for _ in range(5):
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        cx, cy = lonlat_to_xy(img, lon, lat)
        add_crater(img, cx, cy, rng.range(4.0, 8.0), 0.4, 0.6)
        add_rays(img, cx, cy, 6.0, 10, 40.0, 0.4)


def moon_callisto(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 8.0, v * 8.0, 8, S + 1, 3)
        vary = 0.88 + 0.24 * n
        grain = 0.95 + 0.10 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.32 * vary * grain, 0.29 * vary * grain, 0.27 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 150, 1.0, 7.0, 0.4, 0.45)
    for _ in range(500):  # dense bright pinpoint craters
        add_dot(img, rng.f() * 360.0, math.degrees(math.asin(2.0 * rng.f() - 1.0)),
                rng.range(0.6, 1.8), (0.72, 0.68, 0.62), 0.85)
    add_dot(img, 120.0, -10.0, 14.0, (0.78, 0.74, 0.68), 0.9)  # Valhalla
    for rr in (24.0, 36.0, 50.0, 66.0):
        add_ring(img, 120.0, -10.0, rr, 3.0, (0.70, 0.66, 0.60), 0.55)


def moon_mimas(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 8.0, v * 8.0, 8, S + 1, 4)
        vary = 0.90 + 0.20 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.58 * vary * grain, 0.56 * vary * grain, 0.53 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 130, 1.0, 9.0, 0.5, 0.5)
    cx, cy = lonlat_to_xy(img, 180.0, 0.0)  # Herschel
    add_crater(img, cx, cy, img.h * 0.16, 0.55, 0.65)
    add_dot(img, 180.0, 0.0, 7.0, (0.78, 0.76, 0.72), 0.8)


def moon_enceladus(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 6.0, v * 6.0, 6, S + 1, 3)
        vary = 0.97 + 0.06 * n
        grain = 0.98 + 0.04 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.90 * vary * grain, 0.93 * vary * grain, 0.97 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 40, 1.0, 6.0, 0.3, 0.3)
    for k, lat in enumerate((-66.0, -71.0, -76.0, -81.0)):  # tiger stripes
        add_lat_line(img, lat, 40.0 + k * 8.0, 320.0 - k * 6.0, 6.0, 4.0,
                     (0.45, 0.65, 0.85), 0.85)


def moon_tethys(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 6.0, v * 6.0, 6, S + 1, 3)
        vary = 0.95 + 0.10 * n
        grain = 0.97 + 0.06 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.82 * vary * grain, 0.80 * vary * grain, 0.77 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 80, 1.0, 8.0, 0.4, 0.45)
    cx, cy = lonlat_to_xy(img, 130.0, 30.0)  # Odysseus
    add_crater(img, cx, cy, 70.0, 0.45, 0.55)
    add_polyline(img, [(0.0, -60.0), (20.0, -20.0), (45.0, 25.0), (60.0, 60.0)],
                 2.5, (0.50, 0.48, 0.46), 0.55)  # Ithaca Chasma


def moon_dione(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 7.0, v * 7.0, 7, S + 1, 3)
        vary = 0.93 + 0.14 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.72 * vary * grain, 0.70 * vary * grain, 0.66 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 100, 1.0, 8.0, 0.4, 0.45)
    for _ in range(3):  # wispy streak clusters
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        for _ in range(12):
            add_crack(img, rng, lon + rng.range(-20.0, 20.0), lat + rng.range(-15.0, 15.0),
                      80, 2.0, 1.0, (0.90, 0.89, 0.86), 0.5, 0.3)


def moon_rhea(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 7.0, v * 7.0, 7, S + 1, 4)
        vary = 0.90 + 0.20 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.62 * vary * grain, 0.59 * vary * grain, 0.55 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 160, 1.0, 10.0, 0.45, 0.5)
    for _ in range(4):
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        cx, cy = lonlat_to_xy(img, lon, lat)
        add_crater(img, cx, cy, rng.range(4.0, 7.0), 0.4, 0.55)
        add_rays(img, cx, cy, 5.0, 10, 35.0, 0.35)


def moon_titan(img, rng, S):
    w = img.w
    warm = (0.85, 0.62, 0.30)
    cool = (0.72, 0.48, 0.22)
    pole = (0.55, 0.35, 0.18)

    def fn(u, v, lon, lat):
        warp = fbm(u * 4.0, v * 4.0, 4, S + 1, 3) - 0.5
        band = 0.5 + 0.5 * math.sin(math.radians(lat) * 2.5 + warp * 5.0 + 1.0)
        n = fbm(u * 8.0 + 3.1, v * 8.0 + 9.2, 8, S + 3, 3)
        r, g, b = mix3(cool, warm, band * 0.7 + n * 0.3)
        polar = smoothstep(55.0, 80.0, abs(lat))
        r, g, b = mix3((r, g, b), pole, polar * 0.7)
        grain = 0.98 + 0.04 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (r * grain, g * grain, b * grain)

    base_fill(img, S, fn)  # opaque haze: no surface detail


def moon_iapetus(img, rng, S):
    w = img.w
    dark = (0.22, 0.18, 0.15)
    bright = (0.76, 0.74, 0.70)

    def fn(u, v, lon, lat):
        warp = (fbm(u * 4.0, v * 4.0, 4, S + 1, 3) - 0.5) * 80.0
        # Cassini Regio: dark around lon 270, noisy boundary.
        d = abs(((lon - 270.0 + warp + 540.0) % 360.0) - 180.0)
        t = smoothstep(95.0, 70.0, 180.0 - d)
        n = fbm(u * 8.0, v * 8.0, 8, S + 3, 3)
        vary = 0.90 + 0.20 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        r, g, b = mix3(bright, dark, t)
        if abs(lat) < 2.0 and t > 0.5:  # equatorial ridge trace
            r *= 0.85
            g *= 0.85
            b *= 0.85
        return (r * vary * grain, g * vary * grain, b * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 120, 1.0, 9.0, 0.45, 0.5)


def moon_hyperion(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        pits = fbm(u * 24.0, v * 24.0, 24, S + 3, 3)
        n = fbm(u * 6.0, v * 6.0, 6, S + 1, 3)
        k = 1.0
        if pits < 0.42:
            k = 0.45 + 0.55 * smoothstep(0.30, 0.42, pits)
        elif pits < 0.50:
            k = 1.0 + 0.25 * smoothstep(0.50, 0.42, pits)
        vary = (0.88 + 0.24 * n) * k
        grain = 0.95 + 0.10 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.46 * vary * grain, 0.39 * vary * grain, 0.33 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 60, 2.0, 12.0, 0.6, 0.4)


def moon_ariel(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 7.0, v * 7.0, 7, S + 1, 3)
        vary = 0.94 + 0.12 * n
        grain = 0.97 + 0.06 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.74 * vary * grain, 0.72 * vary * grain, 0.68 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 50, 1.0, 7.0, 0.35, 0.4)
    for _ in range(10):  # canyon / groove networks
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        for k in range(4):
            add_crack(img, rng, lon + k * 5.0, lat, 90, 2.0, 2.0,
                      (0.55, 0.53, 0.50), 0.55, 0.15)
        add_dot(img, lon + 8.0, lat, 6.0, (0.88, 0.87, 0.84), 0.6)  # frost


def moon_umbriel(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 6.0, v * 6.0, 6, S + 1, 3)
        vary = 0.94 + 0.12 * n
        grain = 0.96 + 0.08 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.30 * vary * grain, 0.29 * vary * grain, 0.28 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 60, 1.5, 9.0, 0.35, 0.35)
    add_crater(img, *lonlat_to_xy(img, 90.0, -8.0), 12.0, 0.3, 0.4)  # Wunda
    add_dot(img, 90.0, -8.0, 9.0, (0.62, 0.60, 0.58), 0.7)
    add_ring(img, 90.0, -8.0, 12.0, 2.0, (0.55, 0.53, 0.51), 0.6)


def moon_titania(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 7.0, v * 7.0, 7, S + 1, 3)
        vary = 0.92 + 0.16 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.57 * vary * grain, 0.55 * vary * grain, 0.52 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 110, 1.0, 9.0, 0.4, 0.45)
    for k in range(3):  # Messina-style graben: paired canyon lines
        lon = 60.0 + k * 90.0
        lat = -30.0 + k * 20.0
        add_crack(img, rng, lon, lat, 120, 2.0, 2.5, (0.38, 0.36, 0.34), 0.75, 0.1)
        add_crack(img, rng, lon + 4.0, lat + 2.0, 120, 2.0, 2.5, (0.38, 0.36, 0.34), 0.75, 0.1)
    cx, cy = lonlat_to_xy(img, 300.0, -15.0)  # Gertrude
    add_crater(img, cx, cy, 40.0, 0.45, 0.5)
    add_dot(img, 300.0, -15.0, 25.0, (0.68, 0.66, 0.62), 0.5)


def moon_oberon(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 7.0, v * 7.0, 7, S + 1, 3)
        vary = 0.90 + 0.20 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.52 * vary * grain, 0.48 * vary * grain, 0.45 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 130, 1.0, 9.0, 0.6, 0.45)  # dark floors
    for _ in range(6):  # bright-rayed youth
        lon = rng.f() * 360.0
        lat = math.degrees(math.asin(2.0 * rng.f() - 1.0))
        cx, cy = lonlat_to_xy(img, lon, lat)
        add_crater(img, cx, cy, rng.range(3.0, 6.0), 0.4, 0.55)
        add_rays(img, cx, cy, 4.0, 10, 30.0, 0.35)


def moon_miranda(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 8.0, v * 8.0, 8, S + 1, 3)
        vary = 0.92 + 0.16 * n
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (0.55 * vary * grain, 0.53 * vary * grain, 0.50 * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 100, 1.0, 8.0, 0.4, 0.45)
    for lon, lat, rx, ry, tint in ((80.0, -30.0, 55.0, 35.0, (0.68, 0.66, 0.60)),
                                   (210.0, 10.0, 65.0, 40.0, (0.44, 0.42, 0.40)),
                                   (320.0, -5.0, 45.0, 30.0, (0.66, 0.64, 0.60))):
        add_ellipse(img, lon, lat, rx, ry, tint, 0.95)  # coronae
        for f in (1.0, 0.75, 0.5, 0.28):
            add_ring(img, lon, lat, rx * f, 2.0, (0.42, 0.40, 0.38), 0.7)
        add_crack(img, rng, lon, lat, 40, 2.0, 2.5, (0.35, 0.33, 0.31), 0.6, 0.2)
    for _ in range(4):  # cliffs
        add_crack(img, rng, rng.f() * 360.0, math.degrees(math.asin(2.0 * rng.f() - 1.0)),
                  100, 2.0, 2.5, (0.35, 0.33, 0.31), 0.6, 0.12)


def moon_triton(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        d1 = fbm(u * 10.0, v * 10.0, 10, S + 3, 3)
        dimple = 0.88 + 0.12 * smoothstep(0.42, 0.58, d1)  # cantaloupe
        n = fbm(u * 5.0, v * 5.0, 5, S + 1, 3)
        vary = (0.95 + 0.10 * n) * dimple
        wl = lat + (fbm(u * 5.0, v * 5.0, 5, S + 11, 3) - 0.5) * 36.0
        cap = smoothstep(-18.0, -34.0, wl)
        r, g, b = mix3((0.84, 0.74, 0.70), (0.93, 0.89, 0.87), cap)
        grain = 0.97 + 0.06 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (r * vary * grain, g * vary * grain, b * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 20, 1.5, 7.0, 0.3, 0.35)
    for _ in range(55):  # dark wind streaks on the cap
        add_ellipse(img, rng.f() * 360.0, -rng.range(20.0, 70.0),
                    rng.range(6.0, 28.0), rng.range(1.5, 3.5),
                    (0.45, 0.35, 0.33), 0.65)


def moon_charon(img, rng, S):
    w = img.w

    def fn(u, v, lon, lat):
        n = fbm(u * 7.0, v * 7.0, 7, S + 1, 3)
        vary = 0.92 + 0.16 * n
        wl = lat + (fbm(u * 4.0, v * 4.0, 4, S + 13, 3) - 0.5) * 30.0
        mordor = smoothstep(52.0, 68.0, wl)
        r, g, b = mix3((0.57, 0.52, 0.47), (0.48, 0.28, 0.19), mordor)
        grain = 0.965 + 0.07 * hash2(int(u * w), int(v * w // 2), S + 9)
        return (r * vary * grain, g * vary * grain, b * vary * grain)

    base_fill(img, S, fn)
    add_craters(img, rng, 90, 1.0, 9.0, 0.45, 0.5)
    add_blotch(img, 0.0, -20.0, 120.0, (0.50, 0.42, 0.38), 0.8)  # Vulcan Planitia
    add_craters(img, rng, 15, 1.0, 5.0, 0.35, 0.4)
    add_polyline(img, [(-80.0, -5.0), (-20.0, 5.0), (50.0, -10.0)],
                 2.5, (0.35, 0.30, 0.27), 0.7)  # Serenity Chasma


# ---------------------------------------------------------------------------
# Roster + driver.
# ---------------------------------------------------------------------------

MOONS = [
    ("luna", 2048, 1024, moon_luna),
    ("phobos", 1024, 512, moon_phobos),
    ("deimos", 1024, 512, moon_deimos),
    ("io", 1024, 512, moon_io),
    ("europa", 1024, 512, moon_europa),
    ("ganymede", 1024, 512, moon_ganymede),
    ("callisto", 1024, 512, moon_callisto),
    ("mimas", 1024, 512, moon_mimas),
    ("enceladus", 1024, 512, moon_enceladus),
    ("tethys", 1024, 512, moon_tethys),
    ("dione", 1024, 512, moon_dione),
    ("rhea", 1024, 512, moon_rhea),
    ("titan", 1024, 512, moon_titan),
    ("iapetus", 1024, 512, moon_iapetus),
    ("hyperion", 1024, 512, moon_hyperion),
    ("ariel", 1024, 512, moon_ariel),
    ("umbriel", 1024, 512, moon_umbriel),
    ("titania", 1024, 512, moon_titania),
    ("oberon", 1024, 512, moon_oberon),
    ("miranda", 1024, 512, moon_miranda),
    ("triton", 1024, 512, moon_triton),
    ("charon", 1024, 512, moon_charon),
]


def contact_sheet(images, path):
    """images: [(name, Img)]. 6-column thumbnails, roster order."""
    tw, th, pad, cols = 240, 120, 8, 6
    rows = (len(images) + cols - 1) // cols
    sw = cols * (tw + pad) + pad
    sh = rows * (th + pad) + pad
    sheet = Img(sw, sh)
    spx = sheet.px
    for i in range(sw * sh * 3):
        spx[i] = 20
    for idx, (_, img) in enumerate(images):
        ox = pad + (idx % cols) * (tw + pad)
        oy = pad + (idx // cols) * (th + pad)
        sx = img.w / tw
        sy = img.h / th
        for y in range(th):
            srcy = min(int(y * sy), img.h - 1)
            for x in range(tw):
                srcx = min(int(x * sx), img.w - 1)
                si = (srcy * img.w + srcx) * 3
                di = ((oy + y) * sw + ox + x) * 3
                spx[di] = img.px[si]
                spx[di + 1] = img.px[si + 1]
                spx[di + 2] = img.px[si + 2]
    sheet.save_ppm(path)


def main(argv):
    outdir = None
    sheet = None
    only = None
    quiet = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--sheet" and i + 1 < len(argv):
            i += 1
            sheet = argv[i]
        elif a == "--only" and i + 1 < len(argv):
            i += 1
            only = argv[i]
        elif a == "--quiet":
            quiet = True
        elif not a.startswith("-") and outdir is None:
            outdir = a
        else:
            print("usage: GenerateMoonTextures.py [outdir] [--sheet path] [--only name] [--quiet]")
            return 1
        i += 1
    if outdir is None:
        # Engine layout: <root>/Tools/GenerateMoonTextures.py -> <root>/EngineContent/...
        here = os.path.dirname(os.path.abspath(__file__))
        candidate = os.path.join(os.path.dirname(here), "EngineContent", "CelestialTextures", "Moons")
        if os.path.basename(here) == "Tools" and os.path.isdir(os.path.dirname(candidate)):
            outdir = candidate
        else:
            outdir = os.path.join(os.getcwd(), "Moons")
    os.makedirs(outdir, exist_ok=True)
    made = []
    for name, w, h, recipe in MOONS:
        if only is not None and name != only:
            continue
        seed = zlib.crc32(name.encode("ascii")) & MASK32
        img = Img(w, h)
        recipe(img, Rng(seed ^ 0x51ED), seed)
        res = "2k" if w >= 2048 else "1k"
        path = os.path.join(outdir, "%s_%s.ppm" % (name, res))
        img.save_ppm(path)
        made.append((name, img, path))
        if not quiet:
            print("%-10s %dx%d %s" % (name, w, h, img.sha256()))
            sys.stdout.flush()
    if sheet is not None:
        contact_sheet([(n, im) for n, im, _ in made], sheet)
        if not quiet:
            print("sheet %s" % sheet)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

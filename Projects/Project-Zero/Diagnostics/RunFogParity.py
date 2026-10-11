#!/usr/bin/env python3
"""Fog parity gate: F1 density + F2 march vs numpy, F3 outdoor proofs.

F1/F2 transcribe FogSpecification.slang in float32 with identical operation
order (integer-hash noise included). F3 renders the three FogViewport
scenarios, checks determinism (byte-identical rerun), and records the
scenario-vs-clear effect sizes. Writes FogParity.txt + Proof/fog_*.png.
Exit code 0 iff every gate passes.
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "Host"
PROOF = ROOT / "Diagnostics" / "Proof"
REPORT = ROOT / "Diagnostics" / "FogParity.txt"

F32 = np.float32
U32 = np.uint32


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), F32(0.0), F32(1.0))
    return t * t * (F32(3.0) - F32(2.0) * t)


def lerp(a, b, t):
    return a + (b - a) * t


def fog_hash(cell):
    # cell: (n,3) uint32. Wrapping arithmetic matches C++ unsigned exactly.
    with np.errstate(over="ignore"):
        q = cell * U32(374761393) + U32(668265263)
        q = (q ^ (q >> U32(13))) * U32(1274126177)
        h = q[:, 0] ^ q[:, 1] ^ q[:, 2]
    return (h % U32(1024)).astype(np.float32) * F32(0.0009765625)


def fog_noise(p):
    # p: (n,3) float32. Quintic fade + trilinear, C++ association.
    base = np.floor(p).astype(np.int32)
    f = p - np.floor(p)
    f2 = f * f
    f3 = f2 * f
    inner = f * (f * F32(6.0) - F32(15.0)) + F32(10.0)
    u = f3 * inner
    c = {}
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                key = (dx, dy, dz)
                cc = base.copy()
                cc[:, 0] += dx
                cc[:, 1] += dy
                cc[:, 2] += dz
                c[key] = fog_hash(cc.astype(np.uint32))
    ux, uy, uz = u[:, 0], u[:, 1], u[:, 2]
    return lerp(lerp(lerp(c[(0, 0, 0)], c[(1, 0, 0)], ux),
                     lerp(c[(0, 1, 0)], c[(1, 1, 0)], ux), uy),
                lerp(lerp(c[(0, 0, 1)], c[(1, 0, 1)], ux),
                     lerp(c[(0, 1, 1)], c[(1, 1, 1)], ux), uy), uz)


def fog_fractal(p):
    v = fog_noise(p) * F32(0.57142857)
    v = v + fog_noise(p * F32(2.03) + F32(17.17)) * F32(0.28571429)
    v = v + fog_noise(p * F32(4.12) + F32(41.41)) * F32(0.14285714)
    return v


def fog_medium(p, g):
    # p: (n,3) float32 world positions. g: dict of (n,) / (n,3) config arrays.
    n = p.shape[0]
    hgt = p[:, 1] - g["gbase"]
    gext = g["gdens"] * np.exp(-np.maximum(hgt, F32(0.0)) * g["gfall"])
    ext = gext.copy()
    asum = g["galb"] * gext[:, None]
    gsum = g["gg"] * gext
    esum = np.zeros((n, 3), dtype=np.float32)
    for v in (0, 1):
        active = (g["count"] > v) & (g["vdens"][v] > F32(0.0))
        c = g["vc"][v]
        e = g["ve"][v]
        is_sphere = g["vshape"][v] == 1
        # NOTE: box divides by per-axis safe extents; sphere divides by the
        # guarded radius (both match the .slang exactly).
        safe = np.maximum(e, F32(1e-6))
        q = (p - c) / safe
        qs = (p - c) / np.maximum(e[:, 0:1], F32(1e-6))
        dbox = np.maximum(np.abs(q[:, 0]),
                          np.maximum(np.abs(q[:, 1]), np.abs(q[:, 2])))
        rs = np.sqrt((qs[:, 0] * qs[:, 0] + qs[:, 1] * qs[:, 1])
                     + qs[:, 2] * qs[:, 2])
        mask = np.where(is_sphere,
                        F32(1.0) - smoothstep(F32(0.7), F32(1.0), rs),
                        F32(1.0) - smoothstep(F32(0.7), F32(1.0), dbox))
        hf = np.exp(-np.maximum(p[:, 1] - c[:, 1], F32(0.0))
                     * g["vfall"][v])
        nz = fog_fractal(p * g["vnscale"][v][:, None]
                         + g["time"][:, None] * F32(0.01))
        vd = (g["vdens"][v] * mask * hf
              * (F32(1.0) - g["vnamt"][v]
                 + g["vnamt"][v] * nz))
        vd = np.where(active & (mask > F32(0.0)), vd, F32(0.0))
        ext = ext + vd
        asum = asum + g["valb"][v] * vd[:, None]
        gsum = gsum + g["vg"][v] * vd
        esum = esum + g["vemi"][v] * vd[:, None]
    inv = F32(1.0) / np.maximum(ext, F32(1e-9))
    return ext, asum * inv[:, None], gsum * inv, esum * inv[:, None]


def fog_march(o, d, tmin, tmax, ld, lr, vis, g):
    n = o.shape[0]
    s0 = np.full((n, 2), np.float32(1.0))
    s1 = np.full((n, 2), np.float32(-1.0))
    has = np.zeros((n, 2), dtype=bool)
    for v in (0, 1):
        active = g["count"] > v
        c = g["vc"][v]
        e = g["ve"][v]
        is_sphere = g["vshape"][v] == 1
        # box slab (branched per axis, like the .slang)
        lo = c - e
        hi = c + e
        a = np.full(n, np.float32(-1e30))
        b = np.full(n, np.float32(1e30))
        miss = np.zeros(n, dtype=bool)
        for ax in range(3):
            oo = o[:, ax]
            dd = d[:, ax]
            parallel = np.abs(dd) < F32(1e-9)
            miss = miss | (parallel & ((oo < lo[:, ax]) | (oo > hi[:, ax])))
            ta = (lo[:, ax] - oo) / dd
            tb = (hi[:, ax] - oo) / dd
            a = np.where(parallel, a,
                         np.maximum(a, np.minimum(ta, tb)))
            b = np.where(parallel, b,
                         np.minimum(b, np.maximum(ta, tb)))
        abox, bbox = a, np.where(miss, np.float32(-1e30), b)
        # sphere quadratic
        toc = c - o
        tca = toc[:, 0] * d[:, 0] + toc[:, 1] * d[:, 1] + toc[:, 2] * d[:, 2]
        # oc = o - d*dot(d, o-c); done as closest-approach for clarity:
        along = (o[:, 0] - c[:, 0]) * d[:, 0] \
            + (o[:, 1] - c[:, 1]) * d[:, 1] \
            + (o[:, 2] - c[:, 2]) * d[:, 2]
        oc = o - d * along[:, None]
        dd2 = ((oc[:, 0] - c[:, 0]) ** 2 + (oc[:, 1] - c[:, 1]) ** 2
               + (oc[:, 2] - c[:, 2]) ** 2)
        r2 = e[:, 0] * e[:, 0]
        smiss = dd2 > r2
        thc = np.sqrt(np.maximum(r2 - dd2, F32(0.0)))
        asp, bsp = tca - thc, np.where(smiss, tca - thc - F32(1.0), tca + thc)
        hit0 = np.where(is_sphere, asp, abox)
        hit1 = np.where(is_sphere, bsp, bbox)
        a = np.maximum(hit0, tmin)
        b = np.minimum(hit1, np.minimum(tmax, g["maxmarch"]))
        keep = active & (a < b)
        s0[:, v] = np.where(keep, a, F32(1.0))
        s1[:, v] = np.where(keep, b, F32(-1.0))
        has[:, v] = keep
    # sort 2 segments by start, merge overlaps
    swap = s0[:, 1] < s0[:, 0]
    s0 = np.where(swap[:, None], s0[:, ::-1], s0)
    s1 = np.where(swap[:, None], s1[:, ::-1], s1)
    has = np.where(swap[:, None], has[:, ::-1], has)
    m0 = [s0[:, 0].copy()]
    m1 = [s1[:, 0].copy()]
    mh = [has[:, 0].copy()]
    overlap = has[:, 1] & mh[0] & (s0[:, 1] <= m1[0])
    m1[0] = np.where(overlap, np.maximum(m1[0], s1[:, 1]), m1[0])
    second = has[:, 1] & ~overlap
    m0.append(np.where(second, s0[:, 1], F32(1.0)))
    m1.append(np.where(second, s1[:, 1], F32(-1.0)))
    mh.append(second)
    # Global height-fog span (see FogSpecification): when the global density
    # is on and the surviving span is non-empty, the merged set is exactly
    # [tmin, min(tmax, maxmarch)] — the global segment swallows every volume
    # segment, which the .slang always clamps inside it.
    fullb = np.minimum(tmax, g["maxmarch"])
    gspan = (g["gdens"] > 0) & (tmin < fullb)
    m0[0] = np.where(gspan, tmin, m0[0])
    m1[0] = np.where(gspan, fullb, m1[0])
    mh[0] = np.where(gspan, True, mh[0])
    mh[1] = np.where(gspan, False, mh[1])
    # Scatter cosine: dot(dir, lightDir) (see FogSpecification comment).
    cost = (d[:, 0] * ld[:, 0] + d[:, 1] * ld[:, 1]
            + d[:, 2] * ld[:, 2])
    scatter = np.zeros((n, 3), dtype=np.float32)
    tr = np.ones(n, dtype=np.float32)
    steps = g["steps"].astype(np.int32)
    for s in range(2):
        valid = mh[s]
        ln = m1[s] - m0[s]
        dt = ln / np.maximum(steps.astype(np.float32), F32(1.0))
        for i in range(int(steps.max())):
            do = valid & (i < steps)
            tm = m0[s] + (i + F32(0.5)) * dt
            pp = o + d * tm[:, None]
            ext, alb, gg, emi = fog_medium(pp, g)
            sigs = alb * ext[:, None]
            g2 = gg * gg
            den = F32(1.0) + g2 - F32(2.0) * gg * cost
            phase = ((F32(1.0) - g2)
                     / (F32(12.56637061) * den * np.sqrt(den)))
            src = (emi * ext[:, None]
                   + sigs * (g["amb"]
                             + lr * (phase * vis)[:, None]))
            trs = np.exp(-ext * dt)
            sint = (F32(1.0) - trs) / np.maximum(ext, F32(1e-6))
            delta = src * sint[:, None]
            use = do & (ext > F32(1e-6))
            scatter = scatter + np.where(use[:, None],
                                         delta * tr[:, None], F32(0.0))
            tr = tr * np.where(use, trs, F32(1.0))
    return scatter, tr


def load_fog_csv(path):
    rows = np.loadtxt(str(path), delimiter=",", dtype=np.float64)
    f = lambda v: v.astype(np.float32)
    g = {
        "gdens": f(rows[:, 0]), "gfall": f(rows[:, 1]),
        "gbase": f(rows[:, 2]), "galb": f(rows[:, 3:6]),
        "gg": f(rows[:, 6]), "amb": f(rows[:, 7:10]),
        "time": f(rows[:, 10]), "steps": rows[:, 11].astype(np.int32),
        "maxmarch": f(rows[:, 12]), "count": rows[:, 13].astype(np.int32),
    }
    for v, base in ((0, 14), (1, 32)):
        g["vshape"] = g.get("vshape", []) + [rows[:, base].astype(np.int32)]
        g["vc"] = g.get("vc", []) + [f(rows[:, base + 1:base + 4])]
        g["ve"] = g.get("ve", []) + [f(rows[:, base + 4:base + 7])]
        g["vdens"] = g.get("vdens", []) + [f(rows[:, base + 7])]
        g["vfall"] = g.get("vfall", []) + [f(rows[:, base + 8])]
        g["valb"] = g.get("valb", []) + [f(rows[:, base + 9:base + 12])]
        g["vg"] = g.get("vg", []) + [f(rows[:, base + 12])]
        g["vemi"] = g.get("vemi", []) + [f(rows[:, base + 13:base + 16])]
        g["vnamt"] = g.get("vnamt", []) + [f(rows[:, base + 16])]
        g["vnscale"] = g.get("vnscale", []) + [f(rows[:, base + 17])]
    o = f(rows[:, 50:53])
    d = f(rows[:, 53:56])
    tmin, tmax = f(rows[:, 56]), f(rows[:, 57])
    ld = f(rows[:, 58:61])
    lr = f(rows[:, 61:64])
    vis = f(rows[:, 64])
    ref = {
        "d0": f(rows[:, 65]), "d1": f(rows[:, 66]),
        "scatter": f(rows[:, 67:70]), "tr": f(rows[:, 70]),
    }
    return g, o, d, tmin, tmax, ld, lr, vis, ref


def gate_density(g, o, d, tmin, tmax, ref):
    mid = o + d * (((tmin + tmax) * F32(0.5))[:, None])
    got0, _, _, _ = fog_medium(o, g)
    got1, _, _, _ = fog_medium(mid, g)
    err0 = np.abs(got0.astype(np.float64) - ref["d0"].astype(np.float64))
    err1 = np.abs(got1.astype(np.float64) - ref["d1"].astype(np.float64))
    tol0 = 1e-6 + 1e-6 * np.abs(ref["d0"].astype(np.float64))
    tol1 = 1e-6 + 1e-6 * np.abs(ref["d1"].astype(np.float64))
    worst = max((err0 / tol0).max(), (err1 / tol1).max())
    return worst


def gate_march(g, o, d, tmin, tmax, ld, lr, vis, ref):
    got_s, got_t = fog_march(o, d, tmin, tmax, ld, lr, vis, g)
    errs = np.abs(got_s.astype(np.float64) - ref["scatter"].astype(np.float64))
    tols = 1e-5 + 1e-5 * np.abs(ref["scatter"].astype(np.float64))
    errt = np.abs(got_t.astype(np.float64) - ref["tr"].astype(np.float64))
    tolt = 1e-6 + 1e-6 * np.abs(ref["tr"].astype(np.float64))
    return max((errs / tols).max(), (errt / tolt).max())


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:])
        print(r.stderr[-2000:])
        raise SystemExit("command failed: %s" % " ".join(cmd))
    return r.stdout


def main():
    lines = []
    run(["make", "FogProbe", "FogViewport"], HOST)
    run([str(HOST / "FogProbe"), "--rows", "2000",
         "--out", str(PROOF / "fog.csv")], HOST)
    g, o, d, tmin, tmax, ld, lr, vis, ref = load_fog_csv(PROOF / "fog.csv")
    n = o.shape[0]
    w1 = gate_density(g, o, d, tmin, tmax, ref)
    ok1 = w1 <= 1.0
    lines.append("F1 density-numpy (%d rows x2 points): worst %.3f tol -> %s"
                 % (n, w1, "PASS" if ok1 else "FAIL"))
    w2 = gate_march(g, o, d, tmin, tmax, ld, lr, vis, ref)
    ok2 = w2 <= 1.0
    lines.append("F2 march-numpy (%d rows): worst %.3f tol -> %s"
                 % (n, w2, "PASS" if ok2 else "FAIL"))
    cases = [
        ("clear", ["--sun", "6.4", "--yaw", "35", "--pitch", "-6",
                   "--cam", "0,2.2,14", "--fog", "clear"]),
        ("morning", ["--sun", "6.4", "--yaw", "35", "--pitch", "-6",
                     "--cam", "0,2.2,14", "--fog", "morning"]),
        ("backlit", ["--sun", "6.4", "--yaw", "75", "--pitch", "4",
                     "--cam", "0,2.2,14", "--fog", "backlit"]),
    ]
    for name, args in cases:
        run([str(HOST / "FogViewport")] + args
            + ["--width", "480", "--height", "270", "--grain", "0.1",
               "--out", str(PROOF / ("fog_%s.ppm" % name))], HOST)
        Image.open(str(PROOF / ("fog_%s.ppm" % name))).save(
            str(PROOF / ("fog_%s.png" % name)))
    # determinism: morning twice, byte-identical
    run([str(HOST / "FogViewport")] + cases[1][1]
        + ["--width", "480", "--height", "270", "--grain", "0",
           "--out", str(PROOF / "fog_det_a.ppm")], HOST)
    run([str(HOST / "FogViewport")] + cases[1][1]
        + ["--width", "480", "--height", "270", "--grain", "0",
           "--out", str(PROOF / "fog_det_b.ppm")], HOST)
    a = (PROOF / "fog_det_a.ppm").read_bytes()
    b = (PROOF / "fog_det_b.ppm").read_bytes()
    ok3 = a == b
    lines.append("F3 determinism (morning, grain 0, 2 runs): %s"
                 % ("PASS identical %d bytes"
                    % len(a) if ok3 else "FAIL"))
    (PROOF / "fog_det_a.ppm").unlink()
    (PROOF / "fog_det_b.ppm").unlink()
    base = np.asarray(Image.open(str(PROOF / "fog_clear.png"))).astype(float)
    for name in ("morning", "backlit"):
        img = np.asarray(Image.open(str(PROOF / ("fog_%s.png" % name)))
                         ).astype(float)
        dd = np.abs(img - base)
        lines.append("F3 effect %s-vs-clear: mean|d| %.1f max|d| %.0f"
                     % (name, dd.mean(), dd.max()))
    overall = ok1 and ok2 and ok3
    lines.append("OVERALL: %s" % ("PASS" if overall else "FAIL"))
    REPORT.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0 if overall else 1


if __name__ == "__main__":
    sys.exit(main())

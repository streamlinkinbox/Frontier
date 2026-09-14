#!/usr/bin/env python3
"""Parity gate: the shipped .slang core vs both independent truths.

G1: SkyViewport vs the oracle (Tools/SkyReference/*.png) — full frame,
    including the planet ground the oracle implements.
G2: SkyViewport vs CpuPortDiff (vendored upstream transcription) — sky mask
    (dir.y > 0.02); the port's beauty path omits the planet-ground branch
    (documented in Host/CpuPort/PROVENANCE.md), so ground pixels are excluded
    here and covered by G1 instead.
Also runs CheckCelestialSlang.py and (unless --skip-restir) ReSTIRConvergence.

Thresholds: MAE < 0.05 LSB, max <= 1 LSB, differing fraction < 0.001.
Exit code 0 iff every gate passes. Writes CelestialParity.txt + Proof/*.png.
"""
import math
import subprocess
import sys
from pathlib import Path

from PIL import Image
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "Host"
ORACLE = Path("/home/user/Frontier/Tools/SkyReference")
PROOF = ROOT / "Diagnostics/Proof"
REPORT = ROOT / "Diagnostics/CelestialParity.txt"

CASES = [
    (6.4, 35.0, -4.0, "0640_default"),
    (6.4, 35.0, 18.0, "0640_lookup"),
    (7.6, 35.0, 18.0, "0760_lookup"),
    (12.0, 35.0, 18.0, "1200_lookup"),
    (6.4, 87.3, 5.4, "0640_sun"),
    (7.6, 79.0, 21.4, "0760_sun"),
]
W, H = 480, 270
MAE_TOL, MAX_TOL, FRAC_TOL = 0.05, 1.0, 0.001


def sky_mask(yaw_deg, pitch_deg):
    """Pixels safely above the limb (dir.y > 0.02), panel camera math."""
    yaw = math.radians(yaw_deg)
    pitch = math.radians(pitch_deg)
    fwd = np.array([math.sin(yaw) * math.cos(pitch), math.sin(pitch),
                    -math.cos(yaw) * math.cos(pitch)])
    right = np.array([math.cos(yaw), 0.0, math.sin(yaw)])
    up = np.array([-math.sin(pitch) * math.sin(yaw), math.cos(pitch),
                   math.sin(pitch) * math.cos(yaw)])
    tan_h = math.tan(math.radians(72.0) / 2.0)
    xs = (np.arange(W, dtype=np.float64) + 0.5)
    ys = (np.arange(H, dtype=np.float64) + 0.5)
    uu = ((xs * 2.0 - W) / H)[None, :].repeat(H, axis=0)
    vv = (((H - ys) * 2.0 - H) / H)[:, None].repeat(W, axis=1)
    d = fwd[None, None, :] + right[None, None, :] * (uu * tan_h)[:, :, None] \
        + up[None, None, :] * (vv * tan_h)[:, :, None]
    d /= np.linalg.norm(d, axis=2, keepdims=True)
    return d[:, :, 1] > 0.02


def diff_stats(a, b, mask=None):
    d = np.abs(a.astype(float) - b.astype(float)).max(axis=2)
    if mask is not None:
        d = d[mask]
    else:
        d = d.ravel()
    return d.mean(), d.max(), float((d > 0).mean())


def main():
    skip_restir = "--skip-restir" in sys.argv
    PROOF.mkdir(parents=True, exist_ok=True)
    lines = []
    all_pass = True

    def note(s):
        print(s)
        lines.append(s)

    r = subprocess.run(["make", "SkyViewport", "CpuPortDiff"], cwd=HOST,
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:])
        print(r.stderr[-2000:])
        return 1

    r = subprocess.run([sys.executable, str(ROOT / "Diagnostics/CheckCelestialSlang.py")],
                       capture_output=True, text=True)
    note(r.stdout.strip())
    all_pass = all_pass and r.returncode == 0

    for sun, yaw, pitch, tag in CASES:
        slang_ppm = PROOF / ("%s_slang.ppm" % tag)
        port_ppm = PROOF / ("%s_port.ppm" % tag)
        base = ["--sun", str(sun), "--yaw", str(yaw), "--pitch", str(pitch),
                "--width", str(W), "--height", str(H), "--grain", "0.1"]
        subprocess.run([str(HOST / "SkyViewport")] + base + ["--out", str(slang_ppm)],
                       check=True, capture_output=True)
        subprocess.run([str(HOST / "CpuPortDiff")] + base + ["--out", str(port_ppm)],
                       check=True, capture_output=True)
        a = np.asarray(Image.open(slang_ppm))
        Image.fromarray(a).save(PROOF / ("%s_slang.png" % tag))
        o = np.asarray(Image.open(ORACLE / ("htmlsky_%s.png" % tag)))
        p = np.asarray(Image.open(port_ppm))

        mae, mx, frac = diff_stats(a, o)
        g1 = mae < MAE_TOL and mx <= MAX_TOL and frac < FRAC_TOL
        all_pass = all_pass and g1
        note("G1 oracle %s: MAE=%.4f max=%.0f diffrac=%.5f -> %s"
             % (tag, mae, mx, frac, "PASS" if g1 else "FAIL"))
        dm = (np.abs(a.astype(float) - o.astype(float)).max(axis=2) * 255).astype(np.uint8)
        Image.fromarray(dm).save(PROOF / ("%s_diff_oracle.png" % tag))

        mask = sky_mask(yaw, pitch)
        mae2, mx2, frac2 = diff_stats(a, p, mask)
        g2 = mae2 < MAE_TOL and mx2 <= MAX_TOL and frac2 < FRAC_TOL
        all_pass = all_pass and g2
        note("G2 cpu-port %s (sky %d%%): MAE=%.4f max=%.0f diffrac=%.5f -> %s"
             % (tag, round(100 * mask.mean()), mae2, mx2, frac2,
                "PASS" if g2 else "FAIL"))
        # G2's max<=1 over the mask doubles as limb confinement: any port
        # diff above dir.y>0.02 fails the gate (the documented omission
        # stays below the limb).

    if not skip_restir:
        rr = PROOF / "ReSTIRConvergence.txt"
        r = subprocess.run([str(HOST / "ReSTIRConvergence"), "--report", str(rr),
                            "--ppm", str(PROOF / "restir.ppm")],
                           capture_output=True, text=True, cwd=str(PROOF))
        note("ReSTIRConvergence: %s" % ("PASS" if r.returncode == 0 else "FAIL"))
        for line in r.stdout.split("\n"):
            if "T2 DI" in line or "T3 GI" in line or "determinism" in line or "OVERALL" in line:
                note("   " + line.strip())
        all_pass = all_pass and r.returncode == 0
        Image.open(PROOF / "restir.ppm").save(PROOF / "restir.png")
        Image.open(PROOF / "restir.ppm.mean.ppm").save(PROOF / "restir_mean.png")

    note("OVERALL: %s" % ("PASS" if all_pass else "FAIL"))
    REPORT.write_text("\n".join(lines) + "\n")
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())

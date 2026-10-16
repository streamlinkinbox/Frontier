"""Generate illustrative LM-63 Type C intensity tables for the exhibit.

These curves are authored demonstrations, not measured/certified IES data. The
runtime parser accepts measured TILT=NONE Type C files at these paths as well.
Angles use C-planes around each fixture's optical axis, not world coordinates.
"""
from math import atan2, cos, exp, pi, sin, degrees
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "Profiles"
ROOT.mkdir(exist_ok=True)


def gaussian(value, width):
    return exp(-0.5 * (value / width) ** 2)


def cutoff(height, threshold, softness=0.8):
    return 1.0 / (1.0 + exp(max(-60, min(60, (height - threshold) / softness))))


def sample(name, polar, azimuth):
    v, h = polar * pi / 180, azimuth * pi / 180
    # +up is C0, +right is C90. The vehicle is on a left-hand-traffic
    # road: its left verge (negative C angle) gets the low-beam upsweep.
    elevation = degrees(atan2(sin(v) * cos(h), cos(v)))
    lateral = degrees(atan2(sin(v) * sin(h), cos(v)))
    if name == "Roadway":
        # Downlight with wider throw away from the supporting arm.
        return max(0.0, cos(v)) ** 0.7 * (0.65 + 0.35 * gaussian(polar - 43, 27)) * (0.78 + 0.22 * max(0.0, cos(h)))
    if name == "Area":
        return max(0.0, cos(v)) ** 1.4 * (0.75 + 0.25 * gaussian(polar - 22, 30))
    if name == "LowBeam":
        upsweep = 1.5 if lateral < -12 else 0.0
        return gaussian(lateral, 24) * gaussian(elevation + 3.0, 7.5) * cutoff(elevation, 0.8 + upsweep, 0.55)
    if name == "HighBeam":
        return gaussian(lateral, 15) * gaussian(elevation + 1.0, 7.0)
    if name == "FogBeam":
        return gaussian(lateral, 48) * gaussian(elevation + 1.5, 4.5) * cutoff(elevation, 1.5, 0.8)
    if name == "RoofFlood":
        return gaussian(lateral, 32) * gaussian(elevation + 3.0, 11.0)
    if name == "RoofSpot":
        return gaussian(lateral, 13) * gaussian(elevation + 1.0, 7.0)
    raise ValueError(name)


for name in ("Roadway", "Area", "LowBeam", "HighBeam", "FogBeam", "RoofFlood", "RoofSpot"):
    vertical = list(range(0, 181, 2))
    horizontal = list(range(0, 361, 10))
    # Peak candela is encoded in the file for standard readers; the exhibit
    # normalises the shape and uses the inspector's calibrated scene intensity.
    scale = 1000.0
    grid = [max(0.0, sample(name, v, h)) * scale for h in horizontal for v in vertical]
    lines = ["IESNA:LM-63-2002", "[TEST] Frontier authored demonstration profile - not measured photometry",
             f"[LUMINAIRE] {name}", "TILT=NONE", f"1 1000 1 {len(vertical)} {len(horizontal)} 1 2 0.30 0.30 0.15",
             "1 1 60", " ".join(map(str, vertical)), " ".join(map(str, horizontal))]
    lines += [" ".join(f"{value:.5f}" for value in grid[i:i + len(vertical)]) for i in range(0, len(grid), len(vertical))]
    (ROOT / f"{name}.ies").write_text("\n".join(lines) + "\n", encoding="ascii")

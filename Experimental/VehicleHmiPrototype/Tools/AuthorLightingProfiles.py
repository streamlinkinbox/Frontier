"""Generate the AUTHORED LM-63 Type C intensity tables: the three automotive beams (LowBeam, HighBeam, FogBeam).

These curves are authored demonstrations, not measured or certified IES data (their headers say so). Roadway, Area, RoofFlood and RoofSpot are no longer authored: they are real
manufacturer / test-lab files copied verbatim into Profiles/ (Profiles/README.md, Profiles/PROVENANCE.json), and this script must never overwrite them.
Angles use C-planes around each fixture's optical axis, not world coordinates.

    python3 Tools/AuthorLightingProfiles.py [output directory]     (default: ../Profiles)
"""
import sys
from math import atan2, cos, exp, pi, sin, degrees
from pathlib import Path

AUTHORED = ("LowBeam", "HighBeam", "FogBeam")
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "Profiles"
ROOT.mkdir(exist_ok=True, parents=True)


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
    if name == "LowBeam":
        upsweep = 1.5 if lateral < -12 else 0.0
        return gaussian(lateral, 24) * gaussian(elevation + 3.0, 7.5) * cutoff(elevation, 0.8 + upsweep, 0.55)
    if name == "HighBeam":
        return gaussian(lateral, 15) * gaussian(elevation + 1.0, 7.0)
    if name == "FogBeam":
        return gaussian(lateral, 48) * gaussian(elevation + 1.5, 4.5) * cutoff(elevation, 1.5, 0.8)
    raise ValueError(name)


for name in AUTHORED:
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

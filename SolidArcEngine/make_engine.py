#!/usr/bin/env python3
"""Generate SolidArc journal for a high-bypass turbofan, modelled from the FetchCFD Pratt & Whitney rotor
assembly render and a cutaway turbofan reference (see README).  Global frame is right-handed Z-up; the
engine axis runs along +X through (x, 0, ZC).  Every blade is a lofted solid between a root and a tip
section with its own chord, stagger angle and twist, then copied with a radial array."""
import math, sys
CUTAWAY = "--exterior" not in sys.argv

ZC = 1.5                      # engine axis height above the ground grid
out = []
def E(s=""): out.append(s)

def section(x, r, chord, thick, stagger_deg, name):
    """Closed 4-point blade section at radius r (blade standing along +Z from the axis), lying in a plane z = const.
       Chord runs along +X rotated by stagger about Z (positive = leaning toward -Y), thickness across."""
    a = math.radians(stagger_deg)
    cx, sx = math.cos(a), math.sin(a)
    def P(u, v):               # u along chord (centred), v across thickness
        X = x + u * cx - v * sx
        Y = -u * sx - v * cx
        return f"({X:.4f},{Y:.4f},{ZC + r:.4f})"
    h = chord / 2.0; t = thick / 2.0
    pts = [P(-h, -t * 0.4), P(h, -t), P(h, t), P(-h, t * 0.4)]
    E(f"polyline {' '.join(pts)} --closed --name={name}")

def blade_row(stem, x, r_root, r_tip, chord_root, chord_tip, thick, stag_root, stag_tip, count, tint):
    section(x, r_root, chord_root, thick, stag_root, "S0")
    section(x, r_tip, chord_tip, thick * 0.6, stag_tip, "S1")
    E(f"loft S0 S1 --name={stem}")
    E("delete S0 S1")
    E(f"tint {stem} {tint}")
    E(f"radial {stem} --count={count} --axis=(0,0,{ZC}),(1,0,0) --name={stem}")

def disc(name, x0, x1, r, tint):
    E(f"cylinder ({x0},0,{ZC}) {r} {x1 - x0:.4f} --axis=(1,0,0) --name={name}")
    E(f"tint {name} {tint}")

def revolve_profile(name, pts_xr, tint, cut=False):
    """pts_xr: list of (x, r) going round the closed wall section."""
    pl = " ".join(f"({x:.4f},0,{ZC + r:.4f})" for x, r in pts_xr)
    E(f"polyline {pl} --closed --name={name}Profile")
    E(f"revolve {name}Profile 360 --origin=(0,0,{ZC}) --axis=(1,0,0) --name={name}")
    E(f"delete {name}Profile")
    E(f"tint {name} {tint}")
    if cut and CUTAWAY:   # cutaway: remove the quadrant y<0, z>axis (facing the default iso camera); nudged off the seams
        E(f"box (-2,-4.021,{ZC + 0.017:.3f}) 9 4 4 --name=CutTool")
        E(f"boolean subtract {name} -- CutTool --name={name}")

GREY, LIGHT, DARK, STEEL, COPPER, TITAN = "0.72 0.74 0.78", "0.86 0.87 0.90", "0.38 0.40 0.45", "0.62 0.66 0.72", "0.80 0.50 0.32", "0.55 0.58 0.66"

E("# SolidArc native document v1")
E("# High-bypass turbofan — modelled from CAD reference renders (FetchCFD P&W rotor assembly; turbofan cutaway).")
E("# Right-handed, Z up. Engine axis = +X through (x, 0, 1.5). Flow runs -X (inlet) to +X (nozzle).")
E("reset")

# ── Fan module ───────────────────────────────────────────────────────────────────────────────────────
E("# fan module: spinner, fan disc, 22 wide-chord swept fan blades")
E(f"cone (0.45,0,{ZC}) 0.36 0.02 0.78 --axis=(-1,0,0) --name=Spinner"); E(f"tint Spinner {LIGHT}")
disc("FanDisc", 0.45, 0.80, 0.44, DARK)
blade_row("FanBlade", 0.62, 0.42, 1.18, 0.30, 0.46, 0.035, 20, 58, 22, STEEL)
E(f"cone (0.80,0,{ZC}) 0.44 0.50 0.22 --axis=(1,0,0) --name=FanHubCone"); E(f"tint FanHubCone {DARK}")

# ── LP shaft + booster (LP compressor) ──────────────────────────────────────────────────────────────
E("# low-pressure spool: shaft, 3 booster stages")
E(f"cylinder (0.80,0,{ZC}) 0.11 3.55 --axis=(1,0,0) --name=LPShaft"); E(f"tint LPShaft {STEEL}")
E(f"cylinder (1.02,0,{ZC}) 0.30 0.52 --axis=(1,0,0) --name=BoosterDrum"); E(f"tint BoosterDrum {DARK}")
for i, x in enumerate((1.06, 1.24, 1.42)):
    disc(f"BoosterDisc{i+1}", x, x + 0.08, 0.50, DARK)
    blade_row(f"BoosterBlade{i+1}", x + 0.04, 0.49, 0.72 - 0.02 * i, 0.07, 0.07, 0.014, 30, 50, 28, STEEL)

# ── HP compressor drum, 7 stages tapering toward the combustor ──────────────────────────────────────
E("# high-pressure compressor: 7 bladed stages on a tapering drum")
E(f"cone (1.60,0,{ZC}) 0.40 0.32 1.00 --axis=(1,0,0) --name=HPCDrum"); E(f"tint HPCDrum {DARK}")
for i in range(7):
    x = 1.64 + i * 0.135
    r_root = 0.40 - 0.012 * i
    r_tip = 0.62 - 0.018 * i
    disc(f"HPCDisc{i+1}", x, x + 0.06, r_root + 0.02, TITAN)
    blade_row(f"HPCBlade{i+1}", x + 0.03, r_root, r_tip, 0.055, 0.05, 0.010, 32, 48, 34, STEEL)

# ── Combustor (annular) + fuel nozzles ──────────────────────────────────────────────────────────────
E("# annular combustor with 14 fuel nozzle bosses")
revolve_profile("Combustor", [(2.62, 0.30), (2.62, 0.66), (2.72, 0.70), (3.02, 0.70), (3.10, 0.62), (3.10, 0.34), (3.00, 0.28), (2.72, 0.28)], COPPER)
E(f"cylinder (2.56,0,{ZC + 0.48}) 0.045 0.10 --axis=(1,0,0) --name=FuelNozzle"); E(f"tint FuelNozzle {STEEL}")
E(f"radial FuelNozzle --count=14 --axis=(0,0,{ZC}),(1,0,0) --name=FuelNozzle")

# ── Turbines ────────────────────────────────────────────────────────────────────────────────────────
E("# high-pressure turbine (1 stage) and low-pressure turbine (3 stages, growing radius)")
E(f"cylinder (3.12,0,{ZC}) 0.24 0.78 --axis=(1,0,0) --name=TurbineDrum"); E(f"tint TurbineDrum {DARK}")
disc("HPTDisc", 3.16, 3.24, 0.42, TITAN)
blade_row("HPTBlade", 3.20, 0.41, 0.60, 0.07, 0.07, 0.012, -30, -45, 46, COPPER)
for i, x in enumerate((3.36, 3.54, 3.72)):
    disc(f"LPTDisc{i+1}", x, x + 0.08, 0.38, TITAN)
    blade_row(f"LPTBlade{i+1}", x + 0.04, 0.37, 0.60 + 0.05 * i, 0.08, 0.09, 0.012, -28, -42, 50, STEEL)  # tips 0.60-0.70 stay inside the nozzle (r 0.74)

# ── Exhaust ─────────────────────────────────────────────────────────────────────────────────────────
E("# exhaust: plug cone and core nozzle")
E(f"cone (3.84,0,{ZC}) 0.34 0.03 0.95 --axis=(1,0,0) --name=ExhaustPlug"); E(f"tint ExhaustPlug {TITAN}")
revolve_profile("CoreNozzle", [(3.30, 0.78), (3.90, 0.76), (4.25, 0.62), (4.25, 0.58), (3.90, 0.72), (3.30, 0.74)], TITAN, cut=True)

# ── Casings: core cowl, fan case / nacelle, OGVs ────────────────────────────────────────────────────
E("# core cowl (cutaway), outlet guide vanes, nacelle with inlet lip (cutaway)")
revolve_profile("CoreCowl", [(0.98, 0.76), (1.60, 0.84), (2.40, 0.84), (3.00, 0.80), (3.32, 0.80), (3.32, 0.76), (3.00, 0.76), (2.40, 0.80), (1.60, 0.80), (0.98, 0.72)], GREY, cut=True)
E(f"box (1.10,-0.006,{ZC + 0.84}) 0.26 0.012 0.42 --name=OGV"); E(f"tint OGV {STEEL}")
E(f"radial OGV --count=36 --axis=(0,0,{ZC}),(1,0,0) --name=OGV")
revolve_profile("Nacelle", [(-0.60, 1.30), (-0.72, 1.42), (-0.64, 1.56), (-0.35, 1.66), (0.30, 1.72), (1.50, 1.72), (2.40, 1.64), (3.05, 1.50), (3.05, 1.42), (2.40, 1.36), (1.50, 1.30), (0.30, 1.28), (-0.25, 1.28), (-0.50, 1.28)], LIGHT, cut=True)

# ── Pylon + accessories ─────────────────────────────────────────────────────────────────────────────
E("# pylon stub on top, accessory gearbox and two service pipes underneath")
E(f"box (0.70,-0.22,{ZC + 1.60}) 1.90 0.44 0.55 --name=Pylon"); E(f"tint Pylon {GREY}")
E(f"box (1.50,-0.28,{ZC - 1.12}) 0.95 0.56 0.26 --name=Gearbox"); E(f"tint Gearbox {DARK}")
E(f"cylinder (1.00,0.18,{ZC - 0.92}) 0.03 1.90 --axis=(1,0,0) --name=PipeA"); E(f"tint PipeA {STEEL}")
E(f"cylinder (1.20,-0.20,{ZC - 0.96}) 0.025 1.50 --axis=(1,0,0) --name=PipeB"); E(f"tint PipeB {COPPER}")
E("show shading plastic")
E("view iso")
E("view fit")

open("/home/user/Frontier/SolidArcEngine/" + ("Turbofan_Cutaway.arc" if CUTAWAY else "Turbofan_Exterior.arc"), "w").write("\n".join(out) + "\n")
print(len(out), "lines")

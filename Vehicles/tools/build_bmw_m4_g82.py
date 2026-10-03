#!/usr/bin/env python3
"""Build a complete SolidArc CAD model (.arc) of the modern BMW M4 Competition Coupe (G82)
from its 2D orthographic CAD blueprint (Side, Top, Front, Rear views).

Outputs:
  - Vehicles/BMW_M4_G82/BMW_M4_G82_2D_Sketch.arc  (2D/3D orthographic CAD sketch curves)
  - Vehicles/BMW_M4_G82/BMW_M4_G82.arc            (Complete 3D SolidArc B-rep + NURBS CAD model)
"""
import math
import os
import shutil
from pathlib import Path
import numpy as np
from scipy.interpolate import PchipInterpolator

ROOT = Path(__file__).resolve().parent.parent.parent
OUTDIR = ROOT / "Vehicles" / "BMW_M4_G82"
SOLIDARC_DIR = OUTDIR / "SolidArc"
OUTDIR.mkdir(parents=True, exist_ok=True)
SOLIDARC_DIR.mkdir(parents=True, exist_ok=True)

# Copy reference blueprints from image-search if available
ref1 = ROOT / "image-search" / "car-blueprints-bmw-m3-or-m4-or-m5-or-m6--2.jpg"
ref2 = ROOT / "image-search" / "car-blueprints-bmw-m3-or-m4-or-m5-or-m6--3.jpg"
if ref1.exists():
    shutil.copyfile(ref1, OUTDIR / "blueprint_bmw_m4_g82_4view.jpg")
if ref2.exists():
    shutil.copyfile(ref2, OUTDIR / "blueprint_bmw_m4_g82_sheet.jpg")

# ==============================================================================
# 1. EXACT FACTORY DIMENSIONS & ORTHOGRAPHIC 2D SKETCH PROFILES (METRES)
#    BMW M4 Competition Coupe (G82):
#      Length    = 4.794 m  (X in [-2.350, +2.444])
#      Wheelbase = 2.857 m  (X_R = -1.4285, X_F = +1.4285)
#      Width     = 1.887 m  (Half-width Y_max = 0.9435 m; w/ mirrors = 1.034 m)
#      Height    = 1.393 m  (Z in [0.000, 1.393])
# ==============================================================================
X_FRONT = 2.444
X_REAR = -2.350
X_AXLE_F = 1.4285
X_AXLE_R = -1.4285
R_TIRE_F = 0.340
R_TIRE_R = 0.348
Z_AXLE_F = R_TIRE_F
Z_AXLE_R = R_TIRE_R
R_ARCH_F = 0.380
R_ARCH_R = 0.388

def eval_rail(knots_xyz, x_query):
    """Evaluate a 3D feature rail defined by (x, y, z) blueprint knots at station x, clamped to knot domain."""
    pts = np.array(sorted(knots_xyz, key=lambda t: t[0]), dtype=float)
    xq = float(np.clip(x_query, pts[0, 0], pts[-1, 0]))
    fy = PchipInterpolator(pts[:, 0], pts[:, 1])
    fz = PchipInterpolator(pts[:, 0], pts[:, 2])
    return np.array([float(x_query), float(fy(xq)), float(fz(xq))])

# --- 2D Blueprint Side + Top Rails (X, Y, Z) for +Y (Right) Half-Body ---
# Well-spaced monotonic X stations from X = -2.28 (Rear Tail) to X = +2.34 (Front Nose)

# Rail 0: Centerline Top Profile (Y = 0.0)
RAIL_CENTER_TOP = [
    (-2.280, 0.000, 1.045),  # Ducktail Gurney spoiler trailing edge
    (-2.120, 0.000, 1.030),  # Trunk decklid aft
    (-1.740, 0.000, 1.045),  # Base of rear backlight window / trunk shutline
    (-1.250, 0.000, 1.215),  # Mid rear backlight window
    (-0.740, 0.000, 1.352),  # Top of rear backlight window / roof rear header
    (-0.200, 0.000, 1.393),  # Roof peak (exact 1393 mm height)
    ( 0.180, 0.000, 1.358),  # Windshield top header / roof front edge
    ( 0.540, 0.000, 1.150),  # Mid windshield
    ( 0.880, 0.000, 0.925),  # Windshield base / hood cowl
    ( 1.250, 0.000, 0.895),  # Hood rear power-dome center recess
    ( 1.680, 0.000, 0.850),  # Hood mid center channel
    ( 2.080, 0.000, 0.775),  # Hood forward slope
    ( 2.340, 0.000, 0.670),  # Hood leading edge / BMW roundel plinth
]

# Rail 1: Power-Dome / Roof Aero Channel / Trunk Inner Rail (Y ~ 0.21..0.36)
RAIL_DOME = [
    (-2.280, 0.260, 1.038),  # Ducktail spoiler inner
    (-2.120, 0.285, 1.024),
    (-1.740, 0.330, 1.040),  # Rear window base inner
    (-1.250, 0.340, 1.210),
    (-0.740, 0.340, 1.348),  # Roof rear channel
    (-0.200, 0.350, 1.388),  # CFRP roof longitudinal aero ridge
    ( 0.180, 0.355, 1.352),
    ( 0.540, 0.360, 1.145),
    ( 0.880, 0.350, 0.922),  # Cowl power-dome root
    ( 1.250, 0.320, 0.918),  # M4 Hood Power-Dome crest (+2.3 cm proud of center!)
    ( 1.680, 0.275, 0.872),  # M4 Hood Power-Dome ridge
    ( 2.080, 0.235, 0.785),  # Ridge flowing into outer kidney corner
    ( 2.340, 0.215, 0.665),  # Top outer corner of G82 vertical kidney grille
]

# Rail 2: Greenhouse Cant Rail / Hood Shutline / C-Pillar / Decklid Shoulder
RAIL_CANT = [
    (-2.280, 0.540, 0.995),  # Ducktail outer tip / taillight inner top
    (-2.050, 0.565, 1.005),
    (-1.740, 0.585, 1.025),  # C-pillar base / rear deck shoulder
    (-1.250, 0.565, 1.195),  # Fastback C-pillar mid
    (-0.740, 0.545, 1.332),  # Roof cant rail aft
    (-0.200, 0.555, 1.368),  # Roof cant rail peak
    ( 0.180, 0.565, 1.338),  # Top of A-pillar
    ( 0.540, 0.625, 1.132),  # Mid A-pillar
    ( 0.880, 0.685, 0.910),  # Base of A-pillar / hood-fender corner
    ( 1.250, 0.665, 0.885),  # Hood-to-fender shutline
    ( 1.680, 0.620, 0.835),  # Hood-to-fender shutline over front axle
    ( 2.080, 0.535, 0.748),  # Headlight inner-top hood corner
    ( 2.340, 0.420, 0.645),  # Headlight inner corner next to kidney
]

# Rail 3: Beltline / Waistline / Fender Crown / Rear Haunch Shoulder
RAIL_BELT = [
    (-2.280, 0.700, 0.925),  # Outer taillight upper wrap
    (-2.020, 0.765, 0.965),  # Rear quarter top over haunch
    (-1.740, 0.805, 0.990),  # Muscular rear haunch crest
    (-1.428, 0.820, 0.995),  # Over rear axle
    (-1.120, 0.785, 0.995),  # Base of Hofmeister kink
    (-0.740, 0.775, 0.980),  # Rear quarter window sill
    (-0.200, 0.785, 0.960),  # Door beltline at B-pillar
    ( 0.400, 0.795, 0.935),  # Door beltline forward
    ( 0.880, 0.795, 0.902),  # Mirror mount / front fender top
    ( 1.428, 0.815, 0.855),  # Front fender crown over front axle
    ( 1.850, 0.785, 0.785),  # Front fender forward slope
    ( 2.140, 0.720, 0.685),  # Outer corner of headlight
    ( 2.340, 0.630, 0.595),  # Front bumper below headlight
]

# Rail 4: Tornado Swage Character Crease (Sharp BMW M side crease)
RAIL_SWAGE = [
    (-2.280, 0.760, 0.720),  # Rear bumper side wrap crease
    (-2.020, 0.850, 0.760),
    (-1.780, 0.895, 0.790),  # Aft of rear wheel arch
    (-1.428, 0.925, 0.815),  # Haunch crease above rear wheel arch
    (-1.020, 0.885, 0.795),  # Forward of rear wheel arch
    (-0.350, 0.865, 0.775),  # Door handle crease
    ( 0.350, 0.868, 0.750),  # Door mid crease
    ( 0.980, 0.882, 0.725),  # M Fender gill vent
    ( 1.428, 0.918, 0.765),  # Fender flare crease above front wheel arch
    ( 1.840, 0.875, 0.695),  # Ahead of front wheel arch
    ( 2.140, 0.790, 0.585),  # Front bumper outer air-curtain top
    ( 2.340, 0.690, 0.485),
]

# Rail 5: Wheel-Arch Flare Lip / Mid-Door Belly Line (widest Y = 0.9435 m at arches)
def eval_arch_flare(x):
    """Evaluate (x, y, z) along the wheel-arch upper flare rim / mid-door flank."""
    dx_r = x - X_AXLE_R
    if abs(dx_r) <= R_ARCH_R + 1e-6:
        ang = math.acos(np.clip(dx_r / R_ARCH_R, -1.0, 1.0))
        z_foot = 0.285
        z_top = Z_AXLE_R + R_ARCH_R + 0.035
        z = z_foot + (z_top - z_foot) * math.sin(ang)
        y = 0.9435 - 0.014 * (dx_r / R_ARCH_R) ** 2
        return np.array([x, y, z])
    dx_f = x - X_AXLE_F
    if abs(dx_f) <= R_ARCH_F + 1e-6:
        ang = math.acos(np.clip(dx_f / R_ARCH_F, -1.0, 1.0))
        z_foot = 0.275
        z_top = Z_AXLE_F + R_ARCH_F + 0.032
        z = z_foot + (z_top - z_foot) * math.sin(ang)
        y = 0.9410 - 0.014 * (dx_f / R_ARCH_F) ** 2
        return np.array([x, y, z])
    knots = [
        (-2.280, 0.740, 0.305),
        (-2.040, 0.875, 0.290),
        (X_AXLE_R - R_ARCH_R, 0.9295, 0.285),
        (X_AXLE_R + R_ARCH_R, 0.9295, 0.285),
        (-0.500, 0.892, 0.280),
        ( 0.000, 0.890, 0.278),
        ( 0.500, 0.892, 0.276),
        (X_AXLE_F - R_ARCH_F, 0.9270, 0.275),
        (X_AXLE_F + R_ARCH_F, 0.9270, 0.275),
        ( 2.040, 0.865, 0.270),
        ( 2.340, 0.700, 0.265),
    ]
    return eval_rail(knots, x)

# Rail 6: Bottom Sill / Wheel-Arch Cutout Rim / Lower Bumper Valance
def eval_bottom_edge(x):
    """Evaluate (x, y, z) along the lower body edge (hugging circular wheel-arch openings down to the rocker sill!)."""
    dx_r = x - X_AXLE_R
    if abs(dx_r) <= R_ARCH_R + 1e-6:
        ang = math.acos(np.clip(dx_r / R_ARCH_R, -1.0, 1.0))
        z_sill = 0.152
        z_top = Z_AXLE_R + R_ARCH_R - 0.010
        z = z_sill + (z_top - z_sill) * math.sin(ang)
        y = 0.938 - 0.012 * (dx_r / R_ARCH_R) ** 2
        return np.array([x, y, z])
    dx_f = x - X_AXLE_F
    if abs(dx_f) <= R_ARCH_F + 1e-6:
        ang = math.acos(np.clip(dx_f / R_ARCH_F, -1.0, 1.0))
        z_sill = 0.140
        z_top = Z_AXLE_F + R_ARCH_F - 0.010
        z = z_sill + (z_top - z_sill) * math.sin(ang)
        y = 0.935 - 0.012 * (dx_f / R_ARCH_F) ** 2
        return np.array([x, y, z])
    knots = [
        (-2.280, 0.710, 0.205),
        (-2.040, 0.855, 0.175),
        (X_AXLE_R - R_ARCH_R, 0.926, 0.152),
        (X_AXLE_R + R_ARCH_R, 0.910, 0.145),
        (-0.500, 0.880, 0.142),
        ( 0.000, 0.880, 0.140),
        ( 0.500, 0.882, 0.140),
        (X_AXLE_F - R_ARCH_F, 0.910, 0.140),
        (X_AXLE_F + R_ARCH_F, 0.920, 0.140),
        ( 2.040, 0.840, 0.136),
        ( 2.340, 0.670, 0.132),
    ]
    return eval_rail(knots, x)


# ==============================================================================
# 2. BICUBIC NURBS PATCH & PLANAR EXTRUSION HELPERS (100% OUTWARD NORMALS)
# ==============================================================================
def fmt_pt(p):
    return f"({p[0]:.4f},{p[1]:.4f},{p[2]:.4f})"

def emit_oriented_patch(out, name, grid, outward_hint, degree=3):
    """Emit a SolidArc `patch countU countV ... --degree=D` command from a 2D array `grid` (shape [U, V, 3]).
    Checks (dS/dU x dS/dV) against `outward_hint` at the patch center; if negative, reverses V so the
    surface normal is 100% outward (preventing pink back-face shading in SolidArc).
    """
    G = np.array(grid, dtype=float)
    cu, cv, _ = G.shape
    iu, iv = cu // 2, cv // 2
    du = G[min(cu - 1, iu + 1), iv] - G[max(0, iu - 1), iv]
    dv = G[iu, min(cv - 1, iv + 1)] - G[iu, max(0, iv - 1)]
    n = np.cross(du, dv)
    if np.dot(n, outward_hint) < 0:
        G = G[:, ::-1, :]
    pts_str = " ".join(fmt_pt(G[u, v]) for u in range(cu) for v in range(cv))
    deg = min(degree, cu - 1, cv - 1)
    out.append(f"patch {cu} {cv} {pts_str} --degree={deg} --name={name}")
    return name

def build_rail_strip_patches(out, stem, xs, rail_fns, v_subdivs, outward_hint, mirror=True):
    """Build a right (+Y) and optionally mirrored left (-Y) bicubic NURBS patch over station array `xs`
    and a list of rail functions `rail_fns` (ordered from inner/upper to outer/lower).
    """
    names = []
    u_count = len(xs)
    rail_pts = np.array([[fn(x) for fn in rail_fns] for x in xs], dtype=float)
    n_rails = len(rail_fns)
    v_grid = []
    for u in range(u_count):
        row = []
        for r in range(n_rails - 1):
            p0 = rail_pts[u, r]
            p1 = rail_pts[u, r + 1]
            sub = v_subdivs[r]
            for s in range(sub):
                t = s / float(sub)
                row.append((1.0 - t) * p0 + t * p1)
        row.append(rail_pts[u, -1])
        v_grid.append(row)
    G_R = np.array(v_grid, dtype=float)
    nm_r = f"{stem}_R"
    emit_oriented_patch(out, nm_r, G_R, outward_hint)
    names.append(nm_r)

    if mirror:
        G_L = G_R.copy()
        G_L[:, :, 1] *= -1.0
        hint_l = np.array([outward_hint[0], -outward_hint[1], outward_hint[2]])
        nm_l = f"{stem}_L"
        emit_oriented_patch(out, nm_l, G_L, hint_l)
        names.append(nm_l)
    return names

def add_extruded_prism(out, name, loop_xyz, direction, length, tint, matcap=None):
    """Project `loop_xyz` onto the exact plane orthogonal to `direction` and orient it CCW
    with respect to `+direction` so SolidArc's `extrude` always produces a capped, outward-normal
    watertight solid (χ=2, genus 0) with zero pink faces.
    """
    P = np.array(loop_xyz, dtype=float)
    d = np.array(direction, dtype=float)
    d /= np.linalg.norm(d)
    origin = P.mean(axis=0)
    # Project every vertex onto the plane through `origin` with normal `d`
    P_proj = P - np.outer((P - origin) @ d, d)
    # Compute polygon area vector via Newell's method
    area_vec = np.zeros(3)
    for i in range(len(P_proj)):
        p1 = P_proj[i] - origin
        p2 = P_proj[(i + 1) % len(P_proj)] - origin
        area_vec += np.cross(p1, p2)
    if np.dot(area_vec, d) < 0:
        P_proj = P_proj[::-1]
    pts = " ".join(fmt_pt(p) for p in P_proj)
    cname = f"{name}_Prof"
    out.append(f"polyline {pts} --closed --name={cname}")
    d_str = f"({d[0]:.4f},{d[1]:.4f},{d[2]:.4f})"
    out.append(f"extrude {cname} {length:.4f} --direction={d_str} --name={name}")
    out.append(f"delete {cname}")
    out.append(f"tint {name} {tint}")
    if matcap:
        out.append(f"matcap {name} {matcap}")


# ==============================================================================
# 3. GENERATE 2D/3D ORTHOGRAPHIC CAD SKETCH FILE (BMW_M4_G82_2D_Sketch.arc)
# ==============================================================================
def generate_2d_sketch_arc():
    out = [
        "# SolidArc native document v1",
        "# BMW M4 Competition (G82) — 2D/3D Orthographic CAD Sketch Network extracted from 4-View Blueprint",
        "# Dimensions: Length 4.794 m, Wheelbase 2.857 m, Width 1.887 m, Height 1.393 m",
        "# Right-handed, Z up, metres. +X = Front, Y = 0 Centerline, Z = 0 Ground.",
        "reset",
        "show shading plastic",
    ]
    BLUE = "0.22 0.55 0.98"
    RED = "0.95 0.28 0.24"
    CYAN = "0.20 0.85 0.92"
    GOLD = "0.95 0.78 0.20"
    GREEN = "0.28 0.88 0.42"
    GREY = "0.62 0.66 0.72"

    xs_full = np.linspace(-2.28, 2.34, 28)

    # 1. Side Elevation Centerline Roof/Hood/Trunk Profile (Y = 0)
    pts_center = [eval_rail(RAIL_CENTER_TOP, x) for x in xs_full]
    out.append(f"spline {' '.join(fmt_pt(p) for p in pts_center)} --name=Sketch_Side_Centerline")
    out.append(f"tint Sketch_Side_Centerline {BLUE}")

    sill_side = [(-2.35, 0.0, 0.21), (-1.82, 0.0, 0.17), (-1.04, 0.0, 0.14), (1.05, 0.0, 0.138), (1.81, 0.0, 0.138), (2.44, 0.0, 0.125)]
    out.append(f"polyline {' '.join(fmt_pt(p) for p in sill_side)} --name=Sketch_Side_Underbody")
    out.append(f"tint Sketch_Side_Underbody {BLUE}")

    # 2. Longitudinal Feature Rails (+Y and -Y)
    for rname, rknots, col in [
        ("PowerDome_Line", RAIL_DOME, RED),
        ("CantRail_HoodEdge", RAIL_CANT, BLUE),
        ("Beltline_Waist", RAIL_BELT, CYAN),
        ("Tornado_Swage_Crease", RAIL_SWAGE, RED),
    ]:
        for side, sgn in [("R", 1.0), ("L", -1.0)]:
            pts = [eval_rail(rknots, x) * np.array([1.0, sgn, 1.0]) for x in xs_full]
            nm = f"Sketch_{rname}_{side}"
            out.append(f"spline {' '.join(fmt_pt(p) for p in pts)} --name={nm}")
            out.append(f"tint {nm} {col}")

    # 3. Wheel Arch Openings & Wheel Circles (Side + 3D)
    for ax_name, xc, zc, r_arch, r_tire in [
        ("Front", X_AXLE_F, Z_AXLE_F, R_ARCH_F, R_TIRE_F),
        ("Rear", X_AXLE_R, Z_AXLE_R, R_ARCH_R, R_TIRE_R),
    ]:
        xs_a = np.linspace(xc - r_arch, xc + r_arch, 17)
        for side, sgn in [("R", 1.0), ("L", -1.0)]:
            pts_a = [eval_bottom_edge(x) * np.array([1.0, sgn, 1.0]) for x in xs_a]
            nm = f"Sketch_{ax_name}Arch_{side}"
            out.append(f"spline {' '.join(fmt_pt(p) for p in pts_a)} --name={nm}")
            out.append(f"tint {nm} {GREEN}")
            y_w = sgn * 0.88
            c_tire = [np.array([xc + r_tire * math.cos(a), y_w, zc + r_tire * math.sin(a)]) for a in np.linspace(0, 2 * math.pi, 25)[:-1]]
            out.append(f"polyline {' '.join(fmt_pt(p) for p in c_tire)} --closed --name=Sketch_Tire_{ax_name}_{side}")
            out.append(f"tint Sketch_Tire_{ax_name}_{side} {GREEN}")
            r_rim = 0.242
            c_rim = [np.array([xc + r_rim * math.cos(a), y_w, zc + r_rim * math.sin(a)]) for a in np.linspace(0, 2 * math.pi, 25)[:-1]]
            out.append(f"polyline {' '.join(fmt_pt(p) for p in c_rim)} --closed --name=Sketch_Rim_{ax_name}_{side}")
            out.append(f"tint Sketch_Rim_{ax_name}_{side} {GOLD}")

    # 4. Side Window Daylight Opening (DLO) with Hofmeister Kink
    dlo_r = [
        ( 0.78, 0.785, 0.915),
        ( 0.18, 0.568, 1.325),
        (-0.35, 0.558, 1.352),
        (-0.85, 0.562, 1.305),
        (-1.38, 0.655, 1.135),
        (-1.46, 0.720, 1.055),
        (-1.26, 0.780, 1.000),
        (-0.20, 0.785, 0.962),
    ]
    for side, sgn in [("R", 1.0), ("L", -1.0)]:
        pts = [np.array([x, sgn * y, z]) for x, y, z in dlo_r]
        out.append(f"polyline {' '.join(fmt_pt(p) for p in pts)} --closed --name=Sketch_WindowDLO_{side}")
        out.append(f"tint Sketch_WindowDLO_{side} {CYAN}")

    # 5. Front View G82 Vertical Kidney Grilles & Headlights Sketch Loops
    kidney_r = [
        (2.425, 0.030, 0.655),
        (2.395, 0.195, 0.665),
        (2.415, 0.220, 0.520),
        (2.430, 0.185, 0.315),
        (2.440, 0.035, 0.305),
        (2.435, 0.022, 0.480),
    ]
    headlight_r = [
        (2.310, 0.390, 0.635),
        (2.190, 0.715, 0.675),
        (2.140, 0.745, 0.620),
        (2.265, 0.440, 0.575),
    ]
    for side, sgn in [("R", 1.0), ("L", -1.0)]:
        pk = [np.array([x, sgn * y, z]) for x, y, z in kidney_r]
        out.append(f"polyline {' '.join(fmt_pt(p) for p in pk)} --closed --name=Sketch_KidneyGrille_{side}")
        out.append(f"tint Sketch_KidneyGrille_{side} {GOLD}")
        ph = [np.array([x, sgn * y, z]) for x, y, z in headlight_r]
        out.append(f"polyline {' '.join(fmt_pt(p) for p in ph)} --closed --name=Sketch_Headlight_{side}")
        out.append(f"tint Sketch_Headlight_{side} {GOLD}")

    # 6. Cross-Section Station Ribs (showing the 2D Front/Rear section cuts at 9 X-stations)
    for idx, x_st in enumerate([-2.15, -1.74, -1.4285, -0.74, -0.15, 0.45, 0.88, 1.4285, 2.05]):
        sec_r = [
            eval_rail(RAIL_CENTER_TOP, x_st),
            eval_rail(RAIL_DOME, x_st),
            eval_rail(RAIL_CANT, x_st),
            eval_rail(RAIL_BELT, x_st),
            eval_rail(RAIL_SWAGE, x_st),
            eval_arch_flare(x_st),
            eval_bottom_edge(x_st),
        ]
        sec_full = [p * np.array([1.0, -1.0, 1.0]) for p in sec_r[::-1][:-1]] + sec_r
        out.append(f"spline {' '.join(fmt_pt(p) for p in sec_full)} --name=Sketch_Section_{idx:02d}")
        out.append(f"tint Sketch_Section_{idx:02d} {GREY}")

    # 7. Top-View Planform Outer Footprint Projected onto Ground Plane (Z = 0.02 m)
    plan_r = [np.array([x, eval_arch_flare(x)[1], 0.02]) for x in xs_full]
    plan_l = [np.array([x, -eval_arch_flare(x)[1], 0.02]) for x in xs_full[::-1]]
    plan_loop = plan_r + plan_l
    out.append(f"polyline {' '.join(fmt_pt(p) for p in plan_loop)} --closed --name=Sketch_TopPlan_Footprint")
    out.append(f"tint Sketch_TopPlan_Footprint {BLUE}")

    out.append("view iso")
    out.append("view fit")
    sketch_path = OUTDIR / "BMW_M4_G82_2D_Sketch.arc"
    sketch_path.write_text("\n".join(out) + "\n")
    print(f"Wrote {sketch_path} ({len(out)} lines)")


# ==============================================================================
# 4. GENERATE COMPLETE 3D SOLIDARC CAD MODEL (BMW_M4_G82.arc)
# ==============================================================================
def build_wheel_assembly(out, tag, xc, side_sgn, r_tire, zc):
    """Build a complete B-rep wheel, tire, rim barrel, 10-spoke M star wheel, brake disc, and M caliper."""
    TIN_TIRE = "0.17 0.18 0.20"
    TIN_RIM = "0.82 0.85 0.90"
    TIN_SPOKE = "0.78 0.81 0.86"
    TIN_DISC = "0.52 0.55 0.60"
    TIN_CAL = "0.92 0.70 0.14"
    TIN_HUB = "0.22 0.45 0.82"

    y_out = side_sgn * 0.905
    y_in = side_sgn * 0.645
    y0, y1 = min(y_in, y_out), max(y_in, y_out)
    ym = 0.5 * (y0 + y1)
    hw = 0.5 * (y1 - y0)
    r_rim = 0.242
    r_inner = 0.212

    # 1. Revolved Tire Carcass with rounded sidewalls
    tire_prof = [
        (xc, ym - hw * 0.88, zc + r_rim),
        (xc, ym - hw * 1.00, zc + r_rim + 0.45 * (r_tire - r_rim)),
        (xc, ym - hw * 0.96, zc + r_tire - 0.012),
        (xc, ym - hw * 0.75, zc + r_tire),
        (xc, ym + hw * 0.75, zc + r_tire),
        (xc, ym + hw * 0.96, zc + r_tire - 0.012),
        (xc, ym + hw * 1.00, zc + r_rim + 0.45 * (r_tire - r_rim)),
        (xc, ym + hw * 0.88, zc + r_rim),
    ]
    out.append(f"polyline {' '.join(fmt_pt(p) for p in tire_prof)} --closed --name=TireProf_{tag}")
    out.append(f"revolve TireProf_{tag} 360 --origin=({xc:.4f},0,{zc:.4f}) --axis=(0,1,0) --name=Tire_{tag}")
    out.append(f"delete TireProf_{tag}")
    out.append(f"tint Tire_{tag} {TIN_TIRE}")
    out.append(f"matcap Tire_{tag} rubber")

    # 2. Revolved Alloy Rim Barrel & Outer Lip
    rim_prof = [
        (xc, ym - hw * 0.85, zc + r_inner),
        (xc, ym - hw * 0.85, zc + r_rim + 0.004),
        (xc, ym + hw * 0.85, zc + r_rim + 0.004),
        (xc, ym + hw * 0.85, zc + r_inner - 0.008),
        (xc, ym + hw * 0.68, zc + r_inner - 0.008),
        (xc, ym + hw * 0.68, zc + r_inner + 0.008),
        (xc, ym - hw * 0.70, zc + r_inner + 0.008),
        (xc, ym - hw * 0.70, zc + r_inner),
    ]
    out.append(f"polyline {' '.join(fmt_pt(p) for p in rim_prof)} --closed --name=RimProf_{tag}")
    out.append(f"revolve RimProf_{tag} 360 --origin=({xc:.4f},0,{zc:.4f}) --axis=(0,1,0) --name=RimBarrel_{tag}")
    out.append(f"delete RimProf_{tag}")
    out.append(f"tint RimBarrel_{tag} {TIN_RIM}")
    out.append(f"matcap RimBarrel_{tag} chrome")

    # 3. 10-Spoke M Forged Wheel Face (extruded spoke + radial array of 10)
    y_face = y_out - side_sgn * 0.018
    spoke_loop = [
        (xc - 0.012, y_face, zc + 0.048),
        (xc + 0.012, y_face, zc + 0.048),
        (xc + 0.018, y_face, zc + r_inner + 0.004),
        (xc - 0.018, y_face, zc + r_inner + 0.004),
    ]
    add_extruded_prism(out, f"Spoke_{tag}", spoke_loop, (0, -side_sgn, 0), 0.028, TIN_SPOKE, "chrome")
    out.append(f"radial Spoke_{tag} --count=10 --axis=({xc:.4f},0,{zc:.4f}),(0,1,0) --name=Spoke_{tag}")

    # 4. Wheel Center Hub & BMW Roundel Cap
    out.append(f"cylinder ({xc:.4f},{y_out - side_sgn * 0.062:.4f},{zc:.4f}) 0.058 0.052 --axis=(0,{side_sgn:.0f},0) --name=WheelHub_{tag}")
    out.append(f"tint WheelHub_{tag} {TIN_RIM}")
    out.append(f"matcap WheelHub_{tag} chrome")
    out.append(f"cylinder ({xc:.4f},{y_out - side_sgn * 0.010:.4f},{zc:.4f}) 0.026 0.008 --axis=(0,{side_sgn:.0f},0) --name=WheelCap_{tag}")
    out.append(f"tint WheelCap_{tag} {TIN_HUB}")

    # 5. Brake Rotor Disc + M Carbon-Ceramic Gold Brake Caliper
    y_disc = y_out - side_sgn * 0.088
    out.append(f"cylinder ({xc:.4f},{y_disc:.4f},{zc:.4f}) 0.176 0.026 --axis=(0,{-side_sgn:.0f},0) --name=BrakeDisc_{tag}")
    out.append(f"tint BrakeDisc_{tag} {TIN_DISC}")
    out.append(f"matcap BrakeDisc_{tag} steel")

    cal_dir = -1.0 if xc > 0 else 1.0
    cal_loop = [
        (xc + cal_dir * 0.085, y_disc + side_sgn * 0.016, zc - 0.065),
        (xc + cal_dir * 0.172, y_disc + side_sgn * 0.016, zc - 0.055),
        (xc + cal_dir * 0.175, y_disc + side_sgn * 0.016, zc + 0.075),
        (xc + cal_dir * 0.085, y_disc + side_sgn * 0.016, zc + 0.060),
    ]
    add_extruded_prism(out, f"BrakeCaliper_{tag}", cal_loop, (0, -side_sgn, 0), 0.052, TIN_CAL, "gold")


def generate_3d_cad_arc():
    out = [
        "# SolidArc native document v1",
        "# BMW M4 Competition Coupe (G82) — Complete 3D CAD Model (.arc) built from 2D Orthographic Blueprint",
        "# Dimensions: L = 4.794 m, WB = 2.857 m, W = 1.887 m, H = 1.393 m",
        "# Right-handed, Z up, metres. +X = Front, Y = 0 Centerline, Z = 0 Ground.",
        "reset",
        "require open-sew",
        "show shading plastic",
    ]

    # Automotive Palette
    BODY_PAINT = "0.16 0.56 0.42"     # BMW M Isle of Man Green Metallic
    CARBON_DARK = "0.18 0.20 0.23"    # M CFRP Carbon Fibre Roof / Splitter / Diffuser
    TRIM_BLACK = "0.12 0.13 0.15"     # M Shadowline Gloss Black
    GLASS_TINT = "0.22 0.30 0.38"     # Tinted Automotive Safety Glass
    HEADLIGHT_LED = "0.90 0.96 1.00"  # Laserlight Ice-White LED DRL
    TAILLIGHT_RED = "0.88 0.14 0.16"  # BMW L-Shape Ruby Red LED Taillight
    EXHAUST_TITAN = "0.76 0.79 0.84"  # Quad Exhaust Titanium/Chrome Tips

    body_patches = []
    roof_patches = []
    glass_patches = []

    # ── A. HOOD / BONNET (X = 0.88 .. 2.34) with M4 Power-Dome & Center Channel ──
    xs_hood = np.linspace(0.88, 2.34, 9)
    body_patches += build_rail_strip_patches(
        out, "Hood_Center", xs_hood,
        [lambda x: eval_rail(RAIL_CENTER_TOP, x), lambda x: eval_rail(RAIL_DOME, x)],
        [3], outward_hint=np.array([0.15, 0.1, 1.0]), mirror=True
    )
    body_patches += build_rail_strip_patches(
        out, "Hood_Dome", xs_hood,
        [lambda x: eval_rail(RAIL_DOME, x), lambda x: eval_rail(RAIL_CANT, x)],
        [3], outward_hint=np.array([0.15, 0.25, 1.0]), mirror=True
    )

    # ── B. WINDSHIELD (X = 0.18 .. 0.88) ──
    xs_wind = np.linspace(0.18, 0.88, 6)
    glass_patches += build_rail_strip_patches(
        out, "Glass_Windshield", xs_wind,
        [
            lambda x: eval_rail(RAIL_CENTER_TOP, x) + np.array([0.0, 0.0, -0.004]),
            lambda x: eval_rail(RAIL_DOME, x) + np.array([0.0, 0.0, -0.004]),
            lambda x: (0.93 * eval_rail(RAIL_CANT, x) + 0.07 * eval_rail(RAIL_CENTER_TOP, x)) + np.array([0.0, 0.0, -0.004]),
        ],
        [2, 2], outward_hint=np.array([0.45, 0.1, 0.9]), mirror=True
    )

    # ── C. CFRP CARBON ROOF (X = -0.74 .. 0.18) with Longitudinal Aero Channels ──
    xs_roof = np.linspace(-0.74, 0.18, 6)
    roof_patches += build_rail_strip_patches(
        out, "Roof_CFRP", xs_roof,
        [
            lambda x: eval_rail(RAIL_CENTER_TOP, x),
            lambda x: eval_rail(RAIL_DOME, x),
            lambda x: 0.93 * eval_rail(RAIL_CANT, x) + 0.07 * eval_rail(RAIL_CENTER_TOP, x),
        ],
        [2, 2], outward_hint=np.array([0.0, 0.1, 1.0]), mirror=True
    )

    # ── D. REAR BACKLIGHT WINDOW (X = -1.74 .. -0.74) ──
    xs_rwin = np.linspace(-1.74, -0.74, 6)
    glass_patches += build_rail_strip_patches(
        out, "Glass_RearWindow", xs_rwin,
        [
            lambda x: eval_rail(RAIL_CENTER_TOP, x) + np.array([0.0, 0.0, -0.004]),
            lambda x: eval_rail(RAIL_DOME, x) + np.array([0.0, 0.0, -0.004]),
            lambda x: (0.93 * eval_rail(RAIL_CANT, x) + 0.07 * eval_rail(RAIL_CENTER_TOP, x)) + np.array([0.0, 0.0, -0.004]),
        ],
        [2, 2], outward_hint=np.array([-0.4, 0.1, 0.9]), mirror=True
    )

    # ── E. TRUNK DECKLID & DUCKTAIL SPOILER (X = -2.28 .. -1.74) ──
    xs_trunk = np.linspace(-2.28, -1.74, 6)
    body_patches += build_rail_strip_patches(
        out, "Trunk_Deck", xs_trunk,
        [
            lambda x: eval_rail(RAIL_CENTER_TOP, x),
            lambda x: eval_rail(RAIL_DOME, x),
            lambda x: eval_rail(RAIL_CANT, x),
        ],
        [2, 2], outward_hint=np.array([-0.15, 0.15, 1.0]), mirror=True
    )

    # Sculpted CFRP Ducktail Gurney Lip Spoiler (Solid Extruded Aerofoil across Y in [-0.52, +0.52])
    spoil_prof = [
        (-2.165, -0.50, 1.022),
        (-2.295, -0.50, 1.062),
        (-2.285, -0.50, 1.032),
    ]
    add_extruded_prism(out, "Ducktail_Spoiler", spoil_prof, (0, 1, 0), 1.00, CARBON_DARK, "carbon")

    # ── F. A-PILLAR / ROOF CANT RAIL / C-PILLAR PAINTED FRAME (X = -1.74 .. 0.88) ──
    xs_pillar = np.linspace(-1.74, 0.88, 12)
    body_patches += build_rail_strip_patches(
        out, "Roof_Arch_Rail", xs_pillar,
        [
            lambda x: 0.93 * eval_rail(RAIL_CANT, x) + 0.07 * eval_rail(RAIL_CENTER_TOP, x),
            lambda x: eval_rail(RAIL_CANT, x),
            lambda x: 0.82 * eval_rail(RAIL_CANT, x) + 0.18 * eval_rail(RAIL_BELT, x),
        ],
        [2, 2], outward_hint=np.array([0.0, 0.65, 0.75]), mirror=True
    )

    # ── G. SIDE WINDOWS (Door Glass + Quarter Glass with Hofmeister Kink, X = -1.32 .. 0.88) ──
    xs_sglass = np.linspace(-1.32, 0.88, 10)
    glass_patches += build_rail_strip_patches(
        out, "Glass_SideWindow", xs_sglass,
        [
            lambda x: (0.82 * eval_rail(RAIL_CANT, x) + 0.18 * eval_rail(RAIL_BELT, x)) + np.array([0.0, -0.006, 0.0]),
            lambda x: (0.45 * eval_rail(RAIL_CANT, x) + 0.55 * eval_rail(RAIL_BELT, x)) + np.array([0.0, -0.008, 0.0]),
            lambda x: eval_rail(RAIL_BELT, x) + np.array([0.0, -0.004, 0.0]),
        ],
        [2, 2], outward_hint=np.array([0.0, 0.90, 0.42]), mirror=True
    )

    # Painted C-Pillar Sail Panel behind Hofmeister Kink (X = -1.74 .. -1.32)
    xs_sail = np.linspace(-1.74, -1.32, 5)
    body_patches += build_rail_strip_patches(
        out, "CPillar_Sail", xs_sail,
        [
            lambda x: 0.82 * eval_rail(RAIL_CANT, x) + 0.18 * eval_rail(RAIL_BELT, x),
            lambda x: 0.45 * eval_rail(RAIL_CANT, x) + 0.55 * eval_rail(RAIL_BELT, x),
            lambda x: eval_rail(RAIL_BELT, x),
        ],
        [2, 2], outward_hint=np.array([-0.15, 0.85, 0.50]), mirror=True
    )
    # Front Fender Cowl Crown ahead of A-pillar base (X = 0.88 .. X_AXLE_F - R_ARCH_F)
    xs_fcowl = np.linspace(0.88, X_AXLE_F - R_ARCH_F, 4)
    body_patches += build_rail_strip_patches(
        out, "FrontFender_CowlCrown", xs_fcowl,
        [lambda x: eval_rail(RAIL_CANT, x), lambda x: eval_rail(RAIL_BELT, x)],
        [3], outward_hint=np.array([0.1, 0.60, 0.80]), mirror=True
    )


    # ── H. BODY FLANKS SEGMENTED EXACTLY AT WHEEL ARCH BOUNDARIES ──
    flank_segments = [
        ("RearBumper_Side", -2.28, X_AXLE_R - R_ARCH_R, 6),
        ("RearQuarter_Arch", X_AXLE_R - R_ARCH_R, X_AXLE_R + R_ARCH_R, 11),
        ("Door_Flank", X_AXLE_R + R_ARCH_R, X_AXLE_F - R_ARCH_F, 10),
        ("FrontFender_Arch", X_AXLE_F - R_ARCH_F, X_AXLE_F + R_ARCH_F, 11),
        ("FrontBumper_Side", X_AXLE_F + R_ARCH_F, 2.34, 6),
    ]
    for seg_name, xa, xb, n_st in flank_segments:
        if "Arch" in seg_name:
            xc = 0.5 * (xa + xb)
            rad = 0.5 * (xb - xa)
            xs_seg = np.array([xc - rad * math.cos(th) for th in np.linspace(0.0, math.pi, n_st)])
        else:
            xs_seg = np.linspace(xa, xb, n_st)
        # Crown Strip (Rail 2 -> Rail 3 for Fender & RearQuarter)
        if seg_name in ("RearBumper_Side", "RearQuarter_Arch", "FrontFender_Arch", "FrontBumper_Side"):
            body_patches += build_rail_strip_patches(
                out, f"{seg_name}_Crown", xs_seg,
                [lambda x: eval_rail(RAIL_CANT, x), lambda x: eval_rail(RAIL_BELT, x)],
                [3], outward_hint=np.array([0.0, 0.65, 0.75]), mirror=True
            )
        # Beltline to Tornado Swage Crease (Rail 3 -> Rail 4)
        body_patches += build_rail_strip_patches(
            out, f"{seg_name}_Upper", xs_seg,
            [lambda x: eval_rail(RAIL_BELT, x), lambda x: eval_rail(RAIL_SWAGE, x)],
            [3], outward_hint=np.array([0.0, 0.92, 0.38]), mirror=True
        )
        # Tornado Swage Crease to Wheel-Arch Flare / Lower Edge (Rail 4 -> Rail 5 -> Rail 6)
        body_patches += build_rail_strip_patches(
            out, f"{seg_name}_Lower", xs_seg,
            [lambda x: eval_rail(RAIL_SWAGE, x), eval_arch_flare, eval_bottom_edge],
            [2, 2], outward_hint=np.array([0.0, 0.98, 0.15]), mirror=True
        )

    # ── I. FRONT BUMPER FASCIA APRON & REAR BUMPER FASCIA ──
    # Smooth Continuous Front Bumper Fascia (Y in [-0.68, +0.68], top edge matches Hood/Fender at X=2.34)
    ys_fb = np.linspace(-0.68, 0.68, 9)
    fb_grid = []
    for y in ys_fb:
        ay = abs(y)
        z_top = 0.672 - 0.072 * (ay / 0.68) ** 1.4
        zs_fb = np.linspace(0.130, z_top, 4)
        row = []
        for z in zs_fb:
            t_z = (z - 0.130) / max(1e-6, z_top - 0.130)
            # At z=z_top, x=2.340 to meet the hood/fender leading edge seamlessly; bows forward in the middle
            x_val = 2.340 + (1.0 - 0.65 * t_z ** 2) * 0.078 * (1.0 - (ay / 0.68) ** 1.5)
            row.append([x_val, y, z])
        fb_grid.append(row)
    body_patches.append(emit_oriented_patch(out, "Front_Bumper_Fascia", fb_grid, np.array([1.0, 0.0, 0.1])))

    # Smooth Continuous Rear Bumper & Tail Fascia (Y in [-0.72, +0.72], top edge matches Trunk/Quarter at X=-2.28)
    ys_rear = np.linspace(-0.72, 0.72, 9)
    rear_grid = []
    for y in ys_rear:
        ay = abs(y)
        z_top = 1.035 - 0.125 * (ay / 0.72) ** 1.5
        zs_rear = np.linspace(0.205, z_top, 4)
        row = []
        for z in zs_rear:
            t_z = (z - 0.205) / max(1e-6, z_top - 0.205)
            x_val = -2.280 - (1.0 - 0.65 * t_z ** 2) * 0.065 * (1.0 - (ay / 0.72) ** 1.6)
            row.append([x_val, y, z])
        rear_grid.append(row)
    body_patches.append(emit_oriented_patch(out, "Rear_Bumper_Center", rear_grid, np.array([-1.0, 0.0, 0.1])))

    # Sew painted body panels into a single clean B-rep sheet assembly
    out.append(f"sew {' '.join(body_patches)} --open --name=Body_Paint_Shell")
    out.append(f"tint Body_Paint_Shell {BODY_PAINT}")
    out.append("matcap Body_Paint_Shell pearl")

    out.append(f"sew {' '.join(roof_patches)} --open --name=Roof_And_Spoiler_CFRP")
    out.append(f"tint Roof_And_Spoiler_CFRP {CARBON_DARK}")
    out.append("matcap Roof_And_Spoiler_CFRP carbon")

    out.append(f"sew {' '.join(glass_patches)} --open --name=Glass_Greenhouse")
    out.append(f"tint Glass_Greenhouse {GLASS_TINT}")
    out.append("matcap Glass_Greenhouse glass")

    # ── J. SOLID B-REP HARDWARE, GRILLES, LIGHTS, AERO & WHEELS ──

    # 1. Wheelhouse Inner Drum Liners (inboard of wheels at Y = 0.48..0.72 so wheel spokes & brakes are 100% visible!)
    for ax_tag, xc, zc, r_arch in [("F", X_AXLE_F, Z_AXLE_F, R_ARCH_F), ("R", X_AXLE_R, Z_AXLE_R, R_ARCH_R)]:
        for s_tag, sgn in [("R", 1.0), ("L", -1.0)]:
            out.append(
                f"cylinder ({xc:.4f},{sgn * 0.48:.4f},{zc + 0.02:.4f}) {r_arch - 0.015:.4f} 0.24 "
                f"--axis=(0,{sgn:.0f},0) --name=Wheelhouse_{ax_tag}_{s_tag}"
            )
            out.append(f"tint Wheelhouse_{ax_tag}_{s_tag} 0.12 0.13 0.15")

    # 2. Central Internal Bulkhead & Flat Underbody Floor Pan (blocks any interior back-face visibility)
    under_loop = [
        (-2.28, -0.82, 0.150),
        ( 2.34, -0.82, 0.135),
        ( 2.34,  0.82, 0.135),
        (-2.28,  0.82, 0.150),
    ]
    add_extruded_prism(out, "Underbody_Pan", under_loop, (0, 0, 1), 0.035, TRIM_BLACK, "carbon")



    for s_tag, sgn in [("R", 1.0), ("L", -1.0)]:
        skirt_loop = [
            (X_AXLE_R + R_ARCH_R + 0.02, sgn * 0.865, 0.128),
            (X_AXLE_F - R_ARCH_F - 0.02, sgn * 0.865, 0.128),
            (X_AXLE_F - R_ARCH_F - 0.02, sgn * 0.915, 0.128),
            (X_AXLE_R + R_ARCH_R + 0.02, sgn * 0.915, 0.128),
        ]
        add_extruded_prism(out, f"Rocker_Carbon_Blade_{s_tag}", skirt_loop, (0, 0, 1), 0.024, CARBON_DARK, "carbon")

    # 3. Front Carbon Lip Splitter & Aggressive Front Bumper Air Intakes
    splitter_loop = [
        (2.15, -0.84, 0.118),
        (2.38, -0.68, 0.118),
        (2.444, -0.28, 0.118),
        (2.444,  0.28, 0.118),
        (2.38,  0.68, 0.118),
        (2.15,  0.84, 0.118),
        (2.15,  0.72, 0.118),
        (2.34,  0.24, 0.118),
        (2.34, -0.24, 0.118),
        (2.15, -0.72, 0.118),
    ]
    add_extruded_prism(out, "Front_Carbon_Splitter", splitter_loop, (0, 0, 1), 0.024, CARBON_DARK, "carbon")

    # Lower Central Honeycomb Intake
    center_intake = [
        (2.39, -0.28, 0.145),
        (2.39,  0.28, 0.145),
        (2.39,  0.24, 0.275),
        (2.39, -0.24, 0.275),
    ]
    add_extruded_prism(out, "Front_Center_Intake", center_intake, (1, 0, 0), 0.045, TRIM_BLACK, "carbon")

    # Iconic G82 Vertical Frameless Twin Kidney Grilles + 7 Horizontal M Double-Slats
    for s_tag, sgn in [("R", 1.0), ("L", -1.0)]:
        kidney_back = [
            (2.38, sgn * 0.032, 0.650),
            (2.38, sgn * 0.195, 0.658),
            (2.38, sgn * 0.218, 0.515),
            (2.38, sgn * 0.185, 0.305),
            (2.38, sgn * 0.035, 0.295),
        ]
        add_extruded_prism(out, f"Kidney_Backing_{s_tag}", kidney_back, (1, 0, 0), 0.055, TRIM_BLACK, "carbon")
        for si, z_slat in enumerate(np.linspace(0.335, 0.620, 7)):
            w_outer = 0.195 if z_slat > 0.42 else 0.175
            slat_loop = [
                (2.390, sgn * 0.038, z_slat),
                (2.390, sgn * w_outer, z_slat),
                (2.442, sgn * w_outer, z_slat),
                (2.442, sgn * 0.038, z_slat),
            ]
            add_extruded_prism(out, f"Kidney_Slat_{s_tag}_{si}", slat_loop, (0, 0, 1), 0.012, TRIM_BLACK, "steel")

        # Outer Triangular M Front Bumper Side Air Curtains
        intake_loop = [
            (2.28, sgn * 0.40, 0.21),
            (2.28, sgn * 0.65, 0.23),
            (2.28, sgn * 0.67, 0.43),
            (2.28, sgn * 0.44, 0.41),
        ]
        add_extruded_prism(out, f"Front_Side_Intake_{s_tag}", intake_loop, (0.94, 0.34 * sgn, 0), 0.10, TRIM_BLACK, "carbon")

    # 4. BMW Laserlight LED Headlights (Dark Pod + Twin Hexagonal DRL Light Signatures)
    for s_tag, sgn in [("R", 1.0), ("L", -1.0)]:
        hl_pod = [
            (2.365, sgn * 0.255, 0.615),
            (2.330, sgn * 0.625, 0.640),
            (2.335, sgn * 0.645, 0.555),
            (2.375, sgn * 0.275, 0.545),
        ]
        add_extruded_prism(out, f"Headlight_Pod_{s_tag}", hl_pod, (0.94, 0.28 * sgn, 0.18), 0.055, TRIM_BLACK, "glass")
        for bi, (ya, yb, za, zb) in enumerate([
            (0.285, 0.425, 0.565, 0.578),
            (0.455, 0.605, 0.582, 0.598),
        ]):
            drl_loop = [
                (2.375, sgn * ya, za),
                (2.355, sgn * yb, zb),
                (2.350, sgn * yb, zb + 0.026),
                (2.370, sgn * ya, za + 0.026),
            ]
            add_extruded_prism(out, f"Headlight_DRL_{s_tag}_{bi}", drl_loop, (0.94, 0.28 * sgn, 0.18), 0.048, HEADLIGHT_LED, "headlight")

    # 5. Sculpted 3D L-Shaped BMW LED Taillights (flush below rear decklid edge)
    for s_tag, sgn in [("R", 1.0), ("L", -1.0)]:
        tl_loop = [
            (-2.342, sgn * 0.260, 0.825),
            (-2.342, sgn * 0.655, 0.840),
            (-2.342, sgn * 0.685, 0.910),
            (-2.342, sgn * 0.545, 0.918),
            (-2.342, sgn * 0.505, 0.868),
            (-2.342, sgn * 0.260, 0.862),
        ]
        add_extruded_prism(out, f"Taillight_LED_{s_tag}", tl_loop, (1, 0, 0), 0.085, TAILLIGHT_RED, "taillight")

    # 6. Rear M Carbon Diffuser + 4 Aero Fins + Quad Titanium Exhaust Pipes
    diff_loop = [
        (-2.355, -0.62, 0.185),
        (-2.355,  0.62, 0.185),
        (-2.355,  0.56, 0.335),
        (-2.355, -0.56, 0.335),
    ]
    add_extruded_prism(out, "Rear_Diffuser_M", diff_loop, (1, 0, 0), 0.14, CARBON_DARK, "carbon")
    for fi, y_fin in enumerate([-0.24, -0.08, 0.08, 0.24]):
        fin_loop = [
            (-2.365, y_fin - 0.008, 0.175),
            (-2.220, y_fin - 0.008, 0.175),
            (-2.220, y_fin - 0.008, 0.315),
            (-2.365, y_fin - 0.008, 0.305),
        ]
        add_extruded_prism(out, f"Diffuser_Fin_{fi}", fin_loop, (0, 1, 0), 0.016, CARBON_DARK, "carbon")

    for ei, y_ex in enumerate([-0.465, -0.355, 0.355, 0.465]):
        out.append(f"cylinder (-2.385,{y_ex:.4f},0.245) 0.046 0.18 --axis=(1,0,0) --name=Exhaust_Pipe_{ei}")
        out.append(f"tint Exhaust_Pipe_{ei} {EXHAUST_TITAN}")
        out.append(f"matcap Exhaust_Pipe_{ei} chrome")
        out.append(f"cylinder (-2.390,{y_ex:.4f},0.245) 0.036 0.04 --axis=(1,0,0) --name=Exhaust_Bore_{ei}")
        out.append(f"tint Exhaust_Bore_{ei} 0.08 0.08 0.09")

    # 7. M Twin-Stalk Aerodynamic Wing Mirrors, Flush Door Handles, Fender Gills, B-Pillars & Shark-Fin Antenna
    for s_tag, sgn in [("R", 1.0), ("L", -1.0)]:
        mir_loop = [
            (0.68, sgn * 0.80, 0.940),
            (0.79, sgn * 0.98, 0.940),
            (0.75, sgn * 1.034, 0.940),
            (0.64, sgn * 1.015, 0.940),
            (0.63, sgn * 0.81, 0.940),
        ]
        add_extruded_prism(out, f"Mirror_M_{s_tag}", mir_loop, (0, 0, 1), 0.068, BODY_PAINT, "pearl")
        dh_loop = [
            (-0.38, sgn * 0.855, 0.770),
            (-0.18, sgn * 0.855, 0.775),
            (-0.18, sgn * 0.855, 0.802),
            (-0.38, sgn * 0.855, 0.797),
        ]
        add_extruded_prism(out, f"Door_Handle_{s_tag}", dh_loop, (0, sgn, 0), 0.025, BODY_PAINT, "pearl")
        gill_loop = [
            (0.92, sgn * 0.870, 0.685),
            (1.03, sgn * 0.870, 0.705),
            (1.01, sgn * 0.870, 0.755),
            (0.90, sgn * 0.870, 0.735),
        ]
        add_extruded_prism(out, f"Fender_MGill_{s_tag}", gill_loop, (0, sgn, 0), 0.022, TRIM_BLACK, "carbon")
        bp_loop = [
            (-0.24, sgn * 0.778, 0.955),
            (-0.14, sgn * 0.778, 0.955),
            (-0.18, sgn * 0.558, 1.355),
            (-0.28, sgn * 0.558, 1.355),
        ]
        add_extruded_prism(out, f"BPillar_Trim_{s_tag}", bp_loop, (0, sgn * 0.88, 0.47), 0.015, TRIM_BLACK, "carbon")

    shark_loop = [
        (-0.82, -0.012, 1.335),
        (-0.62, -0.012, 1.350),
        (-0.76, -0.012, 1.415),
    ]
    add_extruded_prism(out, "Roof_SharkFin", shark_loop, (0, 1, 0), 0.024, CARBON_DARK, "carbon")

    # 8. Build All 4 Complete M Forged Wheels, Tires, Rotors & Calipers
    build_wheel_assembly(out, "FR", X_AXLE_F,  1.0, R_TIRE_F, Z_AXLE_F)
    build_wheel_assembly(out, "FL", X_AXLE_F, -1.0, R_TIRE_F, Z_AXLE_F)
    build_wheel_assembly(out, "RR", X_AXLE_R,  1.0, R_TIRE_R, Z_AXLE_R)
    build_wheel_assembly(out, "RL", X_AXLE_R, -1.0, R_TIRE_R, Z_AXLE_R)

    out.append("view iso")
    out.append("view fit")

    arc_path = OUTDIR / "BMW_M4_G82.arc"
    arc_path.write_text("\n".join(out) + "\n")
    print(f"Wrote {arc_path} ({len(out)} lines)")


if __name__ == "__main__":
    generate_2d_sketch_arc()
    generate_3d_cad_arc()

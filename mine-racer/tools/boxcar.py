"""Canary MK-2 — the drivable 'box car' mine racer (3D, glTF export).

A boxy single-seat mine buggy: deliberately simple volumes ("box car") with
just enough dressing to read AAA-greybox: roll cage, chunky tires, light bar,
bull bar, exhaust stack, interior. ~14k tris, real PBR materials, named wheel
nodes so an engine can spin/steer them.

Y up, Z forward. Root at ground level, centered on origin.
Wheel radius 0.30 m. Overall: 3.5 m L x 1.9 m W x 1.95 m H (light bar).
"""
import numpy as np
import trimesh
from matlib import (MAT, box, cyl, rod, sphere, torus, tube, place, T,
                    merge_by_material, set_mat)

R_WHEEL = 0.30
WHEEL_X = 0.72          # half track
WHEEL_ZF, WHEEL_ZR = 1.08, -1.08


# ----------------------------------------------------------------- wheels
def build_wheel():
    """One wheel centered at origin, spin axis = X. ~2.6k tris"""
    parts = []
    # tire
    parts.append(cyl(R_WHEEL, 0.32, seg=28, mat="rubber", axis="x"))
    # sidewall rings (read as off-road tire lugs)
    for s in (-1, 1):
        parts.append(tube(R_WHEEL - 0.015, R_WHEEL - 0.09, 0.335, seg=28,
                          pos=(0, 0, 0), mat="rubber", axis="x"))
        # tread blocks
        for i in range(12):
            a = i * 2 * np.pi / 12
            bx = np.cos(a) * (R_WHEEL - 0.045)
            by = np.sin(a) * (R_WHEEL - 0.045)
            parts.append(box(0.062, 0.115, 0.345,
                             pos=(bx, by, 0), rot=(0, 0, a), mat="rubber"))
    # rim + hub
    parts.append(cyl(0.165, 0.335, seg=20, mat="steel_dark", axis="x"))
    parts.append(cyl(0.075, 0.35, seg=14, mat="steel", axis="x"))
    # 5 spokes
    for i in range(5):
        a = i * 2 * np.pi / 5 + 0.3
        parts.append(box(0.20, 0.055, 0.30,
                         pos=(np.cos(a) * 0.085, np.sin(a) * 0.085, s * 0.055),
                         rot=(0, 0, a), mat="steel"))
    # bead-lock bolts
    for i in range(10):
        a = i * 2 * np.pi / 10
        parts.append(cyl(0.016, 0.34, seg=6,
                         pos=(np.cos(a) * 0.135, np.sin(a) * 0.135, 0),
                         mat="chrome", axis="x"))
    return parts


# ----------------------------------------------------------------- body
def build_body():
    p = []  # parts

    # ---- chassis
    for s in (-1, 1):
        p.append(box(0.09, 0.10, 2.60, pos=(s * 0.30, 0.36, -0.05), mat="steel_dark"))
    for z in (-1.05, -0.1, 0.85):
        p.append(box(0.62, 0.07, 0.10, pos=(0, 0.36, z), mat="steel_dark"))
    # skid plate (angled, front)
    p.append(box(0.95, 0.05, 0.62, pos=(0, 0.28, 1.28), rot=(0.42, 0, 0), mat="steel"))
    # floor pan
    p.append(box(1.36, 0.05, 2.35, pos=(0, 0.445, -0.12), mat="steel_dark"))

    # ---- the BOX body (3 stacked volumes = chamfered box read)
    # lower skirt (black plastic cladding)
    p.append(box(1.66, 0.28, 2.92, pos=(0, 0.60, -0.10), mat="plastic"))
    # main box (safety yellow)
    p.append(box(1.60, 0.54, 2.84, pos=(0, 0.99, -0.13), mat="body"))
    # shoulder / cockpit surround (inset)
    p.append(box(1.44, 0.20, 2.34, pos=(0, 1.34, -0.42), mat="body"))
    # hood top plate slightly darker + scoop
    p.append(box(1.46, 0.05, 0.78, pos=(0, 1.065, 0.92), rot=(0.06, 0, 0), mat="body_dark"))
    p.append(box(0.66, 0.09, 0.44, pos=(0, 1.12, 0.90), mat="plastic"))

    # ---- fenders
    for sx in (-1, 1):
        for sz, zpos in ((-1, WHEEL_ZR), (1, WHEEL_ZF)):
            p.append(box(0.28, 0.10, 0.94, pos=(sx * 0.90, 0.77, zpos), mat="body_dark"))
            # fender strut
            p.append(rod((sx * 0.86, 0.55, zpos + sz * 0.30), (sx * 0.86, 0.72, zpos + sz * 0.30),
                         r=0.018, mat="steel_dark"))

    # ---- nose: bumper, bull bar, lights, grille
    p.append(box(1.52, 0.18, 0.16, pos=(0, 0.50, 1.50), mat="plastic"))
    # hazard stripe insert on bumper
    p.append(box(1.30, 0.10, 0.05, pos=(0, 0.50, 1.585), mat="hazard"))
    # bull bar
    p.append(rod((-0.55, 0.62, 1.62), (0.55, 0.62, 1.62), r=0.030, mat="steel"))
    p.append(rod((-0.55, 0.82, 1.60), (0.55, 0.82, 1.60), r=0.030, mat="steel"))
    for s in (-1, 1):
        p.append(rod((s * 0.55, 0.44, 1.58), (s * 0.55, 0.90, 1.585), r=0.030, mat="steel"))
    # headlights
    for s in (-1, 1):
        p.append(cyl(0.105, 0.09, seg=20, pos=(s * 0.52, 0.87, 1.435), mat="chrome", axis="z"))
        p.append(cyl(0.085, 0.10, seg=20, pos=(s * 0.52, 0.87, 1.437), mat="headlight", axis="z"))
    # grille
    p.append(box(0.72, 0.16, 0.06, pos=(0, 0.87, 1.44), mat="plastic"))
    for i in range(3):
        p.append(box(0.72, 0.018, 0.05, pos=(0, 0.81 + i * 0.055, 1.465), mat="steel_dark"))
    # tow hook
    p.append(torus(0.055, 0.018, pos=(0.55, 0.38, 1.56), rot=(np.pi / 2, 0, 0), mat="hazard"))

    # ---- windshield (low rally strip)
    p.append(box(1.30, 0.42, 0.035, pos=(0, 1.45, 0.63), rot=(-0.38, 0, 0), mat="glass"))

    # ---- interior
    # seat
    p.append(box(0.52, 0.10, 0.58, pos=(0, 1.29, -0.66), mat="seat"))
    p.append(box(0.52, 0.62, 0.11, pos=(0, 1.58, -0.97), rot=(-0.16, 0, 0), mat="seat"))
    p.append(box(0.30, 0.18, 0.10, pos=(0, 1.90, -1.02), rot=(-0.16, 0, 0), mat="body_dark"))
    # harness straps
    for s in (-1, 1):
        p.append(box(0.07, 0.5, 0.02, pos=(s * 0.13, 1.56, -0.90), rot=(-0.16, 0, 0), mat="hazard"))
    # dash
    p.append(box(1.20, 0.22, 0.30, pos=(0, 1.33, 0.40), mat="body_dark"))
    p.append(box(0.40, 0.17, 0.03, pos=(0, 1.44, 0.31), rot=(-0.25, 0, 0), mat="screen"))
    # steering column + wheel
    p.append(rod((0, 1.30, 0.42), (0, 1.50, 0.24), r=0.022, mat="steel_dark"))
    sw = trimesh.creation.torus(major_radius=0.15, minor_radius=0.021,
                                major_sections=24, minor_sections=10)
    set_mat(sw, "plastic")
    p.append(place(sw, (0, 1.51, 0.20), (-0.35, 0, 0)))
    for a in (0, 2.1, -2.1):
        p.append(box(0.03, 0.13, 0.02, pos=(np.sin(a) * 0.075, 1.51 - np.cos(a) * 0.075 * 0.94, 0.20 + 0.02),
                     rot=(-0.35, 0, a), mat="plastic"))
    # shifter
    p.append(rod((0.30, 1.30, 0.02), (0.34, 1.48, -0.06), r=0.016, mat="steel_dark"))
    p.append(sphere(0.035, pos=(0.35, 1.50, -0.07), mat="hazard"))
    # fire extinguisher
    p.append(cyl(0.05, 0.34, seg=12, pos=(0.42, 1.48, -0.30), mat="barrel_r", axis="y"))

    # ---- roll cage (dark steel tube, r=0.048)
    CAGE = [
        # main hoop verticals + top
        ((-0.60, 1.42, -0.82), (-0.60, 1.80, -0.82)),
        ((0.60, 1.42, -0.82), (0.60, 1.80, -0.82)),
        ((-0.60, 1.80, -0.82), (0.60, 1.80, -0.82)),
        # halo bars to cowl
        ((-0.60, 1.80, -0.82), (-0.57, 1.34, 0.66)),
        ((0.60, 1.80, -0.82), (0.57, 1.34, 0.66)),
        # spreader bar
        ((-0.57, 1.34, 0.66), (0.57, 1.34, 0.66)),
        # rear stays
        ((-0.60, 1.80, -0.82), (-0.56, 1.27, -1.44)),
        ((0.60, 1.80, -0.82), (0.56, 1.27, -1.44)),
        # mid brace
        ((-0.60, 1.55, -0.82), (0.60, 1.55, -0.82)),
        # door bars (side impact)
        ((-0.60, 1.42, -0.82), (-0.70, 1.10, 0.55)),
        ((0.60, 1.42, -0.82), (0.70, 1.10, 0.55)),
    ]
    for a, b in CAGE:
        p.append(rod(a, b, r=0.048, seg=12, mat="steel_dark"))
        p.append(sphere(0.058, pos=b, mat="steel_dark"))
        p.append(sphere(0.058, pos=a, mat="steel_dark"))

    # ---- roof light bar (mine-legal amber strobes)
    p.append(box(0.90, 0.08, 0.10, pos=(0, 1.87, -0.82), mat="lamp"))
    for i in range(4):
        p.append(cyl(0.036, 0.09, seg=12, pos=(-0.33 + i * 0.22, 1.87, -0.875),
                     mat="amber", axis="z"))
    # beacon on rear deck
    p.append(cyl(0.05, 0.09, seg=12, pos=(0, 1.90, -1.30), mat="amber", axis="y"))

    # ---- rear: bumper, lights, hazard plate, exhaust, fuel can
    p.append(box(1.52, 0.18, 0.16, pos=(0, 0.50, -1.52), mat="plastic"))
    for s in (-1, 1):
        p.append(cyl(0.062, 0.09, seg=16, pos=(s * 0.58, 0.92, -1.555), mat="taillight", axis="z"))
    p.append(box(1.28, 0.22, 0.04, pos=(0, 0.70, -1.555), mat="hazard"))
    # exhaust stack
    p.append(cyl(0.046, 0.92, seg=12, pos=(0.56, 1.02, -1.28), mat="steel_dark", axis="y"))
    p.append(cyl(0.055, 0.10, seg=12, pos=(0.56, 1.52, -1.28), mat="chrome", axis="y"))
    p.append(rod((0.52, 1.25, -1.28), (0.60, 1.25, -1.28), r=0.014, mat="steel_dark"))
    # spare fuel can on rear deck
    p.append(box(0.36, 0.46, 0.17, pos=(-0.52, 1.48, -1.28), mat="hazard"))
    p.append(box(0.30, 0.05, 0.12, pos=(-0.52, 1.72, -1.28), mat="plastic"))
    # rear mud flaps
    for s in (-1, 1):
        p.append(box(0.34, 0.30, 0.025, pos=(s * 0.72, 0.32, -1.52), mat="rubber"))

    # ---- mirrors + antenna
    for s in (-1, 1):
        p.append(rod((s * 0.74, 1.30, 0.62), (s * 0.94, 1.44, 0.66), r=0.014, mat="steel_dark"))
        p.append(box(0.05, 0.14, 0.10, pos=(s * 0.97, 1.50, 0.67), mat="plastic"))
    p.append(rod((-0.60, 1.27, -1.40), (-0.60, 1.95, -1.48), r=0.008, mat="steel_dark"))

    # ---- side steps + number panels
    for s in (-1, 1):
        p.append(box(0.16, 0.05, 0.85, pos=(s * 0.86, 0.44, -0.25), mat="steel_dark"))
        p.append(box(0.03, 0.26, 0.55, pos=(s * 0.805, 0.99, -0.15), mat="body_dark"))
    return p


# ----------------------------------------------------------------- assembly
def build_scene():
    scene = trimesh.Scene()
    # body: merge by material -> few clean nodes under root
    for mat_name, mesh in merge_by_material(build_body()).items():
        scene.add_geometry(mesh, node_name="body_" + mat_name, geom_name=mat_name)
    # wheels: separate nodes, geometry centered on node origin (spin axis X)
    wheels = [("fl", WHEEL_X, WHEEL_ZF), ("fr", -WHEEL_X, WHEEL_ZF),
              ("rl", WHEEL_X, WHEEL_ZR), ("rr", -WHEEL_X, WHEEL_ZR)]
    for tag, x, z in wheels:
        parts = build_wheel()
        for mat_name, mesh in merge_by_material(parts).items():
            scene.add_geometry(mesh,
                               node_name=f"wheel_{tag}_{mat_name}",
                               geom_name=f"wheel_{mat_name}",
                               transform=T((x, R_WHEEL, z)))
    return scene


if __name__ == "__main__":
    scene = build_scene()
    out = "../assets/models/boxcar.glb"
    data = scene.export(out)
    n_tri = sum(len(m.faces) for m in scene.dump())
    print(f"boxcar.glb written | {n_tri:,} tris | "
          f"{len(scene.graph.nodes_geometry)} geometry nodes")

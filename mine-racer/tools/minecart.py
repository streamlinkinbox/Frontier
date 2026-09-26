"""Mine cart — the traffic obstacles (3D, glTF export).

Granby-style tipping ore cart: tapered riveted body, steel frame, flanged
wheels on 0.9 m gauge, couplers, ore load, forward lamp + red tail marker.
Rail gauge 0.9 m, body 2.4 x 1.5 x 1.6 m. ~6k tris.
"""
import numpy as np
import trimesh
from matlib import (MAT, box, cyl, rod, sphere, torus, place, T, set_mat,
                    merge_by_material)


def tapered_box(bw, tw, bl, tl, h, pos=(0, 0, 0), mat="steel"):
    """Box with different bottom/top half-width (bw/tw on x, bl/tl on z), base at y=0."""
    v = np.array([
        [-bw, 0, -bl], [bw, 0, -bl], [bw, 0, bl], [-bw, 0, bl],
        [-tw, h, -tl], [tw, h, -tl], [tw, h, tl], [-tw, h, tl]], float)
    f = np.array([
        [0, 1, 2], [0, 2, 3],          # bottom
        [4, 6, 5], [4, 7, 6],          # top
        [0, 4, 5], [0, 5, 1],          # back
        [2, 6, 7], [2, 7, 3],          # front
        [1, 5, 6], [1, 6, 2],          # right
        [0, 3, 7], [0, 7, 4],          # left
    ])
    m = trimesh.Trimesh(vertices=v, faces=f, process=False)
    m.fix_normals()
    set_mat(m, mat)
    return place(m, pos)


def build_cart(loaded=True):
    p = []
    # ---- body (tapered, dark steel) + rim
    p.append(tapered_box(0.62, 0.76, 1.02, 1.22, 1.05, pos=(0, 0.52, 0), mat="steel_dark"))
    # top rim band
    p.append(tapered_box(0.78, 0.78, 1.24, 1.24, 0.10, pos=(0, 1.52, 0), mat="steel"))
    # rivet ribs (side + end)
    for s in (-1, 1):
        for z in (-0.45, 0, 0.45):
            p.append(box(0.02, 1.05, 0.07, pos=(s * 0.695, 1.03, z), rot=(0, 0.03, 0), mat="steel"))
        for x in (-0.35, 0, 0.35):
            p.append(box(0.07, 1.05, 0.02, pos=(x, 1.03, s * 1.135), mat="steel"))
    # ore load
    if loaded:
        ore = trimesh.creation.icosphere(subdivisions=2, radius=1.0)
        ore.apply_scale((0.68, 0.38, 1.05))
        ore.apply_translation((0, 1.52, 0))
        # clip below rim roughly by squashing; keep simple
        set_mat(ore, "ore")
        p.append(ore)
    # ---- chassis
    p.append(box(1.1, 0.10, 2.0, pos=(0, 0.42, 0), mat="steel_dark"))
    for s in (-1, 1):
        p.append(box(0.08, 0.12, 2.0, pos=(s * 0.45, 0.32, 0), mat="steel_dark"))
    # tipping hinge + trunnions
    for s in (-1, 1):
        p.append(cyl(0.06, 0.14, seg=10, pos=(s * 0.68, 0.72, 0), mat="steel", axis="x"))
    # ---- wheels (flanged): tire + flange disc + hub
    for sx in (-1, 1):
        for sz in (-1, 1):
            x, z = sx * 0.45, sz * 0.78
            p.append(cyl(0.22, 0.09, seg=18, pos=(x, 0.22, z), mat="steel_dark", axis="x"))
            p.append(cyl(0.27, 0.03, seg=18, pos=(x + sx * 0.045, 0.22, z), mat="steel_dark", axis="x"))
            p.append(cyl(0.06, 0.13, seg=10, pos=(x, 0.22, z), mat="steel", axis="x"))
    # ---- couplers (buffers + chain hook)
    for sz in (-1, 1):
        for sx in (-1, 1):
            p.append(cyl(0.055, 0.16, seg=10, pos=(sx * 0.28, 0.44, sz * 1.12), mat="steel", axis="z"))
        p.append(box(0.5, 0.08, 0.08, pos=(0, 0.30, sz * 1.1), mat="steel_dark"))
        p.append(rod((0, 0.30, sz * 1.12), (0, 0.30, sz * 1.30), r=0.025, mat="steel"))
    # ---- lamps: forward warm headlamp + rear red marker
    p.append(cyl(0.07, 0.06, seg=12, pos=(0, 1.72, 1.24), mat="lamp", axis="z"))
    p.append(cyl(0.055, 0.07, seg=12, pos=(0, 1.72, 1.245), mat="warm", axis="z"))
    p.append(cyl(0.05, 0.05, seg=10, pos=(0, 1.15, -1.29), mat="red", axis="z"))
    # side warning flags/markers
    for s in (-1, 1):
        p.append(cyl(0.035, 0.05, seg=8, pos=(s * 0.8, 1.1, -0.9), mat="amber", axis="x"))
    return p


def build_scene():
    scene = trimesh.Scene()
    for mat_name, mesh in merge_by_material(build_cart()).items():
        scene.add_geometry(mesh, node_name="cart_" + mat_name, geom_name=mat_name)
    return scene


if __name__ == "__main__":
    scene = build_scene()
    scene.export("../assets/models/minecart.glb")
    n_tri = sum(len(m.faces) for m in scene.dump())
    print(f"minecart.glb written | {n_tri:,} tris")

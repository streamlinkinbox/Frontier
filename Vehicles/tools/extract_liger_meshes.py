#!/usr/bin/env python3
"""
Extract clean, consistently oriented mesh archives (.npz) for the Liger vehicle
from Vehicles/Liger/Liger_named.blend using headless bpy 5.0.1.

For subdivision parts (Body_Main_Shell, Body_Front_Cowl, Body_Roof_Glass_Frame,
Aero_Side_Skirt), stores:
  - V: (N_v, 3) float32 world-space vertices in cm (Catmull-Clark level 2)
  - T: (N_t, 3) int32 triangles (2 per Catmull-Clark level-2 sub-quad, in contiguous face order)
  - base_deg: (N_base,) int32 polygon degrees of the mirrored control cage
  - base_verts: flattened int32 corner vertex indices of the mirrored control cage

For non-subdivision aero parts, stores:
  - V: (N_v, 3) float32 world-space vertices in cm
  - poly_deg: (N_p,) int32 polygon degrees
  - poly_verts: flattened int32 polygon vertex indices

For structured wheel parts (Wheel_Front, Wheel_Rear), stores:
  - G_right: (65, 34, 3) float32 structured revolution grid in metres (centered at Y=0 symmetry plane)
"""

import os
import bpy
import bmesh
import numpy as np

BLEND_PATH = os.path.join("Vehicles", "Liger", "Liger_named.blend")
OUT_DIR = os.path.join("Vehicles", "Liger", "mesh")
Y_OFFSET_M = 0.47858985900878906


def orient_open_sheet_components_upward(obj, flip_mode="nz"):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    mw = np.array(obj.matrix_world, dtype=np.float64)
    rot = mw[:3, :3]
    visited = set()
    to_flip = []
    for f in bm.faces:
        if f.index in visited:
            continue
        stack = [f]
        visited.add(f.index)
        comp = []
        while stack:
            cur = stack.pop()
            comp.append(cur)
            for e in cur.edges:
                for nf in e.link_faces:
                    if nf.index not in visited:
                        visited.add(nf.index)
                        stack.append(nf)
        if flip_mode == "all":
            to_flip.extend(comp)
        elif flip_mode == "nz":
            avg_nz = sum(cf.calc_area() * (rot @ np.array(cf.normal))[2] for cf in comp)
            if avg_nz < 0:
                to_flip.extend(comp)
    if to_flip:
        bmesh.ops.reverse_faces(bm, faces=to_flip)
        bm.to_mesh(obj.data)
        obj.data.update()
    bm.free()


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=BLEND_PATH)

    sub_parts = [
        ("Body_Main_Shell", "none"),
        ("Body_Front_Cowl", "all"),
        ("Body_Roof_Glass_Frame", "nz"),
        ("Aero_Side_Skirt", "none"),
    ]

    for obj_name, flip_mode in sub_parts:
        orig = bpy.data.objects[obj_name]
        obj = orig.copy()
        obj.data = orig.data.copy()
        bpy.context.collection.objects.link(obj)
        if flip_mode != "none":
            orient_open_sheet_components_upward(obj, flip_mode)
        for md in list(obj.modifiers):
            if md.type == "MIRROR":
                md.show_viewport = True
            else:
                obj.modifiers.remove(md)
        deps = bpy.context.evaluated_depsgraph_get()
        base_mirrored = bpy.data.meshes.new_from_object(obj.evaluated_get(deps))

        test_obj = bpy.data.objects.new(f"Tmp_{obj_name}", base_mirrored)
        bpy.context.collection.objects.link(test_obj)
        sub = test_obj.modifiers.new("Subsurf", "SUBSURF")
        sub.levels = 2
        deps = bpy.context.evaluated_depsgraph_get()
        sub_mesh = bpy.data.meshes.new_from_object(test_obj.evaluated_get(deps))

        mw = np.array(orig.matrix_world, dtype=np.float64)
        rot, trans = mw[:3, :3], mw[:3, 3]
        co_flat = np.empty(len(sub_mesh.vertices) * 3, dtype=np.float64)
        sub_mesh.vertices.foreach_get("co", co_flat)
        V_cm = (co_flat.reshape(-1, 3) @ rot.T + trans).astype(np.float32)

        # Build T: 2 triangles per sub-quad [v0, v1, v2], [v0, v2, v3]
        n_q = len(sub_mesh.polygons)
        quads = np.empty((n_q, 4), dtype=np.int32)
        sub_mesh.polygons.foreach_get("vertices", quads.ravel())
        T = np.empty((n_q * 2, 3), dtype=np.int32)
        T[0::2, 0] = quads[:, 0]
        T[0::2, 1] = quads[:, 1]
        T[0::2, 2] = quads[:, 2]
        T[1::2, 0] = quads[:, 0]
        T[1::2, 1] = quads[:, 2]
        T[1::2, 2] = quads[:, 3]

        base_deg = np.array([len(p.vertices) for p in base_mirrored.polygons], dtype=np.int32)
        base_verts = np.array([v for p in base_mirrored.polygons for v in p.vertices], dtype=np.int32)

        out_path = os.path.join(OUT_DIR, f"{obj_name}.npz")
        np.savez_compressed(out_path, V=V_cm, T=T, base_deg=base_deg, base_verts=base_verts)
        print(f"Saved {out_path}: V={V_cm.shape}, T={T.shape}, base_polys={len(base_deg)}")

    nosub_parts = [
        "Aero_Front_Lip",
        "Aero_Front_Splitter",
        "Aero_Rear_Fender_Flare",
        "Aero_Rear_Wing",
        "Aero_Roof_Fin",
    ]
    for obj_name in nosub_parts:
        orig = bpy.data.objects[obj_name]
        obj = orig.copy()
        obj.data = orig.data.copy()
        bpy.context.collection.objects.link(obj)
        for md in list(obj.modifiers):
            if md.type == "MIRROR" or (md.type == "SOLIDIFY" and obj_name != "Aero_Front_Splitter"):
                md.show_viewport = True
            else:
                obj.modifiers.remove(md)
        deps = bpy.context.evaluated_depsgraph_get()
        em = bpy.data.meshes.new_from_object(obj.evaluated_get(deps))
        mw = np.array(orig.matrix_world, dtype=np.float64)
        rot, trans = mw[:3, :3], mw[:3, 3]
        co_flat = np.empty(len(em.vertices) * 3, dtype=np.float64)
        em.vertices.foreach_get("co", co_flat)
        V_cm = (co_flat.reshape(-1, 3) @ rot.T + trans).astype(np.float32)
        poly_deg = np.array([len(p.vertices) for p in em.polygons], dtype=np.int32)
        poly_verts = np.array([v for p in em.polygons for v in p.vertices], dtype=np.int32)
        out_path = os.path.join(OUT_DIR, f"{obj_name}.npz")
        np.savez_compressed(out_path, V=V_cm, poly_deg=poly_deg, poly_verts=poly_verts)
        print(f"Saved {out_path}: V={V_cm.shape}, polys={len(poly_deg)}")

    for obj_name in ["Wheel_Front", "Wheel_Rear"]:
        orig = bpy.data.objects[obj_name]
        mw = np.array(orig.matrix_world, dtype=np.float64)
        rot, trans = mw[:3, :3], mw[:3, 3]
        verts = np.array([(rot @ np.array(v.co) + trans) * 0.01 for v in orig.data.vertices], dtype=np.float64)
        verts[:, 1] += Y_OFFSET_M

        bm = bmesh.new()
        bm.from_mesh(orig.data)
        bm.faces.ensure_lookup_table()
        b_loops = [l for f in bm.faces for l in f.loops if l.edge.is_boundary]
        l0 = b_loops[0]
        ring = []
        cur = l0
        while True:
            ring.append(cur)
            cur = cur.link_loop_next.link_loop_radial_next.link_loop_next
            if cur == l0:
                break
        grid_idx = np.zeros((64, 34), dtype=int)
        for i, start_l in enumerate(ring):
            c = start_l
            grid_idx[i, 0] = c.vert.index
            for j in range(33):
                c_opp = c.link_loop_next.link_loop_next
                grid_idx[i, j + 1] = c_opp.link_loop_next.vert.index
                if j < 32:
                    c = c_opp.link_loop_radial_next
        bm.free()
        grid_idx_65 = np.vstack([grid_idx, grid_idx[0:1, :]])
        G_right = verts[grid_idx_65].astype(np.float32)
        out_path = os.path.join(OUT_DIR, f"{obj_name}.npz")
        np.savez_compressed(out_path, G_right=G_right)
        print(f"Saved {out_path}: G_right={G_right.shape}")


if __name__ == "__main__":
    main()

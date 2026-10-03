#!/usr/bin/env python3
"""
Reconstruct the Liger vehicle into native SolidArc (.arc) CAD documents directly from
the extracted mesh archives (Vehicles/Liger/mesh/*.npz) and 3D feature curves
(Vehicles/Liger/curves.json).

Produces:
  - Vehicles/Liger/Liger_Body_Surface.arc   (Exact Catmull-Clark NURBS exterior body patches)
  - Vehicles/Liger/Liger_Body_Panels.arc    (Multi-panel segmented & color-coded NURBS body)
  - Vehicles/Liger/Liger_Body_CurveLoft.arc (NURBS exterior body overlaid with 3D feature/crease curves)
  - Vehicles/Liger/Liger_Body_SideR.arc     (+Y right-hand half-shell NURBS body + right-hand 3D feature curves)
  - Vehicles/Liger/Liger_Complete.arc       (Complete Liger hypercar: Body + Aero + 10-Spoke Wheels/Tyres/Brakes)
"""

import math
import os
import time
import numpy as np

LIGER_DIR = os.path.join("Vehicles", "Liger")
MESH_DIR = os.path.join(LIGER_DIR, "mesh")
Y_OFFSET_M = 0.47858985900878906

# 1D cubic Bernstein least-squares operator A3 (4x5) with exact endpoint interpolation
t5 = np.linspace(0.0, 1.0, 5)
B3 = np.column_stack([(1 - t5) ** 3, 3 * t5 * (1 - t5) ** 2, 3 * (t5**2) * (1 - t5), t5**3])
pinv_int = np.linalg.pinv(B3[1:4, 1:3])
A3 = np.zeros((4, 5), dtype=np.float64)
A3[0, 0] = 1.0
A3[3, 4] = 1.0
A3[1:3, 1:4] = pinv_int
A3[1:3, [0, 4]] = -pinv_int @ B3[1:4, [0, 3]]

# 1D quadratic Bernstein interpolation operator A2 (3x3)
t3 = np.array([0.0, 0.5, 1.0])
B2 = np.column_stack([(1 - t3) ** 2, 2 * t3 * (1 - t3), t3**2])
A2 = np.linalg.inv(B2)


def extract_quad_grid_5x5(sub_quads, v0):
    quad_map = {}
    start_q = None
    for vs in sub_quads:
        a, b, c, d = int(vs[0]), int(vs[1]), int(vs[2]), int(vs[3])
        quad_map[(a, b)] = (a, b, c, d)
        quad_map[(b, c)] = (b, c, d, a)
        quad_map[(c, d)] = (c, d, a, b)
        quad_map[(d, a)] = (d, a, b, c)
        if start_q is None:
            if a == v0:
                start_q = (a, b, c, d)
            elif b == v0:
                start_q = (b, c, d, a)
            elif c == v0:
                start_q = (c, d, a, b)
            elif d == v0:
                start_q = (d, a, b, c)
    grid = np.empty((5, 5), dtype=np.int32)
    grid[0, 0], grid[1, 0], grid[1, 1], grid[0, 1] = start_q
    for u in range(1, 4):
        q = quad_map[(grid[u, 1], grid[u, 0])]
        grid[u + 1, 0] = q[2]
        grid[u + 1, 1] = q[3]
    for v in range(1, 4):
        for u in range(4):
            q = quad_map[(grid[u, v], grid[u + 1, v])]
            grid[u + 1, v + 1] = q[2]
            grid[u, v + 1] = q[3]
    return grid


def extract_ngon_grids_3x3(sub_quads, k):
    quad_map = {}
    for vs in sub_quads:
        a, b, c, d = int(vs[0]), int(vs[1]), int(vs[2]), int(vs[3])
        quad_map[(a, b)] = (a, b, c, d)
        quad_map[(b, c)] = (b, c, d, a)
        quad_map[(c, d)] = (c, d, a, b)
        quad_map[(d, a)] = (d, a, b, c)
    grids = []
    for j in range(k):
        vs = sub_quads[k + 3 * j]
        start_q = (int(vs[0]), int(vs[1]), int(vs[2]), int(vs[3]))
        grid = np.empty((3, 3), dtype=np.int32)
        grid[0, 0], grid[1, 0], grid[1, 1], grid[0, 1] = start_q
        q_u = quad_map[(grid[1, 1], grid[1, 0])]
        grid[2, 0] = q_u[2]
        grid[2, 1] = q_u[3]
        for u in range(2):
            q_v = quad_map[(grid[u, 1], grid[u + 1, 1])]
            grid[u + 1, 2] = q_v[2]
            grid[u, 2] = q_v[3]
        grids.append(grid)
    return grids


def load_subsurf_patches_from_npz(npz_path, prefix, orient_mode="none"):
    """
    Reconstruct exact bicubic (4x4) and biquadratic (3x3) NURBS patches directly
    from the (V, T) Catmull-Clark level-2 mesh archive in npz_path.
    """
    d = np.load(npz_path)
    V_cm = d["V"].astype(np.float64)
    verts_world = V_cm * 0.01
    verts_world[:, 1] += Y_OFFSET_M

    T = d["T"]
    quads = np.column_stack([T[0::2, 0], T[0::2, 1], T[0::2, 2], T[1::2, 2]])
    n_q = len(quads)

    patches = []  # list of (fname, degree_str, P_grid, center_xyz)
    offset = 0
    idx = 0

    def emit_patch(P, deg_str):
        nonlocal idx
        c = P.mean(axis=(0, 1))
        n = np.cross(P[-1, 0] - P[0, 0], P[0, -1] - P[0, 0])
        n_len = np.linalg.norm(n)
        n_unit = n / n_len if n_len > 1e-12 else np.array([0.0, 0.0, 1.0])
        if orient_mode == "nz" and n_unit[2] < 0:
            P = P.swapaxes(0, 1)
            n_unit = -n_unit
        elif orient_mode == "skirt" and (n_unit[1] * (1.0 if c[1] >= 0 else -1.0) + 0.6 * n_unit[2]) < 0:
            P = P.swapaxes(0, 1)
            n_unit = -n_unit
        fname = f"{prefix}_{idx:04d}"
        patches.append((fname, deg_str, P, c))
        idx += 1
        # Add a 2.5 mm inner liner on single-sided wheel-arch flanges and the vertical rear spoiler lip
        # so open wheel-well views (Body_Surface / Panels / SideR) and front-3/4 views never see back-faces:
        if orient_mode == "none":
            is_wheel_arch_lip = abs(c[1]) > 0.72 and c[2] < 0.80
            is_rear_spoiler_lip = c[0] < -1.50 and abs(c[1]) < 0.62 and c[2] > 0.83
            if is_wheel_arch_lip or is_rear_spoiler_lip:
                P_in = (P - 0.0025 * n_unit).swapaxes(0, 1)
                fname_in = f"{prefix}_{idx:04d}"
                patches.append((fname_in, deg_str, P_in, c))
                idx += 1

    while offset < n_q:
        c_vert = quads[offset, 2]
        k = 0
        while offset + k < n_q and c_vert in quads[offset + k]:
            k += 1
        n_sub = 4 * k
        sub_slice = quads[offset : offset + n_sub]
        offset += n_sub

        if k == 4:
            v0 = int(sub_slice[4, 0])
            g = extract_quad_grid_5x5(sub_slice, v0)
            Q = verts_world[g]
            P = np.einsum("iu,uvd,jv->ijd", A3, Q, A3)
            emit_patch(P, "4 4")
        else:
            for g in extract_ngon_grids_3x3(sub_slice, k):
                Q = verts_world[g]
                P = np.einsum("iu,uvd,jv->ijd", A2, Q, A2)
                emit_patch(P, "3 3")
    return patches


def load_nosub_patches_from_npz(npz_path, prefix, obj_name):
    d = np.load(npz_path)
    verts_world = d["V"].astype(np.float64) * 0.01
    verts_world[:, 1] += Y_OFFSET_M
    poly_deg = d["poly_deg"]
    poly_verts = d["poly_verts"]

    patches = []
    v_off = 0
    idx = 0

    def add_quad_patch(P, fc):
        nonlocal idx
        pn = np.cross(P[2] - P[0], P[1] - P[0])
        pn_len = np.linalg.norm(pn)
        if pn_len < 1e-9:
            return
        pn_unit = pn / pn_len
        if obj_name in ("Aero_Rear_Fender_Flare", "Aero_Front_Splitter"):
            if obj_name == "Aero_Rear_Fender_Flare":
                y_w = 2.8 if abs(fc[1]) > 0.85 else 1.2
                out_dir = np.array([-0.5, y_w if fc[1] >= 0 else -y_w, 1.0])
            else:
                out_dir = np.array([0.5, 0.8 if fc[1] >= 0 else -0.8, 1.5])
            if np.dot(pn_unit, out_dir) < 0:
                P = [P[0], P[2], P[1], P[3]]
                pn_unit = -pn_unit
        P_arr = np.array(P).reshape(2, 2, 3)
        fname = f"{prefix}_{idx:04d}"
        patches.append((fname, "2 2", P_arr, fc))
        idx += 1
        if obj_name in ("Aero_Rear_Fender_Flare", "Aero_Front_Splitter"):
            P_inner = [P[0] - 0.0025 * pn_unit, P[2] - 0.0025 * pn_unit, P[1] - 0.0025 * pn_unit, P[3] - 0.0025 * pn_unit]
            P_in_arr = np.array(P_inner).reshape(2, 2, 3)
            fname_in = f"{prefix}_{idx:04d}"
            patches.append((fname_in, "2 2", P_in_arr, fc))
            idx += 1

    for k in poly_deg:
        k = int(k)
        bvs = poly_verts[v_off : v_off + k]
        v_off += k
        pts = verts_world[bvs]
        if k == 4:
            add_quad_patch([pts[0], pts[3], pts[1], pts[2]], pts.mean(axis=0))
        else:
            c = pts.mean(axis=0)
            for j in range(k):
                vj = pts[j]
                mj = 0.5 * (pts[j] + pts[(j + 1) % k])
                m_prev = 0.5 * (pts[(j - 1) % k] + pts[j])
                add_quad_patch([vj, m_prev, mj, c], c)
    return patches


def format_patch_line(fname, deg_str, P):
    nu, nv = P.shape[0], P.shape[1]
    pts_str = " ".join(f"({P[u,v,0]:.5f},{P[u,v,1]:.5f},{P[u,v,2]:.5f})" for u in range(nu) for v in range(nv))
    return f"patch {deg_str} {pts_str} --name={fname}"


def load_3d_feature_curve_lines(right_side_only=False):
    """
    Extract the 3D crease & boundary curves from Vehicles/Liger/Liger_Body_Curves.arc
    (excluding the 2D orthographic projection silhouettes View_* and blueprint frame box Frame_*).
    """
    curve_lines = []
    src = os.path.join(LIGER_DIR, "Liger_Body_Curves.arc")
    if not os.path.exists(src):
        return curve_lines
    kept_names = set()
    for line in open(src):
        s = line.strip()
        if not s or s.startswith("#") or s == "reset":
            continue
        if "Sil_" in s or "Frame_" in s or "View_" in s:
            continue
        if s.startswith("line ") or s.startswith("spline "):
            cname = [t.split("=")[1] for t in s.split() if t.startswith("--name=")][0]
            if right_side_only:
                pts = [tuple(map(float, t.strip("()").split(","))) for t in s.split() if t.startswith("(")]
                if pts and min(p[1] for p in pts) < -0.02:
                    continue
            kept_names.add(cname)
            curve_lines.append(s)
        elif s.startswith("tint "):
            toks = s.split()
            if len(toks) >= 2 and toks[1] in kept_names:
                curve_lines.append(s)
    return curve_lines


def classify_panel_zone(c):
    """Classify a patch center (x, y, z) in metres into an automotive panel zone."""
    x, y, z = c[0], c[1], c[2]
    ay = abs(y)
    if z > 0.84 and -0.45 <= x <= 1.25 and ay < 0.72:
        return "Canopy_Roof"
    if x > 1.30 and ay < 0.62 and z > 0.55:
        return "Front_Hood"
    if x > 1.95 and z <= 0.62:
        return "Front_Fascia"
    if x > 0.85 and ay >= 0.58:
        return "Front_Fenders"
    if -0.60 <= x <= 1.15 and ay >= 0.55:
        return "Side_Doors_Pods"
    if x < -0.55 and ay >= 0.58:
        return "Rear_Haunches"
    if x < -1.45 and z < 0.88:
        return "Rear_Fascia"
    return "Rear_Engine_Deck"


def main():
    t0 = time.time()
    shell_patches = load_subsurf_patches_from_npz(os.path.join(MESH_DIR, "Body_Main_Shell.npz"), "Shell", "none")
    cowl_patches = load_subsurf_patches_from_npz(os.path.join(MESH_DIR, "Body_Front_Cowl.npz"), "Cowl", "nz")
    roof_patches = load_subsurf_patches_from_npz(os.path.join(MESH_DIR, "Body_Roof_Glass_Frame.npz"), "RoofFrame", "nz")

    # Recessed backing plates inside the rear decklid slot and front radiator mouth
    extra_body_patches = [
        (
            "RearDeck_Louver_Plate",
            "2 2",
            np.array([[[-1.535, -0.66, 0.971], [-1.535, 0.66, 0.971]], [[-1.485, -0.66, 0.852], [-1.485, 0.66, 0.852]]]),
            np.array([-1.51, 0.0, 0.91]),
        ),
        (
            "FrontGrille_Radiator_Plate",
            "2 2",
            np.array([[[2.36, -0.48, 0.385], [2.22, -0.48, 0.520]], [[2.36, 0.48, 0.385], [2.22, 0.48, 0.520]]]),
            np.array([2.29, 0.0, 0.45]),
        ),
    ]

    all_body_patches = shell_patches + cowl_patches + roof_patches + extra_body_patches
    print(f"Loaded exterior body patches: Shell={len(shell_patches)}, Cowl={len(cowl_patches)}, RoofFrame={len(roof_patches)}")

    # -------------------------------------------------------------------------
    # 1. Write Vehicles/Liger/Liger_Body_Surface.arc
    # -------------------------------------------------------------------------
    surf_lines = [
        "# SolidArc native document v1",
        "# Liger Exterior Body Surface — Exact Catmull-Clark B-spline NURBS patches (Body_Main_Shell + Body_Front_Cowl + Body_Roof_Glass_Frame)",
        "reset",
    ]
    for fname, deg_str, P, _ in all_body_patches:
        surf_lines.append(format_patch_line(fname, deg_str, P))

    shell_names = [f for f, _, _, _ in shell_patches]
    cowl_names = [f for f, _, _, _ in cowl_patches]
    roof_names = [f for f, _, _, _ in roof_patches]
    extra_names = [f for f, _, _, _ in extra_body_patches]

    for i in range(0, len(shell_names), 100):
        b = " ".join(shell_names[i : i + 100])
        surf_lines.append(f"tint {b} 0.740 0.780 0.840")
        surf_lines.append(f"matcap {b} pearl")
    for i in range(0, len(cowl_names), 100):
        b = " ".join(cowl_names[i : i + 100])
        surf_lines.append(f"tint {b} 0.250 0.270 0.310")
        surf_lines.append(f"matcap {b} carbon")
    for i in range(0, len(roof_names), 100):
        b = " ".join(roof_names[i : i + 100])
        surf_lines.append(f"tint {b} 0.220 0.320 0.440")
        surf_lines.append(f"matcap {b} glass")
    for b in extra_names:
        surf_lines.append(f"tint {b} 0.160 0.170 0.200")
        surf_lines.append(f"matcap {b} carbon")

    open(os.path.join(LIGER_DIR, "Liger_Body_Surface.arc"), "w").write("\n".join(surf_lines) + "\n")

    # -------------------------------------------------------------------------
    # 2. Write Vehicles/Liger/Liger_Body_Panels.arc (segmented & color-coded by panel zone)
    # -------------------------------------------------------------------------
    panel_lines = [
        "# SolidArc native document v1",
        "# Liger Exterior Body Panels — crease-bounded anatomical panel segmentation",
        "reset",
    ]
    zone_palette = {
        "Front_Hood":       ((0.28, 0.56, 0.86), "plastic-blue"),
        "Front_Fascia":     ((0.22, 0.26, 0.32), "carbon"),
        "Front_Fenders":    ((0.20, 0.68, 0.64), "steel"),
        "Canopy_Roof":      ((0.20, 0.38, 0.56), "glass"),
        "Side_Doors_Pods":  ((0.76, 0.80, 0.86), "pearl"),
        "Rear_Haunches":    ((0.86, 0.64, 0.22), "gold"),
        "Rear_Engine_Deck": ((0.32, 0.68, 0.48), "steel"),
        "Rear_Fascia":      ((0.24, 0.25, 0.29), "carbon"),
        "Front_Cowl":       ((0.26, 0.28, 0.34), "carbon"),
        "Roof_Frame":       ((0.18, 0.32, 0.48), "glass"),
    }
    zone_groups = {k: [] for k in zone_palette}
    for fname, deg_str, P, c in shell_patches:
        panel_lines.append(format_patch_line(fname, deg_str, P))
        zone_groups[classify_panel_zone(c)].append(fname)
    for fname, deg_str, P, _ in cowl_patches:
        panel_lines.append(format_patch_line(fname, deg_str, P))
        zone_groups["Front_Cowl"].append(fname)
    for fname, deg_str, P, _ in roof_patches:
        panel_lines.append(format_patch_line(fname, deg_str, P))
        zone_groups["Roof_Frame"].append(fname)
    for fname, deg_str, P, _ in extra_body_patches:
        panel_lines.append(format_patch_line(fname, deg_str, P))
        zone_groups["Rear_Fascia"].append(fname)

    for zname, f_list in zone_groups.items():
        rgb, mc = zone_palette[zname]
        for i in range(0, len(f_list), 100):
            b = " ".join(f_list[i : i + 100])
            panel_lines.append(f"tint {b} {rgb[0]:.3f} {rgb[1]:.3f} {rgb[2]:.3f}")
            panel_lines.append(f"matcap {b} {mc}")

    open(os.path.join(LIGER_DIR, "Liger_Body_Panels.arc"), "w").write("\n".join(panel_lines) + "\n")

    # -------------------------------------------------------------------------
    # 3. Write Vehicles/Liger/Liger_Body_CurveLoft.arc (NURBS body + 3D feature curves)
    # -------------------------------------------------------------------------
    cl_lines = list(surf_lines) + ["# 3D Feature & Crease Curve Network (from curves.json)"] + load_3d_feature_curve_lines(False)
    open(os.path.join(LIGER_DIR, "Liger_Body_CurveLoft.arc"), "w").write("\n".join(cl_lines) + "\n")

    # -------------------------------------------------------------------------
    # 4. Write Vehicles/Liger/Liger_Body_SideR.arc (+Y right-hand half-shell + 3D curves)
    # -------------------------------------------------------------------------
    sider_lines = [
        "# SolidArc native document v1",
        "# Liger +Y Right-Side Half-Shell NURBS Body + Feature Curve Network",
        "reset",
    ]
    r_shell, r_cowl, r_roof = [], [], []
    for fname, deg_str, P, c in shell_patches:
        if c[1] >= -0.005:
            sider_lines.append(format_patch_line(fname, deg_str, P))
            r_shell.append(fname)
    for fname, deg_str, P, c in cowl_patches:
        if c[1] >= -0.005:
            sider_lines.append(format_patch_line(fname, deg_str, P))
            r_cowl.append(fname)
    for fname, deg_str, P, c in roof_patches:
        if c[1] >= -0.005:
            sider_lines.append(format_patch_line(fname, deg_str, P))
            r_roof.append(fname)
    for i in range(0, len(r_shell), 100):
        b = " ".join(r_shell[i : i + 100])
        sider_lines.append(f"tint {b} 0.740 0.780 0.840")
        sider_lines.append(f"matcap {b} pearl")
    for i in range(0, len(r_cowl), 100):
        b = " ".join(r_cowl[i : i + 100])
        sider_lines.append(f"tint {b} 0.250 0.270 0.310")
        sider_lines.append(f"matcap {b} carbon")
    for i in range(0, len(r_roof), 100):
        b = " ".join(r_roof[i : i + 100])
        sider_lines.append(f"tint {b} 0.220 0.320 0.440")
        sider_lines.append(f"matcap {b} glass")
    sider_lines.extend(load_3d_feature_curve_lines(True))
    open(os.path.join(LIGER_DIR, "Liger_Body_SideR.arc"), "w").write("\n".join(sider_lines) + "\n")

    # -------------------------------------------------------------------------
    # 5. Write Vehicles/Liger/Liger_Complete.arc (Full Vehicle: Body + Aero + Wheels)
    # -------------------------------------------------------------------------
    comp_lines = list(surf_lines)
    skirt_patches = load_subsurf_patches_from_npz(os.path.join(MESH_DIR, "Aero_Side_Skirt.npz"), "Skirt", "skirt")
    aero_groups = [("Aero_Side_Skirt", skirt_patches, (0.18, 0.20, 0.24), "carbon")]

    for obj_name, prefix, rgb, mc in [
        ("Aero_Front_Lip", "FrontLip", (0.18, 0.20, 0.24), "carbon"),
        ("Aero_Front_Splitter", "Splitter", (0.16, 0.18, 0.22), "carbon"),
        ("Aero_Rear_Fender_Flare", "RearFlare", (0.18, 0.20, 0.24), "carbon"),
        ("Aero_Rear_Wing", "RearWing", (0.16, 0.18, 0.22), "carbon"),
        ("Aero_Roof_Fin", "RoofFin", (0.16, 0.18, 0.22), "carbon"),
    ]:
        p_list = load_nosub_patches_from_npz(os.path.join(MESH_DIR, f"{obj_name}.npz"), prefix, obj_name)
        aero_groups.append((obj_name, p_list, rgb, mc))

    for _, p_list, rgb, mc in aero_groups:
        fnames = []
        for fname, deg_str, P, _ in p_list:
            comp_lines.append(format_patch_line(fname, deg_str, P))
            fnames.append(fname)
        for i in range(0, len(fnames), 100):
            b = " ".join(fnames[i : i + 100])
            comp_lines.append(f"tint {b} {rgb[0]:.3f} {rgb[1]:.3f} {rgb[2]:.3f}")
            comp_lines.append(f"matcap {b} {mc}")

    # Add Front and Rear Wheels, Tyres, 10-Spoke Chrome Alloy Rims & Brake Rotors
    for obj_name, prefix in [("Wheel_Front", "WhlF"), ("Wheel_Rear", "WhlR")]:
        G_right = np.load(os.path.join(MESH_DIR, f"{obj_name}.npz"))["G_right"].astype(np.float64)
        G_left = G_right.copy()[::-1, :, :]
        G_left[:, :, 1] *= -1.0
        cx = 0.5 * (G_right[:, :, 0].min() + G_right[:, :, 0].max())
        cz = 0.5 * (G_right[:, :, 2].min() + G_right[:, :, 2].max())

        tyre_figs, rim_figs, rotor_figs = [], [], []
        for side_tag, G, y_sign in [("L", G_left, -1.0), ("R", G_right, 1.0)]:
            for q in range(4):
                u0, u1 = q * 16, (q + 1) * 16 + 1
                P_tyre = G[u0:u1, 4:29, :]
                pts_str = " ".join(f"({P_tyre[u,v,0]:.5f},{P_tyre[u,v,1]:.5f},{P_tyre[u,v,2]:.5f})" for u in range(17) for v in range(25))
                tname = f"{prefix}_{side_tag}_Tyre_Q{q}"
                comp_lines.append(f"patch 17 25 {pts_str} --degree=2 --name={tname}")
                tyre_figs.append(tname)

                # Outer Rim Barrel: j = 28..32 (radially inward, normal points outward +y_sign)
                P_rim = G[u0:u1, 28:33, :]
                pts_str = " ".join(f"({P_rim[u,v,0]:.5f},{P_rim[u,v,1]:.5f},{P_rim[u,v,2]:.5f})" for u in range(17) for v in range(5))
                rname = f"{prefix}_{side_tag}_Rim_Q{q}"
                comp_lines.append(f"patch 17 5 {pts_str} --degree=2 --name={rname}")
                rim_figs.append(rname)

                # Brake Rotor / Backing Dish: 3-ring dish from j=29 (rim lip) inward to j=32..33 (recessed 4.5 cm)
                # Sealing the gap between rim lip and rotor so inner tyre back-faces are completely occluded!
                P_back = np.empty((17, 3, 3), dtype=np.float64)
                P_back[:, 0, :] = G[u0:u1, 29, :]
                P_back[:, 0, 1] -= y_sign * 0.008
                P_back[:, 1, :] = G[u0:u1, 32, :]
                P_back[:, 1, 1] -= y_sign * 0.045
                P_back[:, 2, :] = 0.98 * G[u0:u1, 33, :] + 0.02 * G[u0:u1, 32, :]
                P_back[:, 2, 1] -= y_sign * 0.045
                pts_str = " ".join(f"({P_back[u,v,0]:.5f},{P_back[u,v,1]:.5f},{P_back[u,v,2]:.5f})" for u in range(17) for v in range(3))
                bname = f"{prefix}_{side_tag}_Back_Q{q}"
                comp_lines.append(f"patch 17 3 {pts_str} --degree=2 --name={bname}")
                rotor_figs.append(bname)

            y_hub = y_sign * (abs(G[0, 32, 1]) + 0.012)
            y_rim = y_sign * (abs(G[0, 29, 1]) - 0.005)
            r_hub, r_rim = 0.068, 0.282
            for s in range(10):
                ang = s * (2.0 * math.pi / 10.0) + (0.12 if s % 2 == 1 else -0.12)
                ang_outer = s * (2.0 * math.pi / 10.0)
                w_in, w_out = 0.035, 0.022
                nx_in, nz_in = -math.sin(ang), math.cos(ang)
                nx_out, nz_out = -math.sin(ang_outer), math.cos(ang_outer)
                p0 = (cx + r_hub * math.cos(ang) - w_in * nx_in, y_hub, cz + r_hub * math.sin(ang) - w_in * nz_in)
                p1 = (cx + r_hub * math.cos(ang) + w_in * nx_in, y_hub, cz + r_hub * math.sin(ang) + w_in * nz_in)
                p2 = (cx + r_rim * math.cos(ang_outer) + w_out * nx_out, y_rim, cz + r_rim * math.sin(ang_outer) + w_out * nz_out)
                p3 = (cx + r_rim * math.cos(ang_outer) - w_out * nx_out, y_rim, cz + r_rim * math.sin(ang_outer) - w_out * nz_out)
                S_pts = [p0, p1, p3, p2] if y_sign < 0 else [p0, p3, p1, p2]
                pts_str = " ".join(f"({q[0]:.5f},{q[1]:.5f},{q[2]:.5f})" for q in S_pts)
                sname = f"{prefix}_{side_tag}_Spoke_{s:02d}"
                comp_lines.append(f"patch 2 2 {pts_str} --name={sname}")
                rim_figs.append(sname)

        for figs, rgb, mc in [
            (tyre_figs, (0.14, 0.15, 0.17), "rubber"),
            (rim_figs, (0.84, 0.87, 0.92), "chrome"),
            (rotor_figs, (0.22, 0.24, 0.28), "steel"),
        ]:
            b = " ".join(figs)
            comp_lines.append(f"tint {b} {rgb[0]:.3f} {rgb[1]:.3f} {rgb[2]:.3f}")
            comp_lines.append(f"matcap {b} {mc}")

    open(os.path.join(LIGER_DIR, "Liger_Complete.arc"), "w").write("\n".join(comp_lines) + "\n")
    print(f"Generated all 5 Liger .arc files in {time.time() - t0:.2f}s")


if __name__ == "__main__":
    main()

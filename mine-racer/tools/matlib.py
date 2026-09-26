"""Shared materials + geometry helpers for the Mine Racer asset builds.

Coordinate system: Y up, Z forward (matches glTF / three.js / Unreal import).
Units: meters.
"""
import numpy as np
import trimesh
from trimesh.visual.material import PBRMaterial


# ---------------------------------------------------------------- materials
def M(name, color, metallic=0.0, roughness=0.8, emissive=(0, 0, 0), ei=1.0):
    """PBR material factory. color/emissive are 0-1 RGB tuples."""
    mat = PBRMaterial(
        name=name,
        baseColorFactor=[color[0], color[1], color[2], 1.0],
        metallicFactor=metallic,
        roughnessFactor=roughness,
        emissiveFactor=[emissive[0] * ei, emissive[1] * ei, emissive[2] * ei],
    )
    return mat


MAT = {
    # vehicle
    "body":      M("BodyPaint_SafetyYellow", (0.93, 0.72, 0.09), 0.15, 0.45),
    "body_dark": M("BodyPaint_Charcoal", (0.12, 0.12, 0.13), 0.2, 0.6),
    "plastic":   M("Plastic_Black", (0.05, 0.05, 0.055), 0.0, 0.9),
    "steel":     M("Steel_Galvanized", (0.55, 0.57, 0.6), 0.9, 0.35),
    "steel_dark":M("Steel_Dark", (0.2, 0.21, 0.23), 0.85, 0.5),
    "rubber":    M("Rubber_Tire", (0.03, 0.03, 0.032), 0.0, 0.95),
    "chrome":    M("Chrome", (0.8, 0.82, 0.85), 1.0, 0.15),
    "glass":     M("Glass_Smoked", (0.05, 0.07, 0.08), 0.9, 0.1),
    "seat":      M("Fabric_Neoprene", (0.1, 0.12, 0.14), 0.0, 0.95),
    "headlight": M("Headlight_On", (1.0, 0.97, 0.85), 0.0, 0.3, emissive=(1.0, 0.95, 0.8), ei=3.2),
    "taillight": M("Taillight_On", (1.0, 0.08, 0.05), 0.0, 0.3, emissive=(1.0, 0.05, 0.03), ei=2.6),
    "amber":     M("Strobe_Amber", (1.0, 0.55, 0.1), 0.0, 0.3, emissive=(1.0, 0.5, 0.08), ei=2.8),
    "screen":    M("Dash_Screen", (0.05, 0.3, 0.35), 0.0, 0.2, emissive=(0.1, 0.7, 0.8), ei=1.6),
    "hazard":    M("Hazard_Plate", (0.85, 0.65, 0.05), 0.1, 0.6),
    # mine structure
    "rock":      M("Rock_Granite", (0.16, 0.13, 0.11), 0.0, 0.98),
    "rock_dark": M("Rock_Deep", (0.09, 0.075, 0.07), 0.0, 0.98),
    "concrete":  M("Concrete_Road", (0.30, 0.29, 0.28), 0.0, 0.85),
    "concrete_d":M("Concrete_Groove", (0.16, 0.155, 0.15), 0.0, 0.9),
    "timber":    M("Timber_Oak", (0.34, 0.22, 0.11), 0.0, 0.9),
    "timber_d":  M("Timber_Dark", (0.22, 0.14, 0.07), 0.0, 0.95),
    "rail":      M("Rail_Steel_Worn", (0.35, 0.33, 0.3), 0.9, 0.45),
    "lamp":      M("Lamp_Housing", (0.08, 0.08, 0.085), 0.6, 0.6),
    "warm":      M("Lamp_Warm_On", (1.0, 0.75, 0.42), 0.0, 0.4, emissive=(1.0, 0.68, 0.32), ei=3.0),
    "cool":      M("Flood_Cool_On", (0.85, 0.93, 1.0), 0.0, 0.3, emissive=(0.8, 0.9, 1.0), ei=3.0),
    "red":       M("Signal_Red_On", (1.0, 0.1, 0.08), 0.0, 0.3, emissive=(1.0, 0.06, 0.05), ei=3.4),
    "green":     M("Signal_Green_On", (0.1, 1.0, 0.25), 0.0, 0.3, emissive=(0.08, 1.0, 0.2), ei=3.0),
    "ore":       M("Ore_Coal", (0.07, 0.07, 0.075), 0.05, 0.95),
    "barrel_b":  M("Barrel_Blue", (0.12, 0.2, 0.35), 0.3, 0.6),
    "barrel_r":  M("Barrel_Red", (0.5, 0.1, 0.08), 0.3, 0.6),
    "crate":     M("Crate_Wood", (0.4, 0.28, 0.14), 0.0, 0.9),
    "sign":      M("Sign_Retro", (0.75, 0.6, 0.1), 0.2, 0.6),
    "sign_glow": M("Sign_Glow", (0.9, 0.75, 0.2), 0.0, 0.4, emissive=(1.0, 0.8, 0.25), ei=1.8),
    "banner":    M("Banner_Checker", (0.9, 0.88, 0.85), 0.0, 0.8),
}


def set_mat(mesh, name):
    """Attach a PBR material + record its name in metadata."""
    mesh.visual = trimesh.visual.TextureVisuals(material=MAT[name])
    mesh.metadata["mat"] = name
    return mesh


# ---------------------------------------------------------------- transforms
def T(pos=(0, 0, 0), rot=(0, 0, 0)):
    """4x4 transform from position + XYZ euler radians (static, sxyz)."""
    mat = trimesh.transformations.euler_matrix(rot[0], rot[1], rot[2], axes="sxyz")
    mat[:3, 3] = pos
    return mat


def place(mesh, pos=(0, 0, 0), rot=(0, 0, 0)):
    m = mesh.copy()
    m.apply_transform(T(pos, rot))
    return m


# ---------------------------------------------------------------- primitives
def box(sx, sy, sz, pos=(0, 0, 0), rot=(0, 0, 0), mat="plastic"):
    m = trimesh.creation.box((sx, sy, sz))
    set_mat(m, mat)
    return place(m, pos, rot)


def cyl(r, h, seg=20, pos=(0, 0, 0), rot=(0, 0, 0), mat="steel", axis="z", capped=True):
    """Cylinder along given axis (default z)."""
    m = trimesh.creation.cylinder(radius=r, height=h, sections=seg)
    if axis == "y":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    elif axis == "x":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
    set_mat(m, mat)
    return place(m, pos, rot)


def tube(r_outer, r_inner, h, seg=20, pos=(0, 0, 0), rot=(0, 0, 0), mat="steel", axis="z"):
    m = trimesh.creation.annulus(r_min=r_inner, r_max=r_outer, height=h, sections=seg)
    if axis == "y":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    elif axis == "x":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
    set_mat(m, mat)
    return place(m, pos, rot)


def rod(a, b, r=0.03, seg=10, mat="steel"):
    """Cylinder from point a to point b."""
    a = np.array(a, float)
    b = np.array(b, float)
    v = b - a
    ln = np.linalg.norm(v)
    if ln < 1e-9:
        return None
    m = trimesh.creation.cylinder(radius=r, height=ln, sections=seg)
    R = trimesh.geometry.align_vectors([0, 0, 1], v / ln)
    m.apply_transform(R)
    m.apply_translation((a + b) / 2.0)
    set_mat(m, mat)
    return m


def sphere(r, pos=(0, 0, 0), mat="steel", seg=10):
    m = trimesh.creation.icosphere(subdivisions=2, radius=r)
    set_mat(m, mat)
    return place(m, pos)


def torus(R, r, pos=(0, 0, 0), rot=(0, 0, 0), mat="plastic", seg=24):
    m = trimesh.creation.torus(major_radius=R, minor_radius=r,
                               major_sections=seg, minor_sections=10)
    set_mat(m, mat)
    return place(m, pos, rot)


# ---------------------------------------------------------------- misc
def merge_by_material(meshes):
    """Concatenate meshes grouped by their material name -> {name: Trimesh}."""
    out = {}
    for m in meshes:
        if m is None or len(m.faces) == 0:
            continue
        name = m.metadata.get("mat", "default")
        out.setdefault(name, []).append(m)
    return {k: trimesh.util.concatenate(v) for k, v in out.items()}


def mesh_color(m):
    """Pull an RGB face color out of a mesh's material (for matplotlib previews)."""
    v = m.visual
    try:
        c = np.array(v.material.baseColorFactor[:3], float)
        e = np.array(getattr(v.material, "emissiveFactor", [0, 0, 0]) or [0, 0, 0], float)
        c = np.clip(c + e * 0.7, 0, 1)
        return c
    except Exception:
        try:
            c = np.array(v.main_color, float) / 255.0
            return c if c.shape == (3,) else np.array([0.6, 0.6, 0.6])
        except Exception:
            return np.array([0.6, 0.6, 0.6])


def mat_of(m):
    return getattr(m.visual, "name", "default")

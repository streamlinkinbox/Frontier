"""Matplotlib 3D preview renders — internal QA for the GLB assets."""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import trimesh
from matlib import mesh_color


def render(scene_or_meshes, path, elev=18, azim=-55, dist=None, size=(1200, 900),
           clip_box=None, bg="#141210"):
    meshes = (scene_or_meshes.dump() if isinstance(scene_or_meshes, trimesh.Scene)
              else scene_or_meshes)
    fig = plt.figure(figsize=(size[0] / 100, size[1] / 100), dpi=100)
    ax = fig.add_subplot(111, projection="3d")
    ax.set_facecolor(bg)
    fig.patch.set_facecolor(bg)
    allv = []
    light = np.array([0.4, 0.75, 0.55]); light /= np.linalg.norm(light)
    for m in meshes:
        if clip_box is not None:
            lo, hi = clip_box
            keep = np.all((m.vertices >= lo) & (m.vertices <= hi), axis=1)
            if not keep.any():
                continue
        col = mesh_color(m)
        tri = m.vertices[m.faces]
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        n /= (np.linalg.norm(n, axis=1, keepdims=True) + 1e-9)
        lam = np.clip(n @ light, 0, 1) * 0.55 + 0.45
        cols = np.clip(col[None, :] * lam[:, None], 0, 1)
        pc = Poly3DCollection(tri, facecolors=cols, edgecolor="none")
        ax.add_collection3d(pc)
        allv.append(m.vertices)
    if not allv:
        print("nothing to render"); return
    V = np.vstack(allv)
    lo, hi = V.min(0), V.max(0)
    if clip_box is not None:
        lo, hi = np.array(clip_box[0]), np.array(clip_box[1])
    c = (lo + hi) / 2
    r = max(hi - lo) / 2 * 1.05
    ax.set_xlim(c[0] - r, c[0] + r)
    ax.set_ylim(c[1] - r, c[1] + r)
    ax.set_zlim(c[2] - r, c[2] + r)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=elev, azim=azim)
    if dist:
        ax.dist = dist
    ax.set_axis_off()
    ax.set_proj_type("persp")
    plt.tight_layout(pad=0)
    plt.savefig(path, facecolor=bg)
    plt.close()
    print("wrote", path)


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "boxcar"
    if which == "boxcar":
        sc = trimesh.Scene()
        import boxcar
        for mat_name, mesh in __import__("matlib").merge_by_material(boxcar.build_body()).items():
            sc.add_geometry(mesh)
        for tag, x, z in [("fl", 0.72, 1.08), ("fr", -0.72, 1.08),
                          ("rl", 0.72, -1.08), ("rr", -0.72, -1.08)]:
            from matlib import T
            for mn, mesh in __import__("matlib").merge_by_material(boxcar.build_wheel()).items():
                sc.add_geometry(mesh, transform=T((x, 0.30, z)))
        render(sc, "../assets/preview/boxcar_34_front.png", elev=14, azim=-55)
        render(sc, "../assets/preview/boxcar_side.png", elev=8, azim=-90)
        render(sc, "../assets/preview/boxcar_rear34.png", elev=16, azim=125)

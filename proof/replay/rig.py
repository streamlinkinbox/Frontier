"""Decode the pinned triangle rig and the actual captured instance/receiver buffers."""

import numpy as np
from embreex.mesh_construction import TriangleMesh
from embreex.rtcore_scene import EmbreeScene


def load(folder, bundle):
    read = lambda name: np.fromfile(folder / (name + ".f32"), "<f4")
    pos = read("uGPosRough").reshape(-1, 4)[:, :3].copy()
    nm = read("uGNormMat").reshape(-1, 4)
    norm = nm[:, :3].copy()
    norm /= np.maximum(1e-12, np.linalg.norm(norm, axis=1, keepdims=True))
    mask = nm[:, 3] > 0
    trans = (
        read("uInstances")
        .reshape(-1, 4)[:90]
        .reshape(10, 9, 4)[:, :4, :]
        .transpose(0, 2, 1)
    )
    data = bundle.read_bytes()
    meta = np.frombuffer(data, "<u4", offset=20, count=16).reshape(4, 4)
    tri = np.frombuffer(data, "<f4", offset=84 + 5930 * 32, count=9240 * 16).reshape(
        -1, 16
    )
    world = []
    lamps = []
    rgb = {12: [2.2, 2.6, 3.2], 13: [7, 7.6, 8.8], 14: [6.5, 0.4, 0.3]}
    for m, M in zip([0, 1, 1, 1, 1, 2, 2, 2, 2, 3], trans):
        start, count = meta[m, 2:]
        t = tri[start : start + count]
        verts = (
            np.stack([t[:, :3], t[:, :3] + t[:, 4:7], t[:, :3] + t[:, 8:11]], axis=1)
            @ M[:3, :3].T
            + M[:3, 3]
        )
        world.append(verts[t[:, 3] != 7])
        if m == 0:
            lamps = [
                (v, np.array(rgb[int(id)], dtype="f4"))
                for v, id in zip(verts, t[:, 3])
                if id in rgb
            ]
    scene = EmbreeScene()
    mesh = TriangleMesh(scene, np.concatenate(world).astype("f4"))
    return scene, mesh, pos, norm, mask, lamps

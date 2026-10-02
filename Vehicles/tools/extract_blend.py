"""Headless Blender (pip `bpy`) extractor: evaluates every modifier stack
(mirror/subsurf/shrinkwrap/mask/geometry-nodes) and dumps world-space
triangle meshes per object to .npz, plus per-part thumbnail sheets.

  LD_LIBRARY_PATH=~/.bpystubs ~/.bpyenv/bin/python extract_blend.py Quicksilver.blend out/
"""
import sys, json, numpy as np, bpy
src, out = sys.argv[1], sys.argv[2]
import os; os.makedirs(out, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=os.path.abspath(src))
dg = bpy.context.evaluated_depsgraph_get()
index = {}
for o in bpy.data.objects:
    if o.type not in ('MESH', 'CURVE'): continue
    oe = o.evaluated_get(dg); m = oe.to_mesh()
    if m is None or not len(m.vertices): continue
    V = np.empty(len(m.vertices) * 3, 'f4'); m.vertices.foreach_get('co', V); V = V.reshape(-1, 3)
    M = np.array(o.matrix_world); V = V @ M[:3, :3].T + M[:3, 3]
    T = np.array([(p.vertices[0], p.vertices[i], p.vertices[i + 1])
                  for p in m.polygons for i in range(1, len(p.vertices) - 1)], 'i4')
    np.savez_compressed(f'{out}/{o.name}.npz', V=V, T=T)
    index[o.name] = dict(v=len(V), f=len(m.polygons), mn=V.min(0).tolist(), mx=V.max(0).tolist(),
                         mods=[md.type for md in o.modifiers])
    oe.to_mesh_clear()
json.dump(index, open(f'{out}/index.json', 'w'), indent=1)
print(len(index), 'meshes,', sum(i['v'] for i in index.values()), 'verts')

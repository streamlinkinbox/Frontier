"""Evaluate the in-scope body parts (subsurf capped at 2) and cache them as npz in Vehicles/<Car>/mesh/.
   LD_LIBRARY_PATH=~/.bpystubs ~/.bpyenv/bin/python Vehicles/tools/export_mesh.py Liger"""
import sys, os, numpy as np, bpy
sys.path.insert(0, os.path.dirname(__file__)); from partnames import CARS, IN_SCOPE
CAR = sys.argv[1]; ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = f'{ROOT}/Vehicles/{CAR}/mesh'; os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=f'{ROOT}/{CAR}.blend')
for o in bpy.data.objects:
    for md in o.modifiers:
        if md.type == 'SUBSURF': md.levels = min(md.levels, 2)
        if md.type == 'MASK': md.show_viewport = False
dg = bpy.context.evaluated_depsgraph_get()
for old, (new, cls) in CARS[CAR].items():
    if cls not in IN_SCOPE or old not in bpy.data.objects: continue
    o = bpy.data.objects[old]; me = o.evaluated_get(dg).to_mesh()
    if me is None or not len(me.polygons): continue
    n = len(me.vertices); V = np.empty(n * 3, 'f4'); me.vertices.foreach_get('co', V); V = V.reshape(-1, 3)
    M = np.array(o.matrix_world); V = (V @ M[:3, :3].T + M[:3, 3]).astype('f4')
    T = np.array([(p.vertices[0], p.vertices[i], p.vertices[i + 1]) for p in me.polygons for i in range(1, len(p.vertices) - 1)], 'i4')
    np.savez_compressed(f'{OUT}/{new}.npz', V=V, T=T); print(new, n, len(T))

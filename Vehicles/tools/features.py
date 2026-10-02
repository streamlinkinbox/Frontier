"""Rename parts, isolate the exterior body, and extract CAD feature lines.

Run headless:
  cd Frontier && LD_LIBRARY_PATH=~/.bpystubs ~/.bpyenv/bin/python Vehicles/tools/features.py Quicksilver

Per vehicle it writes Vehicles/<Car>/
  <Car>_named.blend         objects renamed (written by bpy 5.0.1 - keep the original too)
  rename_in_blender.py      same renames as a script you can run inside Blender 5.2
  features.json             per part: 3D polylines for BOUNDARY / CREASE / SHARP / SEAM
                            edges (world coords), per view: 2D silhouette outlines
  sheet_<view>.png          layered sheet: tinted visible parts, black silhouette,
                            blue boundary, red crease, magenta sharp, green seam
"""
import sys, os, json, time
import numpy as np, bpy
sys.path.insert(0, os.path.dirname(__file__))
from partnames import CARS, IN_SCOPE

CAR = sys.argv[1]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = f'{ROOT}/Vehicles/{CAR}'; os.makedirs(OUT, exist_ok=True)
MAP = CARS[CAR]
NEW2CLS = {v[0]: v[1] for v in MAP.values()}
NEW2OLD = {v[0]: k for k, v in MAP.items()}
SUBSURF_CAP = 2          # evaluation level (3 is overkill for drawings)
PX = 2.0                 # pixels per Blender unit (cm)

# ---------------------------------------------------------------- open + rename
bpy.ops.wm.open_mainfile(filepath=f'{ROOT}/{CAR}.blend')
for old, (new, cls) in MAP.items():
    if old in bpy.data.objects:
        bpy.data.objects[old].name = new
with open(f'{OUT}/rename_in_blender.py', 'w') as f:
    f.write('import bpy\nM = ' + json.dumps({k: v[0] for k, v in MAP.items()}, indent=1) +
            '\nfor o, n in M.items():\n    if o in bpy.data.objects: bpy.data.objects[o].name = n\n')
try:
    bpy.ops.wm.save_as_mainfile(filepath=f'{OUT}/{CAR}_named.blend', copy=True, compress=True)
except Exception as e:
    print('save failed:', e)

# ---------------------------------------------------------------- evaluate body parts
for o in bpy.data.objects:
    for md in o.modifiers:
        if md.type == 'SUBSURF': md.levels = min(md.levels, SUBSURF_CAP)
        if md.type == 'MASK': md.show_viewport = False
dg = bpy.context.evaluated_depsgraph_get()

def chain(segs):
    """edge list [(a,b),...] -> list of vertex-index polylines"""
    from collections import defaultdict
    adj = defaultdict(list)
    for a, b in segs: adj[a].append(b); adj[b].append(a)
    used = set(); out = []
    def walk(start, nxt):
        line = [start, nxt]; used.add((min(start, nxt), max(start, nxt)))
        while True:
            cur, prev = line[-1], line[-2]
            cand = [n for n in adj[cur] if n != prev and (min(cur, n), max(cur, n)) not in used]
            if len(cand) != 1 or len(adj[cur]) != 2: return line
            used.add((min(cur, cand[0]), max(cur, cand[0]))); line.append(cand[0])
            if cand[0] == start: return line
    for s in list(adj):                       # start at endpoints / junctions first
        if len(adj[s]) != 2:
            for n in adj[s]:
                if (min(s, n), max(s, n)) not in used: out.append(walk(s, n))
    for s in list(adj):                       # closed loops
        for n in adj[s]:
            if (min(s, n), max(s, n)) not in used: out.append(walk(s, n))
    return out

parts = {}
for o in bpy.data.objects:
    cls = NEW2CLS.get(o.name, '?')
    if o.type != 'MESH' or cls not in IN_SCOPE: continue
    me = o.evaluated_get(dg).to_mesh()
    if me is None or not len(me.polygons): continue
    n = len(me.vertices); V = np.empty(n * 3, 'f4'); me.vertices.foreach_get('co', V); V = V.reshape(-1, 3)
    M = np.array(o.matrix_world, 'f8'); V = (V @ M[:3, :3].T + M[:3, 3]).astype('f4')
    E = np.empty(len(me.edges) * 2, 'i4'); me.edges.foreach_get('vertices', E); E = E.reshape(-1, 2)
    # triangles + edge face-count (boundary detection)
    tris = []; ecount = {}
    for p in me.polygons:
        vs = list(p.vertices)
        for i in range(1, len(vs) - 1): tris.append((vs[0], vs[i], vs[i + 1]))
        for i in range(len(vs)):
            k = (min(vs[i], vs[(i + 1) % len(vs)]), max(vs[i], vs[(i + 1) % len(vs)]))
            ecount[k] = ecount.get(k, 0) + 1
    T = np.array(tris, 'i4')
    boundary = [k for k, c in ecount.items() if c == 1]
    def attr(name, thresh=0.0):
        a = me.attributes.get(name)
        if a is None: return []
        vals = np.empty(len(a.data), 'f4' if a.data_type != 'BOOLEAN' else 'b')
        a.data.foreach_get('value', vals)
        return [tuple(e) for e in E[vals > thresh]]
    feats = {'BOUNDARY': boundary, 'CREASE': attr('crease_edge', 0.3),
             'SHARP': attr('sharp_edge'), 'SEAM': attr('uv_seam')}
    lines = {k: [V[np.array(l)].tolist() for l in chain(v)] for k, v in feats.items()}
    parts[o.name] = dict(cls=cls, V=V, T=T, lines=lines, orig=NEW2OLD[o.name])
    print(f'{o.name:28s} v={n:6d} tris={len(T):6d} ' + ' '.join(f'{k}={len(v)}' for k, v in lines.items()))
    o.evaluated_get(dg).to_mesh_clear()

# ---------------------------------------------------------------- views
VIEWS = {  # name: (u, v, depth)  depth smaller = closer to viewer
    'side':   lambda P: (P[:, 0], P[:, 2], P[:, 1]),
    'top':    lambda P: (P[:, 0], P[:, 1], -P[:, 2]),
    'bottom': lambda P: (P[:, 0], -P[:, 1], P[:, 2]),
    'front':  lambda P: (-P[:, 1], P[:, 2], -P[:, 0]),
    'rear':   lambda P: (P[:, 1], P[:, 2], P[:, 0]),
}
names = list(parts)
allV = np.vstack([parts[n]['V'] for n in names])

def rasterize(view):
    u, v, d = VIEWS[view](allV)
    u0, v0 = u.min() - 5, v.min() - 5
    W, H = int((u.max() + 5 - u0) * PX) + 1, int((v.max() + 5 - v0) * PX) + 1
    zb = np.full((H, W), np.inf, 'f4'); idb = np.full((H, W), -1, 'i2')
    for pi, n in enumerate(names):
        P = parts[n]['V']; pu, pv, pd = VIEWS[view](P)
        px = (pu - u0) * PX; py = (pv - v0) * PX
        for a, b, c in parts[n]['T']:
            xs = (px[a], px[b], px[c]); ys = (py[a], py[b], py[c])
            x1, x2 = int(min(xs)), int(max(xs)) + 1; y1, y2 = int(min(ys)), int(max(ys)) + 1
            if x2 - x1 > 400 or y2 - y1 > 400: continue
            gx, gy = np.meshgrid(np.arange(x1, x2) + 0.5, np.arange(y1, y2) + 0.5)
            det = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
            if abs(det) < 1e-9: continue
            l1 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / det
            l2 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / det
            l3 = 1 - l1 - l2
            m = (l1 >= -1e-4) & (l2 >= -1e-4) & (l3 >= -1e-4)
            if not m.any(): continue
            z = l1 * pd[a] + l2 * pd[b] + l3 * pd[c]
            sub = zb[y1:y2, x1:x2]; upd = m & (z < sub)
            sub[upd] = z[upd]; idb[y1:y2, x1:x2][upd] = pi
    return zb, idb, (u0, v0, W, H)

import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
STYLE = {'BOUNDARY': ('#1f5fff', 1.0), 'CREASE': ('#e00000', 0.9), 'SHARP': ('#ff00c8', 1.3), 'SEAM': ('#00a000', 1.0)}
cmap = plt.get_cmap('tab20', max(len(names), 1))
result = {'car': CAR, 'units': 'cm (Blender units)', 'axes': '+X front, +Z up, Y across',
          'parts': {n: dict(cls=parts[n]['cls'], blender_name=parts[n]['orig'],
                            bbox=[parts[n]['V'].min(0).tolist(), parts[n]['V'].max(0).tolist()],
                            lines=parts[n]['lines']) for n in names},
          'views': {}}
for view in VIEWS:
    t = time.time(); zb, idb, (u0, v0, W, H) = rasterize(view)
    fig, ax = plt.subplots(figsize=(W / 60, H / 60 + 1.4), dpi=160)
    ext = (u0, u0 + W / PX, v0, v0 + H / PX)
    rgb = np.ones((H, W, 4), 'f4'); vis = idb >= 0
    rgb[vis] = np.array([cmap(i) for i in range(len(names))])[idb[vis]] * [1, 1, 1, 0.35] + [0, 0, 0, 0]
    ax.imshow(rgb, origin='lower', extent=ext, interpolation='nearest')
    # silhouette of the whole body
    cs = ax.contour(np.arange(W) / PX + u0, np.arange(H) / PX + v0, vis.astype('f4'), [0.5], colors='k', linewidths=1.6)
    sil = [p.tolist() for c in cs.allsegs[0] for p in [np.array(c)] if len(p) > 20]
    # feature lines with hidden-line test
    for pi, n in enumerate(names):
        for kind, ls in parts[n]['lines'].items():
            col, lw = STYLE[kind]
            for L in ls:
                P = np.array(L, 'f4'); pu, pv, pd = VIEWS[view](P)
                ix = np.clip(((pu - u0) * PX).astype(int), 0, W - 1); iy = np.clip(((pv - v0) * PX).astype(int), 0, H - 1)
                visible = pd <= zb[iy, ix] + 1.5
                # draw visible runs solid, hidden faint
                start = 0
                for i in range(1, len(P) + 1):
                    if i == len(P) or visible[i] != visible[start]:
                        seg = slice(max(start - 1, 0), i + 1)
                        ax.plot(pu[seg], pv[seg], color=col, lw=lw if visible[start] else 0.5,
                                alpha=1 if visible[start] else 0.18, solid_capstyle='round')
                        start = i
    ax.set_aspect('equal'); ax.set_xlim(ext[0], ext[1]); ax.set_ylim(ext[2], ext[3])
    ax.grid(alpha=0.25, lw=0.5); ax.set_xticks(np.arange(np.ceil(ext[0] / 50) * 50, ext[1], 50)); ax.set_yticks(np.arange(np.ceil(ext[2] / 50) * 50, ext[3], 50))
    ax.set_title(f'{CAR} - {view.upper()} view  (exterior body only; cm)')
    h = [plt.Rectangle((0, 0), 1, 1, color=cmap(i), alpha=0.5) for i in range(len(names))] + \
        [Line2D([], [], color='k', lw=1.6)] + [Line2D([], [], color=c, lw=2) for c, _ in STYLE.values()]
    ax.legend(h, names + ['silhouette'] + [k.lower() + ' edge' for k in STYLE], fontsize=7, ncol=4,
              loc='upper center', bbox_to_anchor=(0.5, -0.06))
    fig.savefig(f'{OUT}/sheet_{view}.png', bbox_inches='tight'); plt.close(fig)
    result['views'][view] = dict(silhouette=sil, size=[W, H])
    print(f'{view}: {W}x{H} px, {len(sil)} silhouette loops, {time.time() - t:.1f}s')
json.dump(result, open(f'{OUT}/features.json', 'w'))
print('wrote', OUT)

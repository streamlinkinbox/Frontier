#!/usr/bin/env python3
"""Tyre tread: CAD model in SolidArc + all-quad mesh, from ONE pattern definition.

Pattern = one pitch of the +Y half of the tread, defined on a WARPED GRID: rows run across the tread
(y, mm from the centreline), columns run around the tyre (x, mm).  Every row may place its column
nodes anywhere, so lug edges can be slanted, curved and tapered; every cell carries a height
(0 = groove floor, D = block top, D-s = sipe floor).  No height-maps / textures: the geometry is the
traced outline of the blocks.

  half tile  --mirror(y) + shift(pitch/2)-->  full directional pitch  --radial x N-->  tread band

Outputs (in <out>/):
  Tread.arc         SolidArc journal: crown+rib revolve, each block = polyline outline extruded radially,
                    mirrored half, `radial` array of N pitches  (CAD model)
  Tread_quads.obj   the same pattern as a welded all-quad mesh (f with 4 indices), checked
  tread_sheet.png   plan of the tile + quad wire + 3D preview of the mesh
"""
import sys, math, json, numpy as np
from pathlib import Path
from collections import defaultdict

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else 'Vehicles/Tread'); OUT.mkdir(parents=True, exist_ok=True)

# ───────────────────────── tyre & pattern parameters (mm) ─────────────────────────
R0      = 330.0     # groove-floor radius at the centreline
W       = 95.0      # half tread width
CROWN   = 6.0       # radial drop of the floor at the shoulder (crown bulge)
D       = 8.5       # tread depth (block height)
SIPE    = 4.0       # sipe depth below the block top
N_PITCH = 68        # pitches around; pitch = 2πR0/N
P       = 2 * math.pi * R0 / N_PITCH

def shear(y):            # diagonal groove centre-line: curved V (steeper near the centre, flatter at shoulder)
    t = y / W
    return 0.62 * y - 22.0 * t * t

def groove_w(y):         # lateral groove widens toward the shoulder (reference tyres do this)
    return 4.0 + 4.5 * (y / W)

# rows: y position + a "layout" key telling how the columns are placed on this row
ROWS = [(0.0, 'uniform'), (4.0, 'uniform'), (8.0, 'rib'), (12.0, 'lug'), (20.0, 'lug'), (28.0, 'lug'), (36.0, 'lug'),
        (44.0, 'lug'), (49.0, 'lug'), (55.0, 'lug'), (63.0, 'lug'), (71.0, 'lug'), (79.0, 'lug'), (87.0, 'lug'), (95.0, 'lug')]
NC = 8   # columns per pitch (even, so the half-pitch stagger maps centreline nodes onto nodes)

def columns(y, layout):
    """x of the NC column nodes on a row (node NC == node 0 of the next pitch)."""
    if layout == 'uniform':
        return [-P / 2 + k * P / NC for k in range(NC)]
    g = groove_w(y) if layout == 'lug' else 5.0
    lug = P - g
    # groove | lug split into 7 columns: 2 lead, sipe(1.6), 2, notch col, 1 trail
    if layout == 'lug':
        widths = [g, lug * 0.19, lug * 0.19, 1.6, lug * 0.19, lug * 0.19, (lug - 1.6) * 0.24 * 0.5, 0]
        widths[-1] = P - sum(widths[:-1])
    else:
        widths = [g] + [lug / (NC - 1)] * (NC - 1)
    xs, x = [], -P / 2
    for w in widths: xs.append(x); x += w
    return [xx + shear(y) for xx in xs]

def cell_height(i, j):
    """height of cell (column i, row j) — the design of the tread."""
    y0, l0 = ROWS[j]; y1, l1 = ROWS[j + 1]
    if l1 in ('uniform', 'rib'):                        # centre rib band 0..8: continuous, with a small notch
        return 0.0 if (l1 == 'rib' and i == 0) else D   # notch where the lateral groove meets the rib
    if y0 < 12 and l1 == 'lug':                          # inner longitudinal groove 8..12
        return 0.0
    if i == 0: return 0.0                                # lateral (diagonal) groove
    if 49 <= y0 < 55: return 0.0                         # outer longitudinal groove 49..55
    if i == 3 and y0 >= 55: return D - SIPE              # sipe through the shoulder block
    if i == 6 and 36 <= y0 < 49: return 0.0              # notch in the inner lug, opening into the groove
    if i == 6 and y0 >= 87: return 0.0                   # shoulder notch
    if i == 3 and 28 <= y0 < 36: return D - SIPE         # short sipe in the inner lug
    return D

NR = len(ROWS)
NODE = [[(x, ROWS[j][0]) for x in columns(ROWS[j][0], ROWS[j][1])] for j in range(NR)]   # NODE[j][i] -> (x, y)
H = [[cell_height(i, j) for i in range(NC)] for j in range(NR - 1)]                     # H[j][i]

# ───────────────────────── mapping onto the wheel (axis = Y) ─────────────────────────
def floor_r(y): return R0 - CROWN * (y / W) ** 2
def on_wheel(x, y, h):
    r = floor_r(y) + h; a = x / R0
    return np.array([r * math.sin(a), y, r * math.cos(a)])

# ───────────────────────── 1. all-quad mesh ─────────────────────────
def node_xy(i, j, side, k):
    """node (i,j) of pitch k on a side: +1 = designed half, -1 = mirrored half (y -> -y, x + P/2)."""
    i_t, carry = i % NC, i // NC
    x, y = NODE[j][i_t]; x += carry * P + k * P
    if side < 0: x += P / 2; y = -y
    return x, y

class Mesh:
    def __init__(s): s.V, s.F, s.idx = [], [], {}
    def v(s, key, pos):
        if key not in s.idx: s.idx[key] = len(s.V); s.V.append(pos)
        return s.idx[key]
    def quad(s, a, b, c, d):
        if len({a, b, c, d}) == 4: s.F.append((a, b, c, d))

def wrap_key(i, k, side, j, h):
    """vertex identity so that pitch k column NC == pitch k+1 column 0 and the centreline is shared."""
    gi = (i + k * NC) % (N_PITCH * NC)           # global column index around the tyre
    if j == 0: return ('c', (gi + (NC // 2 if side < 0 else 0)) % (N_PITCH * NC), round(h, 3))   # centreline: stagger maps node->node
    return (side, gi, j, round(h, 3))

LEVELS = sorted({0.0, D - SIPE, D})
def levels_between(h1, h2):
    lv = [l for l in LEVELS if min(h1, h2) - 1e-9 <= l <= max(h1, h2) + 1e-9]
    return lv[:-1], lv[1:]

def build_mesh():
    M = Mesh()
    def V(i, j, h, side, k):
        x, y = node_xy(i, j, side, k)
        return M.v(wrap_key(i, k, side, j, h), on_wheel(x, y, h))
    for k in range(N_PITCH):
        for side in (+1, -1):
            for j in range(NR - 1):
                for i in range(NC):
                    h = H[j][i]
                    a, b, c, d = V(i, j, h, side, k), V(i + 1, j, h, side, k), V(i + 1, j + 1, h, side, k), V(i, j + 1, h, side, k)
                    if side > 0: M.quad(a, b, c, d)
                    else:        M.quad(a, d, c, b)
                    # walls to the next column (periodic) and the next row
                    hn = H[j][(i + 1) % NC]
                    if abs(hn - h) > 1e-9:
                        for lo, hi in zip(*levels_between(h, hn)):     # split at every global level -> no T-junctions
                            p, q = V(i + 1, j, lo, side, k), V(i + 1, j + 1, lo, side, k)
                            r, t = V(i + 1, j + 1, hi, side, k), V(i + 1, j, hi, side, k)
                            M.quad(p, q, r, t) if (h > hn) == (side > 0) else M.quad(p, t, r, q)
                    if j + 1 < NR - 1:
                        hn = H[j + 1][i]
                        if abs(hn - h) > 1e-9:
                            for lo, hi in zip(*levels_between(h, hn)):
                                p, q = V(i, j + 1, lo, side, k), V(i + 1, j + 1, lo, side, k)
                                r, t = V(i + 1, j + 1, hi, side, k), V(i, j + 1, hi, side, k)
                                M.quad(p, t, r, q) if (h > hn) == (side > 0) else M.quad(p, q, r, t)
    # centreline: both halves' row-0 cells are rib (height D) so the shared nodes carry one height -> no wall needed
    return M

def check(M):
    E = defaultdict(int)
    for f in M.F:
        for a, b in zip(f, f[1:] + f[:1]): E[(min(a, b), max(a, b))] += 1
    open_e = sum(1 for v in E.values() if v == 1); bad = sum(1 for v in E.values() if v > 2)
    return dict(vertices=len(M.V), faces=len(M.F), all_quads=all(len(f) == 4 for f in M.F),
                open_edges=open_e, nonmanifold_edges=bad)

# ───────────────────────── 2. SolidArc journal ─────────────────────────
def regions():
    """connected groups of cells with the same height > 0 (excluding the rib rows) -> boundary polygons (x,y)."""
    seen = set(); out = []
    for j in range(2, NR - 1):
        for i in range(NC):
            if (i, j) in seen or H[j][i] <= 0: continue
            h = H[j][i]; stack = [(i, j)]; comp = set()
            while stack:
                ci, cj = stack.pop()
                if (ci, cj) in comp or cj < 2 or cj >= NR - 1 or ci < 0 or ci >= NC or abs(H[cj][ci] - h) > 1e-9: continue
                comp.add((ci, cj)); stack += [(ci + 1, cj), (ci - 1, cj), (ci, cj + 1), (ci, cj - 1)]
            seen |= comp
            # boundary edges (directed ccw around each cell), chained into a loop
            edges = defaultdict(list)
            for (ci, cj) in comp:
                corners = [(ci, cj), (ci + 1, cj), (ci + 1, cj + 1), (ci, cj + 1)]
                nb = [(ci, cj - 1), (ci + 1, cj), (ci, cj + 1), (ci - 1, cj)]
                for e in range(4):
                    if nb[e] not in comp: edges[corners[e]].append(corners[(e + 1) % 4])
            def xy(ci, cj): return NODE[cj][ci] if ci < NC else (NODE[cj][0][0] + P, NODE[cj][0][1])
            loops = []
            while edges:
                start = next(iter(edges)); loop = [start]; cur = edges[start].pop()
                if not edges[start]: del edges[start]
                while cur != start:
                    loop.append(cur); nxt = edges[cur].pop()
                    if not edges[cur]: del edges[cur]
                    cur = nxt
                loops.append(loop)
            def area(lp):
                q = [xy(*c) for c in lp]
                return abs(sum(q[a][0] * q[a - 1][1] - q[a - 1][0] * q[a][1] for a in range(len(q)))) / 2
            loop = max(loops, key=area)                       # outer outline (inner loops are holes = sipes, emitted as their own blocks)
            pts = [xy(ci, cj) for ci, cj in loop]
            # drop collinear points
            keep = []
            for q in range(len(pts)):
                a, b, c = np.array(pts[q - 1]), np.array(pts[q]), np.array(pts[(q + 1) % len(pts)])
                if abs((b - a)[0] * (c - b)[1] - (b - a)[1] * (c - b)[0]) > 1e-6: keep.append(pts[q])
            out.append((h, keep))
    return out

def arc_journal():
    L = ['# SolidArc native document v1',
         f'# Tyre tread — directional pattern, {N_PITCH} pitches (pitch {P:.2f} mm). Units: metres. Wheel axis = Y.',
         '# One half-pitch is designed on a warped grid (tools/tread_quads.py); blocks are traced outlines extruded radially,',
         '# the other half is the mirror (y) shifted by half a pitch, then a radial array around Y. No height-maps/textures.',
         'show shading plastic', '']
    S = 0.001
    # crown + rib as one revolve: profile in the XY plane (X = radius, Y = axial), revolved about the Y axis
    prof = []
    ys = np.linspace(-W, W, 25)
    for y in ys: prof.append((floor_r(y), y))
    prof += [(floor_r(W) - 25, W), (floor_r(W) - 25, -W)]
    L.append('polyline ' + ' '.join(f'({x*S:.5f},{y*S:.5f})' for x, y in prof) + ' --closed --name=CrownProfile')
    L.append('revolve CrownProfile 360 --origin=(0,0,0) --axis=(0,1,0) --name=Crown')
    rib = [(floor_r(y) + D, y) for y in (-8, -4, 0, 4, 8)] + [(floor_r(8) - 1, 8), (floor_r(-8) - 1, -8)]
    L.append('polyline ' + ' '.join(f'({x*S:.5f},{y*S:.5f})' for x, y in rib) + ' --closed --name=RibProfile')
    L.append('revolve RibProfile 360 --origin=(0,0,0) --axis=(0,1,0) --name=Rib')
    L.append('tint Crown Rib 0.16 0.16 0.17'); L.append('')
    names = []
    blocks = []
    for h, poly in regions():
        blocks.append((h, poly, 'R'))
        blocks.append((h, [(p[0] + P / 2, -p[1]) for p in poly][::-1], 'L'))   # mirror across y=0 + half-pitch stagger
    for n, (h, poly, side) in enumerate(blocks, 1):
        cx = np.mean([p[0] for p in poly]); cy = np.mean([p[1] for p in poly])
        a = cx / R0; rf = floor_r(cy) - 1.0                             # tangent plane 1 mm under the floor
        o = np.array([rf * math.sin(a), cy, rf * math.cos(a)]) * S
        u = np.array([math.cos(a), 0, -math.sin(a)]); v = np.array([0, 1, 0]); nrm = np.array([math.sin(a), 0, math.cos(a)])
        L.append(f'# STEP block {n} ({"designed half" if side == "R" else "mirrored half, shifted half a pitch"}): outline traced on the grid ({len(poly)} corners), height {h:.1f} mm, drawn on the tangent plane 1 mm under the floor and extruded along the radial normal')
        P3 = [o + (p[0] - cx) * S * u + (p[1] - cy) * S * v for p in poly]
        segs = []
        for q in range(len(P3)):
            a, b = P3[q], P3[(q + 1) % len(P3)]
            L.append(f'line ({a[0]:.6f},{a[1]:.6f},{a[2]:.6f}) ({b[0]:.6f},{b[1]:.6f},{b[2]:.6f}) --name=s{n}_{q}'); segs.append(f's{n}_{q}')
        L.append(f'join {" ".join(segs)} --name=Out{n}')
        L.append(f'extrude Out{n} {(h+1.0)*S:.5f} --direction=({nrm[0]:.5f},{nrm[1]:.5f},{nrm[2]:.5f}) --name=Blk{n}')
        L.append(f'tint Blk{n} 0.16 0.16 0.17'); names.append(f'Blk{n}'); L.append('')
    L.append(f'# STEP array: one full pitch (both halves) x {N_PITCH} around the Y axis')
    L.append(f'radial {" ".join(names)} --count={N_PITCH} --axis=(0,0,0),(0,1,0) --name=Pitch')
    return '\n'.join(L) + '\n'

# ───────────────────────── 3. sheet (plan + quads + 3D) ─────────────────────────
def sheet(M):
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    fig = plt.figure(figsize=(26, 15), dpi=100)
    ax = fig.add_subplot(2, 2, 1)
    for k in range(3):
        for side in (+1, -1):
            for j in range(NR - 1):
                for i in range(NC):
                    poly = [node_xy(i, j, side, k), node_xy(i + 1, j, side, k), node_xy(i + 1, j + 1, side, k), node_xy(i, j + 1, side, k)]
                    h = H[j][i]; c = (0.15, 0.15, 0.16) if h >= D else ((0.45, 0.45, 0.47) if h > 0 else (0.85, 0.85, 0.85))
                    ax.add_patch(plt.Polygon(poly, fc=c, ec='orange', lw=0.4))
    ax.set_xlim(-P, 3.3 * P); ax.set_ylim(-W, W); ax.set_aspect('equal'); ax.set_title('plan: 3 pitches flat (dark = block top, grey = sipe floor, light = groove floor, orange = quad edges)')
    V = np.array(M.V); F = np.array(M.F)
    def view(ax, Rm, title, lim=None):
        Vc = V @ Rm.T; tri = Vc[F]
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]); n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
        front = n[:, 2] > 0; o = np.argsort(tri[front][:, :, 2].mean(1))
        sh = 0.25 + 0.65 * np.clip(0.4 * n[front][:, 2] + 0.5 * n[front][:, 1] + 0.3, 0, 1)
        ax.add_collection(PolyCollection(tri[front][o][:, :, :2], facecolors=plt.cm.gray(sh[o]), edgecolors=('k' if lim else 'none'), linewidths=0.25))
        ax.set_aspect('equal'); ax.set_facecolor('#1b1f24'); ax.set_title(title); ax.set_xticks([]); ax.set_yticks([])
        if lim: ax.set_xlim(*lim[0]); ax.set_ylim(*lim[1])
        else: ax.autoscale()
    a = math.radians(35); Ry = np.array([[math.cos(a), 0, -math.sin(a)], [0, 1, 0], [math.sin(a), 0, math.cos(a)]])
    b = math.radians(25); Rx = np.array([[1, 0, 0], [0, math.cos(b), -math.sin(b)], [0, math.sin(b), math.cos(b)]])
    view(fig.add_subplot(2, 2, 2), Rx @ Ry, 'quad mesh on the wheel (3/4 view)')
    view(fig.add_subplot(2, 2, 3), np.array([[0, 1, 0], [1, 0, 0], [0, 0, 1]]), 'quad mesh — looking down onto the tread (top of wheel), wire on', lim=((-W - 5, W + 5), (-70, 70)))
    view(fig.add_subplot(2, 2, 4), np.eye(3), 'quad mesh — side (axis view)')
    fig.suptitle(f'Tread: {N_PITCH} pitches × 2 halves, {len(M.F)} quads, {len(M.V)} vertices — traced quads, no height-map', fontsize=15)
    plt.tight_layout(); fig.savefig(OUT / 'tread_sheet.png')

if __name__ == '__main__':
    M = build_mesh(); st = check(M); print(json.dumps(st))
    with open(OUT / 'Tread_quads.obj', 'w') as f:
        f.write(f'# tyre tread, all quads, mm. {st}\n')
        for p in M.V: f.write(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f}\n')
        for q in M.F: f.write('f %d %d %d %d\n' % tuple(i + 1 for i in q))
    (OUT / 'Tread.arc').write_text(arc_journal())
    sheet(M)
    print('wrote', OUT / 'Tread.arc', OUT / 'Tread_quads.obj', OUT / 'tread_sheet.png')

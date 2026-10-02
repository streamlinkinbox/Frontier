#!/usr/bin/env python3
"""Tyre treads: CAD model in SolidArc + all-quad mesh, from ONE pattern definition per design.

A design describes one pitch of ONE half of the tread on a WARPED GRID: rows run across the tread (y mm from
the centreline), columns run around the tyre (x mm).  Every row may place its column nodes anywhere, so lug
edges can be slanted, curved, wavy and tapered; every cell carries a height (0 = groove floor, D = block top,
D-s = sipe floor).  No height-maps / textures / displacement: the geometry is the traced outline of the blocks.

  half tile --(mirror+stagger | mirror | second half design)--> full pitch --radial x N--> tread band

Outputs per design (in <out>/<Design>/):  Tread.arc (SolidArc journal), Tread_quads.obj (welded all-quad mesh,
audited), plan.png (flat plan of three pitches with the quad grid).

usage: tread_quads.py <outdir> [design ...]      designs: Vee Touring Sport AllTerrain Winter
"""
import sys, math, json, numpy as np
from pathlib import Path
from collections import defaultdict

# ───────────────────────────── design base ─────────────────────────────
class Half:
    """one half of a tread (centreline y=0 .. shoulder y=W); subclasses define rows / columns / heights"""
    NC = 8
    def __init__(s, T): s.T = T                                    # T = the Tread (tyre numbers)
    def rows(s): raise NotImplementedError                          # [(y, layout), ...]  first row y=0 'uniform'
    def columns(s, y, layout): raise NotImplementedError            # NC node x positions (pitch-periodic)
    def height(s, i, j): raise NotImplementedError                  # cell (col i, row j) height
    def uniform(s):
        P = s.T.P; return [-P / 2 + k * P / s.NC for k in range(s.NC)]
    def widths_to_x(s, widths, x0):
        xs, x = [], x0
        for w in widths: xs.append(x); x += w
        return xs
    def build(s):
        s.R = s.rows(); s.NR = len(s.R)
        s.NODE = [[(x, s.R[j][0]) for x in s.columns(s.R[j][0], s.R[j][1])] for j in range(s.NR)]
        s.H = [[s.height(i, j) for i in range(s.NC)] for j in range(s.NR - 1)]
        return s

class Tread:
    name = 'Base'; R0 = 330.0; W = 95.0; CROWN = 6.0; D = 8.5; SIPE = 4.0; N = 68
    second = 'mirror_stagger'          # 'mirror_stagger' | 'mirror' | 'asym'
    def __init__(s):
        s.P = 2 * math.pi * s.R0 / s.N
        s.halves = s.make_halves()
        for h in s.halves: h.build()
        s.LEVELS = sorted({0.0, s.D - s.SIPE, s.D} | set(s.extra_levels()))
    def extra_levels(s): return []
    def make_halves(s): raise NotImplementedError
    def floor_r(s, y): return s.R0 - s.CROWN * (y / s.W) ** 2
    def on_wheel(s, x, y, h):
        r = s.floor_r(y) + h; a = x / s.R0
        return np.array([r * math.sin(a), y, r * math.cos(a)])
    def half(s, side): return s.halves[0] if (side > 0 or len(s.halves) == 1) else s.halves[1]
    def stagger(s): return s.P / 2 if s.second == 'mirror_stagger' else 0.0

# ───────────────────────────── the five designs ─────────────────────────────
class VeeHalf(Half):
    """directional V: curved diagonal lugs widening to the shoulder, inner/outer longitudinal grooves, sipes, notches"""
    NC = 8
    def shear(s, y): t = y / s.T.W; return 0.62 * y - 22.0 * t * t
    def gw(s, y): return 4.0 + 4.5 * (y / s.T.W)
    def rows(s): return [(0, 'uniform'), (4, 'uniform'), (8, 'rib'), (12, 'lug'), (20, 'lug'), (28, 'lug'), (36, 'lug'), (44, 'lug'),
                         (49, 'lug'), (55, 'lug'), (63, 'lug'), (71, 'lug'), (79, 'lug'), (87, 'lug'), (95, 'lug')]
    def columns(s, y, layout):
        if layout == 'uniform': return s.uniform()
        P = s.T.P; g = s.gw(y) if layout == 'lug' else 5.0; lug = P - g
        w = [g, lug * .19, lug * .19, 1.6, lug * .19, lug * .19, (lug - 1.6) * .12]; w.append(P - sum(w))
        return [x + s.shear(y) for x in s.widths_to_x(w, -P / 2)]
    def height(s, i, j):
        D, S = s.T.D, s.T.SIPE; y0, l0 = s.R[j]; y1, l1 = s.R[j + 1]
        if l1 in ('uniform', 'rib'): return 0.0 if (l1 == 'rib' and i == 0) else D
        if y0 < 12: return 0.0
        if i == 0: return 0.0
        if 49 <= y0 < 55: return 0.0
        if i == 3 and y0 >= 55: return D - S
        if i == 6 and 36 <= y0 < 49: return 0.0
        if i == 6 and y0 >= 87: return 0.0
        if i == 3 and 28 <= y0 < 36: return D - S
        return D
class Vee(Tread):
    name = 'Vee'; second = 'mirror_stagger'
    def make_halves(s): return [VeeHalf(s)]

class TouringHalf(Half):
    """symmetric touring: zig-zag longitudinal grooves, straight lateral grooves, blocks with two sipes, continuous shoulder rib with notches"""
    NC = 10
    def rows(s): return [(0, 'uniform'), (3, 'uniform'), (7, 'a'), (11, 'b'), (16, 'a'), (21, 'b'), (26, 'a'), (31, 'b'), (36, 'a'),
                         (41, 'b'), (46, 'a'), (51, 'b'), (56, 'a'), (61, 'b'), (66, 'a'), (72, 'a'), (80, 'a'), (88, 'a'), (95, 'a')]
    def columns(s, y, layout):
        if layout == 'uniform': return s.uniform()
        P = s.T.P; zig = 1.8 if layout == 'a' else -1.8          # zig-zag: alternate rows shift the whole column set
        w = [4.5, (P - 4.5) * .22, 1.4, (P - 4.5) * .22, 1.4, (P - 4.5) * .22, 1.4, (P - 4.5) * .17]; w.append(P - sum(w) - 0.0)
        w = w[:-1] + [max(P - sum(w[:-1]) - 1.0, 1.0), 1.0]
        return [x + zig for x in s.widths_to_x(w, -P / 2)]
    def height(s, i, j):
        D, S = s.T.D, s.T.SIPE; y0, _ = s.R[j]
        if y0 < 7: return D if i not in (2, 6) or y0 < 3 else D - S          # centre rib with two fine sipes
        if 7 <= y0 < 11 or 31 <= y0 < 36 or 61 <= y0 < 66: return 0.0        # three longitudinal (zig-zag) grooves
        if i == 0: return 0.0 if y0 < 61 else (0.0 if y0 < 80 else D)        # lateral groove; shoulder: notch only up to y 80
        if i in (2, 4, 6) and 11 <= y0 < 61: return D - S                    # sipes through the inner blocks
        if i == 8 and y0 >= 66: return D - S                                 # shoulder sipe
        if i == 9 and y0 >= 88: return 0.0                                   # shoulder notch at the rim
        return D
class Touring(Tread):
    name = 'Touring'; second = 'mirror'; R0 = 320.0; W = 100.0; D = 8.0; SIPE = 3.5; N = 72
    def make_halves(s): return [TouringHalf(s)]

class SportInner(Half):
    """asymmetric UHP, inboard half: narrow curved lugs, fine sipes, water-evacuating slanted grooves"""
    NC = 8
    def shear(s, y): return 0.35 * y + 0.0015 * y * y
    def rows(s): return [(0, 'uniform'), (4, 'uniform'), (9, 'lug'), (15, 'lug'), (24, 'lug'), (33, 'lug'), (42, 'lug'), (47, 'lug'),
                         (56, 'lug'), (66, 'lug'), (76, 'lug'), (86, 'lug'), (95, 'lug')]
    def columns(s, y, layout):
        if layout == 'uniform': return s.uniform()
        P = s.T.P; g = 5.0 + 3.0 * y / s.T.W
        w = [g, (P - g) * .3, 1.4, (P - g) * .3, 1.4]; w.append((P - sum(w)) * .5); w.append(P - sum(w) - 1.2); w.append(1.2)
        return [x + s.shear(y) for x in s.widths_to_x(w, -P / 2)]
    def height(s, i, j):
        D, S = s.T.D, s.T.SIPE; y0, _ = s.R[j]
        if y0 < 4: return D
        if 4 <= y0 < 9: return 0.0                                           # groove beside the centre rib
        if 42 <= y0 < 47: return 0.0                                         # mid groove
        if i == 0: return 0.0
        if i in (2, 4) and y0 >= 9: return D - S                             # two sipes along each lug
        if i == 7 and 47 <= y0 < 86: return D - S
        return D
class SportOuter(Half):
    """asymmetric UHP, outboard half: wide centre rib, large stiff shoulder blocks, one wide groove, short lateral notches"""
    NC = 8
    def rows(s): return [(0, 'uniform'), (4, 'uniform'), (14, 'rib'), (22, 'rib'), (30, 'g'), (40, 'blk'), (50, 'blk'), (60, 'blk'),
                         (70, 'blk'), (80, 'blk'), (88, 'blk'), (95, 'blk')]
    def columns(s, y, layout):
        if layout == 'uniform': return s.uniform()
        P = s.T.P; g = 7.0; sh = 0.12 * max(0.0, y - 30)
        w = [g, (P - g) * .28, (P - g) * .28, 1.5, (P - g) * .2]; w.append((P - sum(w)) * .5); w.append(P - sum(w) - 1.0); w.append(1.0)
        return [x + sh for x in s.widths_to_x(w, -P / 2)]
    def height(s, i, j):
        D, S = s.T.D, s.T.SIPE; y0, _ = s.R[j]
        if y0 < 22: return D if not (i == 0 and 14 <= y0 < 22) else D - S     # wide rib, short lateral sipe notch at its edge
        if 22 <= y0 < 30: return 0.0                                          # the wide outboard groove
        if i == 0 and y0 < 70: return 0.0                                     # lateral groove up to y 70, then closed (stiff shoulder)
        if i == 3 and y0 >= 40: return D - S                                  # one sipe across the block
        if i == 7 and y0 >= 88: return 0.0                                    # rim notch
        return D
class Sport(Tread):
    name = 'Sport'; second = 'asym'; R0 = 340.0; W = 115.0; CROWN = 5.0; D = 7.0; SIPE = 3.0; N = 66
    def make_halves(s): return [SportInner(s), SportOuter(s)]

class AllTerrainHalf(Half):
    """all-terrain: big staggered blocks, wide deep voids, stepped block edges, stone ejectors (low ribs) in the voids"""
    NC = 8
    def rows(s): return [(0, 'uniform'), (5, 'uniform'), (10, 'c'), (18, 'c'), (26, 'c'), (34, 'g'), (42, 's'), (52, 's'), (62, 's'),
                         (70, 'g2'), (78, 'o'), (88, 'o'), (98, 'o'), (108, 'o')]
    def columns(s, y, layout):
        if layout == 'uniform': return s.uniform()
        P = s.T.P; g = 9.0 if layout in ('c', 'g') else 11.0
        off = {'c': 0.0, 'g': 0.0, 's': P * 0.5, 'g2': P * 0.5, 'o': P * 0.18}[layout]      # block rows staggered against each other
        step = 2.5 if layout == 'o' else 0.0
        w = [g, (P - g) * .3, 2.0, (P - g) * .3 - step, 2.0]; w.append(P - sum(w) - 2.0 - 2.0); w.append(2.0); w.append(2.0)
        return [x + off for x in s.widths_to_x(w, -P / 2)]
    def height(s, i, j):
        D = s.T.D; EJ = 3.0; y0, l0 = s.R[j]; y1, l1 = s.R[j + 1]
        if y0 < 10: return D if i != 0 else (0.0 if y0 >= 5 else D)
        if l1 == 'g' or l1 == 'g2': return EJ if i == 2 else 0.0                # circumferential voids with a stone-ejector rib
        if i == 0: return 0.0                                                    # lateral voids
        if i in (2, 4) and l1 == 's': return D - 2.0                             # shallow sipes on the mid blocks
        if i == 6 and l1 in ('c', 's'): return 0.0 if y0 in (18, 52) else D      # stepped notch into the block
        if i == 7: return 0.0 if l1 == 'o' else D                                # outer blocks are shorter: void strip
        return D
class AllTerrain(Tread):
    name = 'AllTerrain'; second = 'mirror_stagger'; R0 = 390.0; W = 108.0; CROWN = 10.0; D = 14.0; SIPE = 2.0; N = 46
    def extra_levels(s): return [3.0, s.D - 2.0]
    def make_halves(s): return [AllTerrainHalf(s)]

class WinterHalf(Half):
    """winter: directional blocks with wavy edges and 3–4 zig-zag sipes each, wide centre groove, serrated shoulders"""
    NC = 12
    def shear(s, y): return 0.45 * y
    def wave(s, y, k): return 0.9 * math.sin(y * 0.9 + k)         # zig-zag sipes: the sipe columns wiggle row by row
    def rows(s): return [(0, 'uniform'), (3, 'uniform'), (6, 'g'), (10, 'l'), (15, 'l'), (20, 'l'), (25, 'l'), (30, 'l'), (36, 'l'),
                         (41, 'l'), (46, 'l'), (51, 'l'), (56, 'l'), (61, 'l'), (66, 'l'), (72, 'l'), (78, 'l'), (84, 'l'), (90, 'l'), (96, 'l')]
    def columns(s, y, layout):
        if layout == 'uniform': return s.uniform()
        P = s.T.P; g = 6.0; b = (P - g - 4 * 1.2) / 5
        w = [g, b, 1.2, b, 1.2, b, 1.2, b, 1.2, b * .45, b * .3]; w.append(P - sum(w))
        xs = s.widths_to_x(w, -P / 2)
        return [x + s.shear(y) + (s.wave(y, k) if k in (2, 3, 4, 5, 6, 7, 8, 9) else 0.0) for k, x in enumerate(xs)]
    def height(s, i, j):
        D, S = s.T.D, s.T.SIPE; y0, _ = s.R[j]
        if y0 < 3: return D
        if 3 <= y0 < 10: return 0.0                                              # wide centre groove (y 3..10 both sides)
        if i == 0: return 0.0
        if i in (2, 4, 6, 8) and y0 >= 10: return D - S                          # four zig-zag sipes per block
        if i == 10 and y0 >= 10: return 0.0 if (int(y0) // 10) % 2 == 0 else D   # serrated edge: alternating notches
        if 46 <= y0 < 51: return 0.0                                             # lateral cut splitting the lug in two blocks
        return D
class Winter(Tread):
    name = 'Winter'; second = 'mirror_stagger'; R0 = 325.0; W = 96.0; CROWN = 6.0; D = 9.5; SIPE = 6.0; N = 64
    def make_halves(s): return [WinterHalf(s)]

DESIGNS = {c.name: c for c in (Vee, Touring, Sport, AllTerrain, Winter)}

# ───────────────────────────── quad mesh ─────────────────────────────
class Mesh:
    def __init__(s): s.V, s.F, s.idx = [], [], {}
    def v(s, key, pos):
        if key not in s.idx: s.idx[key] = len(s.V); s.V.append(pos)
        return s.idx[key]
    def quad(s, a, b, c, d):
        if len({a, b, c, d}) == 4: s.F.append((a, b, c, d))

def node_xy(T, i, j, side, k):
    Hf = T.half(side); NC = Hf.NC
    x, y = Hf.NODE[j][i % NC]; x += (i // NC) * T.P + k * T.P
    if side < 0: x += T.stagger(); y = -y
    return x, y

def build_mesh(T):
    M = Mesh(); NCg = T.halves[0].NC; total = T.N * NCg
    shift = NCg // 2 if T.second == 'mirror_stagger' else 0
    def key(i, k, side, j, h):
        gi = (i + k * NCg) % total
        if j == 0: return ('c', (gi + (shift if side < 0 else 0)) % total, round(h, 3))
        return (side, gi, j, round(h, 3))
    def V(i, j, h, side, k):
        x, y = node_xy(T, i, j, side, k); return M.v(key(i, k, side, j, h), T.on_wheel(x, y, h))
    def levels(h1, h2):
        lv = [l for l in T.LEVELS if min(h1, h2) - 1e-9 <= l <= max(h1, h2) + 1e-9]; return zip(lv[:-1], lv[1:])
    for k in range(T.N):
        for side in (+1, -1):
            Hf = T.half(side); NC, NR, H = Hf.NC, Hf.NR, Hf.H
            assert NC == NCg, 'both halves must share the column count'
            for j in range(NR - 1):
                for i in range(NC):
                    h = H[j][i]
                    a, b, c, d = V(i, j, h, side, k), V(i + 1, j, h, side, k), V(i + 1, j + 1, h, side, k), V(i, j + 1, h, side, k)
                    M.quad(a, b, c, d) if side > 0 else M.quad(a, d, c, b)
                    hn = H[j][(i + 1) % NC]
                    if abs(hn - h) > 1e-9:
                        for lo, hi in levels(h, hn):
                            p, q = V(i + 1, j, lo, side, k), V(i + 1, j + 1, lo, side, k)
                            r, t = V(i + 1, j + 1, hi, side, k), V(i + 1, j, hi, side, k)
                            M.quad(p, q, r, t) if (h > hn) == (side > 0) else M.quad(p, t, r, q)
                    if j + 1 < NR - 1:
                        hn = H[j + 1][i]
                        if abs(hn - h) > 1e-9:
                            for lo, hi in levels(h, hn):
                                p, q = V(i, j + 1, lo, side, k), V(i + 1, j + 1, lo, side, k)
                                r, t = V(i + 1, j + 1, hi, side, k), V(i, j + 1, hi, side, k)
                                M.quad(p, t, r, q) if (h > hn) == (side > 0) else M.quad(p, q, r, t)
    return M

def audit(M):
    E = defaultdict(int)
    for f in M.F:
        for a, b in zip(f, f[1:] + f[:1]): E[(min(a, b), max(a, b))] += 1
    return dict(vertices=len(M.V), faces=len(M.F), all_quads=all(len(f) == 4 for f in M.F),
                open_edges=sum(1 for v in E.values() if v == 1), nonmanifold_edges=sum(1 for v in E.values() if v > 2))

# ───────────────────────────── block outlines → SolidArc journal ─────────────────────────────
def regions(Hf, T):
    """connected groups of cells with equal height > 0 (rows after the centre band) → outer outline polygons"""
    NC, NR, H, NODE, P = Hf.NC, Hf.NR, Hf.H, Hf.NODE, T.P
    j0 = next(j for j in range(NR) if Hf.R[j][1] != 'uniform')   # skip the rib band (it is revolved)
    seen = set(); out = []
    for j in range(j0, NR - 1):
        for i in range(NC):
            if (i, j) in seen or H[j][i] <= 0: continue
            h = H[j][i]; stack = [(i, j)]; comp = set()
            while stack:
                ci, cj = stack.pop()
                if (ci, cj) in comp or cj < j0 or cj >= NR - 1 or ci < 0 or ci >= NC or abs(H[cj][ci] - h) > 1e-9: continue
                comp.add((ci, cj)); stack += [(ci + 1, cj), (ci - 1, cj), (ci, cj + 1), (ci, cj - 1)]
            seen |= comp
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
                q = [xy(*c) for c in lp]; return abs(sum(q[a][0] * q[a - 1][1] - q[a - 1][0] * q[a][1] for a in range(len(q)))) / 2
            pts = [xy(ci, cj) for ci, cj in max(loops, key=area)]
            keep = []
            for q in range(len(pts)):
                a, b, c = np.array(pts[q - 1]), np.array(pts[q]), np.array(pts[(q + 1) % len(pts)])
                if abs((b - a)[0] * (c - b)[1] - (b - a)[1] * (c - b)[0]) > 1e-6: keep.append(pts[q])
            out.append((h, keep))
    return out

def arc_journal(T):
    S = 0.001; W, D = T.W, T.D
    L = ['# SolidArc native document v1',
         f'# Tyre tread "{T.name}" — {T.N} pitches (pitch {T.P:.2f} mm), floor radius {T.R0:.0f} mm, width {2*W:.0f} mm, depth {D} mm. Units: metres, wheel axis = Y.',
         '# One half-pitch is designed on a warped grid (tools/tread_quads.py); blocks are traced outlines extruded radially;',
         f'# second half: {T.second}; then a radial array around Y. No height-maps / textures.', 'show shading plastic', '']
    prof = [(T.floor_r(y), y) for y in np.linspace(-W, W, 25)] + [(T.floor_r(W) - 25, W), (T.floor_r(W) - 25, -W)]
    L.append('polyline ' + ' '.join(f'({x*S:.5f},{y*S:.5f})' for x, y in prof) + ' --closed --name=CrownProfile')
    L.append('revolve CrownProfile 360 --origin=(0,0,0) --axis=(0,1,0) --name=Crown')
    # centre band(s): the 'uniform' rows of each half are the rib → revolve them (per half, since an asymmetric tyre differs)
    for side in (+1, -1):
        Hf = T.half(side); yr = max(y for y, l in Hf.R if l == 'uniform'); hr = Hf.H[0][1]
        if yr <= 0 or hr <= 0: continue
        sg = 1 if side > 0 else -1
        rib = [(T.floor_r(y) + hr, y * sg) for y in (0, yr / 2, yr)] + [(T.floor_r(yr) - 1, yr * sg), (T.floor_r(0) - 1, 0)]
        L.append('polyline ' + ' '.join(f'({x*S:.5f},{y*S:.5f})' for x, y in rib) + f' --closed --name=RibProfile{"R" if side>0 else "L"}')
        L.append(f'revolve RibProfile{"R" if side>0 else "L"} 360 --origin=(0,0,0) --axis=(0,1,0) --name=Rib{"R" if side>0 else "L"}')
    L.append('tint Crown 0.16 0.16 0.17'); L.append('')
    blocks = []
    for h, poly in regions(T.halves[0], T): blocks.append((h, poly, 'R'))
    if T.second == 'asym':
        for h, poly in regions(T.halves[1], T): blocks.append((h, [(p[0], -p[1]) for p in poly][::-1], 'L'))
    else:
        for h, poly in regions(T.halves[0], T): blocks.append((h, [(p[0] + T.stagger(), -p[1]) for p in poly][::-1], 'L'))
    names = []
    for n, (h, poly, side) in enumerate(blocks, 1):
        cx = np.mean([p[0] for p in poly]); cy = np.mean([p[1] for p in poly])
        a = cx / T.R0; rf = T.floor_r(cy) - 1.0
        o = np.array([rf * math.sin(a), cy, rf * math.cos(a)]) * S
        u = np.array([math.cos(a), 0, -math.sin(a)]); v = np.array([0, 1, 0]); nrm = np.array([math.sin(a), 0, math.cos(a)])
        what = {'R': 'designed half', 'L': {'mirror_stagger': 'mirrored half, shifted half a pitch', 'mirror': 'mirrored half', 'asym': 'outboard half (own design)'}[T.second]}[side]
        L.append(f'# STEP block {n} ({what}): outline traced on the grid ({len(poly)} corners), height {h:.1f} mm, drawn on the tangent plane 1 mm under the floor, extruded radially')
        P3 = [o + (p[0] - cx) * S * u + (p[1] - cy) * S * v for p in poly]; segs = []
        for q in range(len(P3)):
            a3, b3 = P3[q], P3[(q + 1) % len(P3)]
            L.append(f'line ({a3[0]:.6f},{a3[1]:.6f},{a3[2]:.6f}) ({b3[0]:.6f},{b3[1]:.6f},{b3[2]:.6f}) --name=s{n}_{q}'); segs.append(f's{n}_{q}')
        L.append(f'join {" ".join(segs)} --name=Out{n}')
        L.append(f'extrude Out{n} {(h+1.0)*S:.5f} --direction=({nrm[0]:.5f},{nrm[1]:.5f},{nrm[2]:.5f}) --name=Blk{n}')
        L.append(f'tint Blk{n} 0.16 0.16 0.17'); names.append(f'Blk{n}'); L.append('')
    L.append(f'# STEP array: one full pitch (both halves) x {T.N} around the Y axis')
    L.append(f'radial {" ".join(names)} --count={T.N} --axis=(0,0,0),(0,1,0) --name=Pitch')
    return '\n'.join(L) + '\n'

def plan_png(T, path):
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(16, 10), dpi=100)
    for k in range(3):
        for side in (+1, -1):
            Hf = T.half(side)
            for j in range(Hf.NR - 1):
                for i in range(Hf.NC):
                    poly = [node_xy(T, i, j, side, k), node_xy(T, i + 1, j, side, k), node_xy(T, i + 1, j + 1, side, k), node_xy(T, i, j + 1, side, k)]
                    h = Hf.H[j][i]; c = plt.cm.gray(0.15 + 0.7 * (1 - h / T.D))
                    ax.add_patch(plt.Polygon(poly, fc=c, ec='orange', lw=0.35))
    ax.set_xlim(-T.P, 3.4 * T.P); ax.set_ylim(-T.W, T.W); ax.set_aspect('equal')
    ax.set_title(f'{T.name}: 3 pitches flat — dark = block top, mid = sipe/ejector, light = groove floor; orange = quad edges; second half = {T.second}')
    plt.tight_layout(); fig.savefig(path); plt.close(fig)

if __name__ == '__main__':
    out = Path(sys.argv[1]); names = sys.argv[2:] or list(DESIGNS)
    for nm in names:
        T = DESIGNS[nm](); d = out / nm; d.mkdir(parents=True, exist_ok=True)
        M = build_mesh(T); st = audit(M)
        with open(d / 'Tread_quads.obj', 'w') as f:
            f.write(f'# tyre tread {nm}, all quads, mm. {st}\n')
            for p in M.V: f.write(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f}\n')
            for q in M.F: f.write('f %d %d %d %d\n' % tuple(i + 1 for i in q))
        (d / 'Tread.arc').write_text(arc_journal(T)); plan_png(T, d / 'plan.png')
        print(nm, json.dumps(st), 'blocks/pitch', sum(1 for _ in regions(T.halves[0], T)) * 2)

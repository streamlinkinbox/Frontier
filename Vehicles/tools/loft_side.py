#!/usr/bin/env python3
"""Phase 3 — +Y side of Body_Main_Shell lofted curve-to-curve from the fitted feature curves.

* Only CREASE / BOUNDARY curves are used (silhouettes are flat guides, never lofted).
* One strip = loft between two neighbouring curves, trimmed to their common x-range.
* Each strip is checked against the original Blender mesh (nearest-vertex distance of the ruled surface).

usage: loft_side.py Liger
"""
import json, sys, numpy as np
from pathlib import Path
from scipy.spatial import cKDTree

CAR = sys.argv[1] if len(sys.argv) > 1 else 'Liger'
HERE = Path(__file__).resolve().parent.parent; OUT = HERE / CAR
SCALE, N, TINT = 0.01, 16, '0.80 0.82 0.86'
curves = json.load(open(OUT / 'curves.json'))['curves']
doc = json.load(open(OUT / 'curves.json')); YC = doc.get('mirror_offset_applied', 0.0)
mesh = np.load(OUT / 'mesh/Body_Main_Shell.npz')
MV = mesh['V'] - np.array([0, YC if isinstance(YC, (int, float)) else YC[1], 0]); MT = mesh['T']; tree = cKDTree(MV)
fn = np.cross(MV[MT[:, 1]] - MV[MT[:, 0]], MV[MT[:, 2]] - MV[MT[:, 0]])
VN = np.zeros_like(MV)
for k in range(3): np.add.at(VN, MT[:, k], fn)
VN /= np.linalg.norm(VN, axis=1)[:, None] + 1e-12
MID_TOL = 4.0   # cm: strips further from the reference than this get a construction mid-section

def C(i):
    assert curves[i]['kind'] != 'SILHOUETTE', f'curve {i} is a silhouette (guide only)'
    return np.array(curves[i]['pts'], float)
def half(P): return P[P[:, 1] > -2]                              # +Y part of a cross-body U
def by_x(P, x0, x1):
    o = np.argsort(P[:, 0]); P = P[o]; xs = np.linspace(x0, x1, N)
    return np.c_[xs, np.interp(xs, P[:, 0], P[:, 1]), np.interp(xs, P[:, 0], P[:, 2])]
def cat(*Ps):                                                    # join curves end-to-end, sorted by x
    P = np.vstack(Ps); return P[np.argsort(P[:, 0])]
def arch(P):
    """wheel-arch opening: order rear foot -> over the top -> front foot (arc-length param)."""
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]
    if P[0, 0] > P[-1, 0]: P = P[::-1]
    return P
def by_s(P):
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]; t = np.linspace(0, d[-1], N)
    return np.c_[[np.interp(t, d, P[:, k]) for k in range(3)]].T

# name, inner/upper curve, outer/lower curve, param ('x' common x-range | 's' arc length)
PLAN = [
    # rear deck, centre -> outboard
    ('Deck_Step',      75, 73, 'x'),
    ('Deck_Ledge',     73, 34, 'x'),
    ('Deck_Outer',     34, 60, 'x'),
    ('Rear_Shoulder',  60, 27, 'x'),
    ('Quarter_Top',    27, 70, 'x'),
    # rear quarter panel: quarter line straight down to the rear-arch opening (092 is the panel cut, drawn only)
    ('Quarter_Panel',  ('h', 76), 175, 'a'),
    ('Quarter_Sill',   70, 29, 'x'),
    # cabin side
    ('Cant_Rail',      ('h', 62), 22, 'x'),
    ('Door_Upper',     22, 63, 'x'),
    ('Door_Lower',     63, 29, 'x'),
    ('Sill',           29, ('cat', 174, 173), 'x'),
    # front fender: bonnet edge + bonnet shoulder as one top rail, straight down to the front-arch opening
    # (064/065 = door/fender cut lines, 026 = flare crease: drawn only, not loft rails)
    ('Fender_Panel',   ('cat', 22, 24), 194, 'a'),
]

def get(i):
    if isinstance(i, tuple):
        return half(C(i[1])) if i[0] == 'h' else cat(*[C(j) for j in i[1:]])
    return C(i)
def nm(i):
    if isinstance(i, tuple): return '+'.join(curves[j]['name'] for j in i[1:]) + ('(+Y half)' if i[0] == 'h' else '')
    return curves[i]['name']

out = ['# SolidArc native document v1',
       f'# {CAR} — phase 3: +Y side of the main shell, lofted curve-to-curve from the fitted feature curves.',
       '# Silhouettes are not used. One STEP per strip; sections are the two bounding curves resampled on their common range.',
       'show shading plastic', '']
rows = []
for k, (name, a, b, mode) in enumerate(PLAN, 1):
    A, B = get(a), get(b)
    if mode == 'x':
        x0 = max(A[:, 0].min(), B[:, 0].min()); x1 = min(A[:, 0].max(), B[:, 0].max())
        if x1 - x0 < 8: rows.append((name, 'NO OVERLAP')); continue
        SA, SB = by_x(A, x0, x1), by_x(B, x0, x1)
    elif mode == 'a':                            # A = top rail (by x over the arch's span), B = arch opening (by arc length)
        B = arch(B); SB = by_s(B)
        o = np.argsort(A[:, 0]); A = A[o]; xs = np.clip(SB[:, 0], A[0, 0], A[-1, 0])   # rail point above each arch point
        SA = np.c_[xs, np.interp(xs, A[:, 0], A[:, 1]), np.interp(xs, A[:, 0], A[:, 2])]
    else:
        SA, SB = by_s(A), by_s(B)
        if np.linalg.norm(SA[0]-SB[0]) > np.linalg.norm(SA[0]-SB[-1]): SB = SB[::-1]
    # deviation of the ruled strip from the mesh
    t = np.linspace(0, 1, 9)[1:-1][:, None, None]
    samp = (SA[None] * (1 - t) + SB[None] * t).reshape(-1, 3)
    dev = tree.query(samp)[0]
    # orientation: kernel normal = tangent x (A->B); agree with the reference surface normal
    m = N // 2; n = np.cross(SA[m+1]-SA[m-1], SB[m]-SA[m]); mid = (SA[m]+SB[m])/2
    flip = np.dot(n, VN[tree.query(mid)[1]]) < 0
    sections = [SA, SB]; note = ''
    if dev.max() > MID_TOL:                     # construction section: midline dropped onto the reference surface
        M = (SA + SB) / 2; M = MV[tree.query(M)[1]]
        M = np.c_[M[:, 0] * 0 + (SA[:, 0] + SB[:, 0]) / 2, M[:, 1], M[:, 2]]   # keep x stations of the strip
        sections = [SA, M, SB]
        samp2 = np.r_[(SA[None]*(1-t) + M[None]*t).reshape(-1, 3), (M[None]*(1-t) + SB[None]*t).reshape(-1, 3)]
        dev2 = tree.query(samp2)[0]; note = f' -> with mid-section mean {dev2.mean():.1f} / max {dev2.max():.1f}'
    secs = []
    for j, S in enumerate(sections, 1):
        pts = ' '.join(f'({q[0]*SCALE:.4f},{q[1]*SCALE:.4f},{q[2]*SCALE:.4f})' for q in S)
        tag = ' (construction: strip midline projected onto the reference surface)' if len(sections) == 3 and j == 2 else ''
        out.append(f'spline {pts} --name=R_{name}_c{j}{("  #" + tag) if tag else ""}'); secs.append(f'R_{name}_c{j}')
    if flip: secs = secs[::-1]
    out += [f'# STEP {k} — R_{name}: {nm(a)} -> {nm(b)}, x {SA[0,0]:.0f}..{SA[-1,0]:.0f} cm, mesh deviation mean {dev.mean():.1f} / max {dev.max():.1f} cm',
            f'loft {" ".join(secs)} --sheet --name=R_{name}', f'delete {" ".join(secs)}', f'tint R_{name} {TINT}', '']
    rows.append((name, f'{nm(a):22s} -> {nm(b):22s} x {SA[0,0]:5.0f}..{SA[-1,0]:4.0f}  dev mean {dev.mean():4.1f} max {dev.max():4.1f} cm{note}'))

(OUT / f'{CAR}_Body_SideR.arc').write_text('\n'.join(out) + '\n')
for r in rows: print(f'{r[0]:15s} {r[1]}')
print(f'-> {OUT}/{CAR}_Body_SideR.arc')

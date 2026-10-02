#!/usr/bin/env python3
"""Phase 2c — loft the fitted FEATURE CURVES pairwise (no polygon topology).

Each strip is a two-section loft between two neighbouring longitudinal curves from
<car>/curves.json, trimmed to their common x-overlap and resampled at the same N
parameter values.  The -Y side is emitted explicitly (negated y, reversed point
order) because the kernel's `mirror` flips surface orientation.

usage: curve_loft.py Liger
"""
import json, sys, numpy as np
from pathlib import Path

CAR = sys.argv[1] if len(sys.argv) > 1 else 'Liger'
HERE = Path(__file__).resolve().parent.parent
OUT = HERE / CAR
SCALE = 0.01                     # cm -> m for the kernel
N = 14                           # samples per section
TINT = '0.80 0.82 0.86'

curves = json.load(open(OUT / 'curves.json'))['curves']
def C(i): return np.array(curves[i]['pts'], float)

def resample_x(P, x0, x1, n=N):
    """Resample a curve (roughly monotonic in x) at n evenly spaced x in [x0,x1]."""
    o = np.argsort(P[:, 0]); P = P[o]
    xs = np.linspace(x0, x1, n)
    return np.c_[xs, np.interp(xs, P[:, 0], P[:, 1]), np.interp(xs, P[:, 0], P[:, 2])]

def resample_s(P, n=N):
    """Resample by arc length (for curves that are not monotonic in x)."""
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]
    t = np.linspace(0, d[-1], n)
    return np.c_[[np.interp(t, d, P[:, k]) for k in range(3)]].T

def mirror_y(P): return P * np.array([1, -1, 1])
def half(P, eps=2.0):
    """+Y half of a cross-body (U-shaped) curve."""
    return P[P[:, 1] > -eps]

# (name, curve A, curve B, mode) ; B may be ('mirror', idx) = the -Y twin -> centre strip
#  mode 'x' = trim to x-overlap & resample by x ; 's' = arc-length resample (whole curves)
PLAN = [
    # rear deck / tail  (x -151..13)
    ('Tail_Centre',        75, ('m', 75), 'x'),   # rear ledge inner  <-> its mirror
    ('Tail_Ledge',         75, 34, 'x'),          # ledge inner -> ledge outer
    ('Tail_Deck',          34, 60, 'x'),          # ledge outer -> deck inner
    ('Rear_Shoulder',      60, 27, 'x'),          # deck inner -> rear shoulder
    ('Rear_Quarter_Top',   27, 70, 'x'),          # rear shoulder -> y100 line
    ('Rear_Quarter_Arch',  70, 19, 'x'),          # y100 line -> rear arch rim
    ('Rear_Quarter_Sill',  70, 29, 'x'),          # y100 line -> sill crease (x -60..13)
    # cabin / doors (x 13..132)
    ('Cabin_Rim',          ('h', 62), 22, 'x'),   # cabin-opening rim (A-pillar/cant rail) -> bonnet edge
    ('Door_Top',           22, 63, 'x'),          # bonnet edge -> door crease
    ('Door_Sill',          63, 29, 'x'),          # door crease -> sill crease
    ('Sill',               29, 173, 'x'),         # sill crease -> sill bottom boundary
    # front fenders / nose (x 132..250)
    ('Fender_Top',         22, 64, 'x'),          # bonnet edge -> fender crease
    ('Fender_Arch',        64, 194, 'x'),         # fender crease -> front arch boundary
    ('Nose_Centre',        24, ('m', 24), 'x'),   # bonnet shoulder <-> mirror
    ('Nose_Side',          24, 25, 'x'),          # bonnet shoulder -> fender rim
    ('Nose_Arch',          25, 194, 'x'),         # fender rim -> arch boundary (x 134..208)
    # tail face (cross-body U curves, emitted once)
    ('Tail_Lip',           80, 85, 's'),          # tail lip -> tail top edge
    ('Tail_Face',          85, 90, 's'),          # tail top edge -> tail face bottom
]

out = ['# SolidArc native document v1', '# Liger — Phase 2c: pairwise lofts between fitted feature curves (curves.json)',
       '# Each STEP = one strip: two sections resampled on the common x-range, lofted as a sheet.',
       'show shading plastic', '']
report = []
step = 1
for name, a, b, mode in PLAN:
    def get(i):
        if isinstance(i, tuple): return mirror_y(C(i[1])) if i[0] == 'm' else half(C(i[1]))
        return C(i)
    A, B = get(a), get(b)
    centre = (isinstance(b, tuple) and b[0] == 'm') or mode == 's'
    cname = lambda i: curves[i[1]]['name'] + (' (mirror)' if i[0] == 'm' else ' (+Y half)') if isinstance(i, tuple) else curves[i]['name']
    if mode == 'x':
        x0 = max(A[:, 0].min(), B[:, 0].min()); x1 = min(A[:, 0].max(), B[:, 0].max())
        if x1 - x0 < 10:
            report.append(f'{name}: no x overlap ({x0:.0f}..{x1:.0f}) — skipped'); continue
        SA, SB = resample_x(A, x0, x1), resample_x(B, x0, x1)
    else:
        SA, SB = resample_s(A), resample_s(B)
        if np.linalg.norm(SA[0] - SB[0]) > np.linalg.norm(SA[0] - SB[-1]): SB = SB[::-1]
    gap = np.linalg.norm(SA - SB, axis=1)
    # orientation: kernel normal = (point tangent) x (section A->B); make it point away from the body axis
    m = len(SA) // 2; t = SA[m+1] - SA[m-1]; n = np.cross(t, SB[m] - SA[m])
    outward = (SA[m] + SB[m]) / 2 - np.array([(SA[m] + SB[m])[0] / 2, 0, 55])
    flip = np.dot(n, outward) < 0
    report.append(f'{name:18s} {cname(a)} -> {cname(b)}'
                  f'  x {SA[0,0]:.0f}..{SA[-1,0]:.0f}  width {gap.min():.0f}..{gap.max():.0f} cm')
    for side, sign in ((('C', 1),) if centre else (('R', 1), ('L', -1))):
        secs = []
        for k, S in enumerate((SA, SB), 1):
            S = S.copy(); S[:, 1] *= sign
            if sign < 0: S = S[::-1]
            pts = ' '.join(f'({q[0]*SCALE:.4f},{q[1]*SCALE:.4f},{q[2]*SCALE:.4f})' for q in S)
            nm = f'{side}_{name}_c{k}'; secs.append(nm)
            out.append(f'spline {pts} --name={nm}')
        if flip: secs = secs[::-1]                # negated y + reversed point order cancel out, so same rule both sides
        out.append(f'# STEP {step} — {side}_{name}')
        out.append(f'loft {" ".join(secs)} --sheet --name={side}_{name}')
        out.append(f'delete {" ".join(secs)}')
        out.append(f'tint {side}_{name} {TINT}')
        out.append('')
        step += 1

(OUT / f'{CAR}_Body_CurveLoft.arc').write_text('\n'.join(out) + '\n')
print('\n'.join(report)); print(f'{step-1} strips -> {OUT}/{CAR}_Body_CurveLoft.arc')

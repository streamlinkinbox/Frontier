#!/usr/bin/env python3
"""Fit the extracted feature polylines as smooth splines and write a step-journaled
SolidArc document that uses the console's `spline` (interpolating) command.

  ~/.venv/bin/python Vehicles/tools/fit_curves.py Liger

Method (per polyline, in cm):
  1. smooth the samples lightly (silhouettes are pixel contours -> 3-tap moving average)
  2. split at corners: RDP-simplify, cut where the turning angle exceeds CORNER_DEG
  3. per piece, adaptive interpolation: start with the two endpoints, fit a chord-length
     parameterised natural cubic spline through the chosen points, insert the sample of
     maximum deviation, repeat until max deviation < TOL (or the point budget is spent)
  4. emit `spline (x,y,z) ... --name=...`; closed loops get `--closed`
Outputs:
  Vehicles/<Car>/<Car>_Body_Curves.arc   the journal (metres)
  Vehicles/<Car>/curves.json             fitted interpolation points + fit statistics
  Vehicles/<Car>/<Car>_Body_Curves.png   proof render + deviation histogram
"""
import sys, os, json, math
import numpy as np
from scipy.interpolate import CubicSpline

CAR = sys.argv[1]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
D = json.load(open(f'{ROOT}/Vehicles/{CAR}/features.json'))
OUTDIR = f'{ROOT}/Vehicles/{CAR}'
TOL, TOL_SIL = 0.35, 0.7      # cm max deviation for feature curves / silhouettes
CORNER_DEG = 48               # split curves at corners sharper than this
MIN_LEN = 4.0                 # cm, ignore fragments
MAX_PTS = 40                  # interpolation points per piece
SCALE = 0.01                  # cm -> m
GAP = 25.0
main = 'Body_Main_Shell'
bb = D['parts'][main]['bbox']; yc = (bb[0][1] + bb[1][1]) / 2.0
lo = [bb[0][0], bb[0][1] - yc, bb[0][2]]; hi = [bb[1][0], bb[1][1] - yc, bb[1][2]]
for p in D['parts'].values():
    for i in range(3):
        o = yc if i == 1 else 0
        lo[i] = min(lo[i], p['bbox'][0][i] - o); hi[i] = max(hi[i], p['bbox'][1][i] - o)

# ------------------------------------------------------------------ geometry helpers
def chord(P):
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]
    return d

def rdp_idx(P, tol):
    """indices kept by Douglas-Peucker"""
    keep = [0, len(P) - 1]; stack = [(0, len(P) - 1)]
    while stack:
        a, b = stack.pop()
        if b - a < 2: continue
        ab = P[b] - P[a]; L = np.linalg.norm(ab)
        if L < 1e-9: d = np.linalg.norm(P[a + 1:b] - P[a], axis=1)
        else:
            t = np.clip(((P[a + 1:b] - P[a]) @ ab) / (L * L), 0, 1)
            d = np.linalg.norm(P[a + 1:b] - P[a] - t[:, None] * ab, axis=1)
        i = int(np.argmax(d))
        if d[i] > tol: keep.append(a + 1 + i); stack += [(a, a + 1 + i), (a + 1 + i, b)]
    return sorted(set(keep))

def corners(P, tol, deg):
    ks = rdp_idx(P, tol); out = []
    for j in range(1, len(ks) - 1):
        a, b, c = P[ks[j - 1]], P[ks[j]], P[ks[j + 1]]
        u, v = b - a, c - b
        if np.linalg.norm(u) < 1e-9 or np.linalg.norm(v) < 1e-9: continue
        ang = math.degrees(math.acos(np.clip(u @ v / np.linalg.norm(u) / np.linalg.norm(v), -1, 1)))
        if ang > deg: out.append(ks[j])
    return out

def smooth(P, closed):
    if len(P) < 5: return P
    if closed:
        Q = np.vstack([P[-1:], P, P[:1]]); return (Q[:-2] + 2 * Q[1:-1] + Q[2:]) / 4
    Q = P.copy(); Q[1:-1] = (P[:-2] + 2 * P[1:-1] + P[2:]) / 4; return Q

def dedupe(P, eps=1e-4):
    keep = [0] + [i for i in range(1, len(P)) if np.linalg.norm(P[i] - P[i - 1]) > eps]
    return P[keep]

def spline_through(Q, closed):
    Q = dedupe(Q)
    if closed and len(Q) > 1 and np.linalg.norm(Q[0] - Q[-1]) > 1e-4: Q = np.vstack([Q, Q[:1]])
    if len(Q) < (4 if closed else 2): Q = np.vstack([Q, Q[:1]]) if closed else Q
    t = chord(Q)
    if t[-1] < 1e-9 or len(Q) < 2: return None
    return CubicSpline(t, Q, bc_type='periodic' if closed else 'natural', axis=0), t

def deviation(P, Q, closed):
    """max/mean distance of samples P from the spline through Q (dense evaluation)"""
    r = spline_through(Q, closed)
    if r is None: return np.zeros(len(P))
    cs, t = r
    S = cs(np.linspace(0, t[-1], max(200, 12 * len(Q))))
    # nearest dense-sample distance
    d = np.min(np.linalg.norm(P[:, None, :] - S[None, :, :], axis=2), axis=1)
    return d

def adaptive_fit(P, closed, tol):
    n = len(P)
    if closed:
        idx = sorted({0, n // 3, 2 * n // 3})          # periodic needs >= 3 distinct + repeat
    else:
        idx = [0, n - 1]
    for _ in range(MAX_PTS):
        Q = P[idx]
        d = deviation(P, Q, closed)
        if d.max() < tol: break
        i = int(np.argmax(d))
        if i in idx:                                    # numerical stall: add a neighbour instead
            cand = [k for k in range(n) if k not in idx]
            if not cand: break
            i = cand[int(np.argmax(d[cand]))]
        idx = sorted(set(idx + [i]))
    return idx, d

# ------------------------------------------------------------------ fitting
curves = []   # dict(name, pts(cm, world Y recentred), closed, kind, part, max_dev, mean_dev, nsamples)
stats = {}
def fit_group(polylines, kind, part, prefix, tol, pretransform=None):
    k = 0
    for L in polylines:
        P = np.array(L, 'f8')
        if P.shape[1] == 2: P = np.array([pretransform(u, v) for u, v in P], 'f8')
        else: P[:, 1] -= yc
        closed = len(P) > 3 and np.linalg.norm(P[0] - P[-1]) < 1e-3
        if closed: P = P[:-1]
        P = dedupe(P)
        if len(P) < 2 or chord(P)[-1] < MIN_LEN: continue
        P = smooth(P, closed)
        cuts = corners(P, tol * 2, CORNER_DEG)
        if closed and cuts:                              # rotate so a corner is the start, treat as open pieces
            P = np.roll(P, -cuts[0], axis=0); cuts = [c - cuts[0] for c in cuts[1:]]; P = np.vstack([P, P[:1]]); closed = False
        bounds = [0] + cuts + [len(P) - 1]
        pieces = [P[bounds[i]:bounds[i + 1] + 1] for i in range(len(bounds) - 1)] if not closed else [P]
        for piece in pieces:
            if len(piece) < 2 or chord(piece)[-1] < MIN_LEN * 0.5: continue
            idx, d = adaptive_fit(piece, closed, tol)
            k += 1
            curves.append(dict(name=f'{prefix}_{k:03d}', part=part, kind=kind, closed=bool(closed),
                               pts=piece[idx].round(3).tolist(), max_dev=float(d.max()), mean_dev=float(d.mean()), nsamples=int(len(piece))))
    return k

RED, BLUE, BLACK, GREY = '0.85 0.10 0.10', '0.15 0.35 1.00', '0.05 0.05 0.05', '0.45 0.45 0.50'
steps = []   # (title, [curve names], tint)
def step(title, tint, fn):
    before = len(curves); n = fn(); steps.append((title, [c['name'] for c in curves[before:]], tint)); return n

step(f'{main}: crease curves fitted as splines (3D)', RED, lambda: fit_group(D['parts'][main]['lines']['CREASE'], 'CREASE', main, 'Shell_Crease', TOL))
step(f'{main}: panel boundary curves fitted as splines (3D)', BLUE, lambda: fit_group(D['parts'][main]['lines']['BOUNDARY'], 'BOUNDARY', main, 'Shell_Edge', TOL))
for part in D['parts']:
    if part == main: continue
    short = part.replace('Body_', '')
    step(f'{part}: creases + boundaries as splines', RED, lambda part=part, short=short: fit_group(D['parts'][part]['lines']['CREASE'], 'CREASE', part, short + '_Crease', TOL))
    step(f'{part}: boundaries as splines', BLUE, lambda part=part, short=short: fit_group(D['parts'][part]['lines']['BOUNDARY'], 'BOUNDARY', part, short + '_Edge', TOL))
planes = {
    'side':   lambda u, v: (u, lo[1] - GAP, v),
    'top':    lambda u, v: (u, v - yc, hi[2] + GAP),
    'bottom': lambda u, v: (u, -v - yc, lo[2] - GAP),
    'front':  lambda u, v: (hi[0] + GAP, -u, v),
    'rear':   lambda u, v: (lo[0] - GAP, u, v),
}
for view, f in planes.items():
    step(f'{view.upper()} view silhouette as splines on its blueprint plane', BLACK,
         lambda view=view, f=f: fit_group(D['views'][view]['silhouette'], 'SILHOUETTE', 'view_' + view, 'View_' + view.capitalize(), TOL_SIL, f))

# ------------------------------------------------------------------ write journal
out = ['# SolidArc native document v1',
       f'# {CAR} — exterior body feature curves fitted as interpolating splines (`spline`): creases, panel boundaries, five silhouettes.',
       '# Right-handed, Z up, metres. +X front, mirrored about Y=0. Each line is one operation; replay = rebuild.',
       '# Fit: chord-length cubic through adaptively chosen points; max deviation from the Blender surface per curve is in curves.json.',
       'reset']
byname = {c['name']: c for c in curves}
for i, (title, names, tint) in enumerate(steps, 1):
    out.append(f'# STEP {i} — {title}  ({len(names)} curves)')
    for n in names:
        c = byname[n]
        pts = ' '.join(f'({x*SCALE:.4f},{y*SCALE:.4f},{z*SCALE:.4f})' for x, y, z in c['pts'])
        cmd = 'spline' if len(c['pts']) > 2 else 'line'
        out.append(f'{cmd} {pts}{" --closed" if c["closed"] else ""} --name={n}')
        out.append(f'tint {n} {tint}')
out.append(f'# STEP {len(steps) + 1} — ground frame')
g = lambda *c: '(%.4f,%.4f,%.4f)' % tuple(v * SCALE for v in c)
out.append(f'polyline {g(lo[0], lo[1]-GAP, lo[2]-GAP)} {g(hi[0]+GAP, lo[1]-GAP, lo[2]-GAP)} {g(hi[0]+GAP, hi[1], lo[2]-GAP)} {g(lo[0], hi[1], lo[2]-GAP)} --closed --name=Frame_Ground')
out.append(f'tint Frame_Ground {GREY}')
open(f'{OUTDIR}/{CAR}_Body_Curves.arc', 'w').write('\n'.join(out) + '\n')
json.dump(dict(car=CAR, units='cm', mirror_offset_applied=yc, tolerance_cm=dict(feature=TOL, silhouette=TOL_SIL), curves=curves),
          open(f'{OUTDIR}/curves.json', 'w'))

# ------------------------------------------------------------------ report + proof render
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
npts = sum(len(c['pts']) for c in curves); nsamp = sum(c['nsamples'] for c in curves)
print(f'{CAR}: {len(curves)} splines from {nsamp} mesh samples -> {npts} interpolation points ({nsamp/max(npts,1):.1f}x reduction)')
for kind in ('CREASE', 'BOUNDARY', 'SILHOUETTE'):
    cs = [c for c in curves if c['kind'] == kind]
    if cs: print(f'  {kind:10s} n={len(cs):3d}  pts/curve={np.mean([len(c["pts"]) for c in cs]):.1f}  max dev={max(c["max_dev"] for c in cs):.2f} cm  mean dev={np.mean([c["mean_dev"] for c in cs]):.3f} cm')
fig = plt.figure(figsize=(22, 15), dpi=110)
axI = fig.add_subplot(2, 2, (1, 2)); axS = fig.add_subplot(2, 2, 3); axT = fig.add_subplot(2, 2, 4)
def draw(ax, proj, names_filter, lw_scale=1.0):
    for c in curves:
        if not names_filter(c): continue
        Q = np.array(c['pts']); closed = c['closed']
        if len(Q) > 2:
            cs, t = spline_through(np.vstack([Q, Q[:1]]) if closed else Q, closed); S = cs(np.linspace(0, t[-1], 20 * len(Q)))
        else: S = Q
        col = {'CREASE': '#d01010', 'BOUNDARY': '#2050ff', 'SILHOUETTE': 'k'}[c['kind']]
        u, v = proj(S); ax.plot(u, v, color=col, lw=(0.6 if c['kind'] == 'CREASE' else 1.1) * lw_scale)
        u, v = proj(Q); ax.plot(u, v, '.', color=col, ms=2.2 * lw_scale, alpha=0.7)
a = math.radians(30)
iso = lambda S: ((S[:, 0] - S[:, 1]) * math.cos(a), -(S[:, 0] + S[:, 1]) * math.sin(a) * 0.55 + S[:, 2] * 0.9)
draw(axI, iso, lambda c: True)
draw(axS, lambda S: (S[:, 0], S[:, 2]), lambda c: c['part'] in D['parts'] or c['part'] == 'view_side', 1.3)
draw(axT, lambda S: (S[:, 0], S[:, 1]), lambda c: c['part'] in D['parts'] or c['part'] == 'view_top', 1.3)
for ax, t in ((axI, 'isometric — fitted splines (dots = interpolation points the journal stores)'), (axS, 'SIDE — silhouette spline + 3D feature splines'), (axT, 'TOP')):
    ax.set_aspect('equal'); ax.grid(alpha=.25); ax.set_title(t)
fig.suptitle(f'{CAR}_Body_Curves.arc — {len(curves)} splines, {npts} points, max deviation {max(c["max_dev"] for c in curves):.2f} cm', fontsize=14)
plt.tight_layout(); fig.savefig(f'{OUTDIR}/{CAR}_Body_Curves.png'); print('wrote', OUTDIR)

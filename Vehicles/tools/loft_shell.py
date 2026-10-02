#!/usr/bin/env python3
"""Phase 2 — rebuild the Liger main shell as lofted NURBS sheets in SolidArc.

  ~/.venv/bin/python Vehicles/tools/loft_shell.py Liger

Station sections (planes x = const) are cut from the evaluated Blender mesh, the main
open profile (sill/arch-lip -> roof -> other side) is kept, resampled by arc length to
N points, fitted as a `spline`, and consecutive sections are `loft`ed.  The body is
split into segments at the design's natural stations so each sheet has a coherent
lower edge; wheel-arch and panel-boundary splines are added as the trim network.

Outputs Vehicles/<Car>/<Car>_Body_Surface.arc (journal), surface.json (section grid),
<Car>_Body_Surface.png (proof: shaded loft in iso + side + top, deviation report).
"""
import sys, os, json, math
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from sections import cut_x, length
from scipy.interpolate import CubicSpline

CAR = sys.argv[1]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUTDIR = f'{ROOT}/Vehicles/{CAR}'
MESH = os.environ.get('MESH', '/home/user/mesh')
d = np.load(f'{MESH}/Body_Main_Shell.npz'); V, T = d['V'].astype('f8'), d['T']
yc = (V[:, 1].min() + V[:, 1].max()) / 2; V[:, 1] -= yc
SCALE = 0.01; N = 2 * (8 + 12) + 1                          # points per section (odd -> a point on the centreline)

# segments: (name, x_start, x_end, spacing)  -- Liger stations, cm
SEGMENTS = [('Tail', -160, -132, 9.3), ('RearArch', -132, -68, 8), ('Cabin', -68, 112, 10),
            ('FrontArch', 112, 205, 9.3), ('Nose', 205, 246, 10.25)]

def main_profile(x):
    ls = cut_x(V, T, x)
    ls = [L for L in ls if np.linalg.norm(L[0] - L[-1]) > 5]       # open chains only
    if not ls: return None
    L = max(ls, key=length)
    if L[0, 1] > L[-1, 1]: L = L[::-1]                                 # run -Y -> +Y
    return L

def resample(L, n):
    s = np.r_[0, np.cumsum(np.linalg.norm(np.diff(L, axis=0), axis=1))]
    u = np.linspace(0, s[-1], n)
    return np.column_stack([np.interp(u, s, L[:, k]) for k in range(3)])

NF, NT = 8, 12            # points per flank (end -> shoulder) and per half-top (shoulder -> centre); N = 2*(NF+NT)+1
def resample_landmarks(L):
    """anchor the parameterisation at the shoulders (widest point of each half) so U-lines stay aligned"""
    m = len(L); c = int(np.argmin(np.abs(L[:, 1])))                      # centreline crossing
    left, right = L[:c + 1], L[c:]
    il = int(np.argmin(left[:, 1])); ir = int(np.argmax(right[:, 1]))     # most -Y / most +Y
    parts = [resample(left[:il + 1], NF + 1)[:-1], resample(left[il:], NT + 1),
             resample(right[:ir + 1], NT + 1)[1:], resample(right[ir:], NF + 1)[1:]]
    return np.vstack(parts)

def symmetrise(S):
    """average with the mirrored, reversed section so the loft is exactly mirror-symmetric"""
    M = S[::-1].copy(); M[:, 1] *= -1
    return (S + M) / 2

grid = {}; stations = {}
for name, x0, x1, dx in SEGMENTS:
    xs = np.linspace(x0, x1, int(round((x1 - x0) / dx)) + 1)
    secs = []
    for x in xs:
        L = main_profile(x)
        if L is None or length(L) < 30: continue
        secs.append(symmetrise(resample_landmarks(L)))
    grid[name] = np.array(secs); stations[name] = xs[:len(secs)].tolist()
    print(f'{name:10s} {len(secs)} sections  x {x0}..{x1}')

# ---------------------------------------------------------------- deviation check (loft ~ interpolation between sections)
def loft_eval(secs, xs, x):
    i = np.searchsorted(xs, x) - 1; i = max(0, min(i, len(xs) - 2)); t = (x - xs[i]) / (xs[i + 1] - xs[i])
    return (1 - t) * secs[i] + t * secs[i + 1]
devs = []
for name, _, _, _ in SEGMENTS:
    xs = stations[name]; secs = grid[name]
    for i in range(len(xs) - 1):
        xm = (xs[i] + xs[i + 1]) / 2; chains = cut_x(V, T, xm)
        if not chains: continue
        S = loft_eval(secs, xs, xm); Lm = np.vstack([resample(Lc, max(8, int(length(Lc)))) for Lc in chains])
        dd = np.min(np.linalg.norm(S[:, None, 1:] - Lm[None, :, 1:], axis=2), axis=1)   # loft point -> mesh section
        devs.append((name, xm, float(dd.max()), float(dd.mean())))
worst = max(devs, key=lambda r: r[2])
print(f'loft surface -> mesh at mid-stations: max {worst[2]:.2f} cm ({worst[0]} x={worst[1]:.0f}), mean {np.mean([r[3] for r in devs]):.3f} cm')

# ---------------------------------------------------------------- journal
curves = json.load(open(f'{OUTDIR}/curves.json'))['curves']
out = ['# SolidArc native document v1',
       f'# {CAR} — main shell rebuilt as lofted NURBS sheets from station sections of the Blender design; arch/panel splines as trim network.',
       '# Right-handed, Z up, metres. +X front, mirrored about Y=0. Each line is one operation; replay = rebuild.',
       'reset']
step = 1; SHELL = '0.80 0.82 0.86'
for name, _, _, _ in SEGMENTS:
    secs = grid[name]; xs = stations[name]
    out.append(f'# STEP {step} — {name}: {len(secs)} station sections (x = {xs[0]:.0f} .. {xs[-1]:.0f} cm), {N}-point splines')
    names = []
    for k, S in enumerate(secs, 1):
        # thin each section to the points a spline needs (every 2nd point, keep ends and centre)
        keep = sorted(set(list(range(0, N, 2)) + [N - 1, N // 2]))
        pts = ' '.join(f'({p[0]*SCALE:.4f},{p[1]*SCALE:.4f},{p[2]*SCALE:.4f})' for p in S[keep])
        nm = f'Sec_{name}_{k:02d}'; names.append(nm)
        out.append(f'spline {pts} --name={nm}')
    step += 1
    out.append(f'# STEP {step} — {name}: loft the sections into one sheet, then drop the construction sections')
    out.append(f'loft {" ".join(names)} --sheet --name=Shell_{name}')
    out.append(f'delete {" ".join(names)}')
    out.append(f'tint Shell_{name} {SHELL}')
    step += 1
out.append(f'# STEP {step} — trim network: wheel-arch / panel-boundary splines (blue) and creases (red) from {CAR}_Body_Curves.arc')
for c in curves:
    if c['part'] != 'Body_Main_Shell': continue
    pts = ' '.join(f'({x*SCALE:.4f},{y*SCALE:.4f},{z*SCALE:.4f})' for x, y, z in c['pts'])
    cmd = 'spline' if len(c['pts']) > 2 else 'line'
    out.append(f'{cmd} {pts}{" --closed" if c["closed"] else ""} --name={c["name"]}')
    out.append(f'tint {c["name"]} {"0.15 0.35 1.00" if c["kind"] == "BOUNDARY" else "0.85 0.10 0.10"}')
open(f'{OUTDIR}/{CAR}_Body_Surface.arc', 'w').write('\n'.join(out) + '\n')
json.dump(dict(car=CAR, units='cm', points_per_section=N, segments={n: dict(x=stations[n], sections=grid[n].round(3).tolist()) for n in grid},
               deviation=devs), open(f'{OUTDIR}/surface.json', 'w'))
nops = sum(1 for l in out if not l.startswith('#'))
print(f'journal: {nops} operations, {step} steps, {sum(len(g) for g in grid.values())} sections, {len(SEGMENTS)} lofts')

# ---------------------------------------------------------------- proof render (painter's algorithm, shaded quads)
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
def shaded(ax, cam, light=(0.4, -0.6, 0.7), title=''):
    """cam: 3x3 rows = (right, up, toward-viewer) unit vectors"""
    R = np.array(cam, 'f8'); Lv = np.array(light) / np.linalg.norm(light)
    quads, depth, cols = [], [], []
    for name in grid:
        G = grid[name]
        A, B, C, D = G[:-1, :-1], G[1:, :-1], G[1:, 1:], G[:-1, 1:]
        n = np.cross(C - A, D - B); n /= (np.linalg.norm(n, axis=2, keepdims=True) + 1e-9)
        lam = np.abs(n @ Lv)
        Q = np.stack([A, B, C, D], axis=2).reshape(-1, 4, 3)
        P = Q @ R.T; quads.append(P[:, :, :2]); depth.append(P[:, :, 2].mean(1))
        cols.append(np.clip(0.35 + 0.6 * lam.reshape(-1), 0, 1))
    quads = np.vstack(quads); depth = np.concatenate(depth); cols = np.concatenate(cols)
    o = np.argsort(depth)
    rgb = np.column_stack([cols * 0.86, cols * 0.88, cols * 0.93])[o]
    ax.add_collection(PolyCollection(quads[o], facecolors=rgb, edgecolors=rgb * 0.85, linewidths=0.25))
    for c in curves:
        if c['part'] != 'Body_Main_Shell': continue
        Q = np.array(c['pts']) @ R.T; col = '#2050ff' if c['kind'] == 'BOUNDARY' else '#d01010'
        ax.plot(Q[:, 0], Q[:, 1], color=col, lw=1.0 if c['kind'] == 'BOUNDARY' else 0.5)
    ax.autoscale_view(); ax.set_aspect('equal'); ax.set_axis_off(); ax.set_title(title)
def camera(az, el):
    az, el = math.radians(az), math.radians(el)
    f = np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)])   # toward viewer
    r = np.cross([0, 0, 1], f); r /= np.linalg.norm(r); u = np.cross(f, r)
    return [r, u, f]
fig, axs = plt.subplots(2, 2, figsize=(24, 15), dpi=110)
shaded(axs[0, 0], camera(-50, 24), title='front three-quarter')
shaded(axs[0, 1], camera(135, 26), title='rear three-quarter')
shaded(axs[1, 0], camera(-90, 2), title='side')
shaded(axs[1, 1], camera(-90, 89), title='plan')
fig.suptitle(f'{CAR}_Body_Surface.arc — {len(SEGMENTS)} lofted sheets from {sum(len(g) for g in grid.values())} station sections; '
             f'loft surface -> mesh: max {worst[2]:.2f} cm, mean {np.mean([r[3] for r in devs]):.2f} cm   (blue = arch/panel trim splines, red = creases)', fontsize=13)
plt.tight_layout(); fig.savefig(f'{OUTDIR}/{CAR}_Body_Surface.png'); print('wrote', OUTDIR)

#!/usr/bin/env python3
"""Phase 2b — panel-by-panel NURBS rebuild of the Liger main shell.

  ~/.venv/bin/python Vehicles/tools/panel_loft.py Liger

The body is divided in x at the ends of the long longitudinal crease curves (the design's
own lines).  Inside each x-interval the +Y half-section at every station is split exactly
where the active crease curves cross it; each strip between two crease lines is resampled,
fitted as a `spline` and lofted on its own.  The −Y half is produced with `mirror`.
Every patch is checked against the Blender mesh (loft point -> mesh distance).

Outputs Vehicles/<Car>/<Car>_Body_Panels.arc, panels.json, <Car>_Body_Panels.png
"""
import sys, os, json, math
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from sections import cut_x, length

CAR = sys.argv[1]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUTDIR = f'{ROOT}/Vehicles/{CAR}'
d = np.load(f'{OUTDIR}/mesh/Body_Main_Shell.npz'); V, T = d['V'].astype('f8'), d['T']
yc = (V[:, 1].min() + V[:, 1].max()) / 2; V[:, 1] -= yc
F = json.load(open(f'{OUTDIR}/features.json'))['parts']['Body_Main_Shell']['lines']
SCALE = 0.01
STATION = 7.0          # cm between stations inside an interval
SNAP = 3.0             # cm: a crease crossing must lie this close to the section to split it
PT_SPACING = 6.0       # cm between spline points along a strip
X0, X1 = -152.0, 246.0

# ---------------------------------------------------------------- landmark curves (+Y side, longitudinal)
# indices into features CREASE list, chosen from the curve table (see README)
LANDMARKS = {'SillCrease': 34, 'DoorCrease': 78, 'BonnetEdge': 23, 'FenderCrease': 79, 'BonnetShoulder': 25,
             'RearShoulder': 30, 'RearDeckInner': 75, 'RearLedgeOuter': 41, 'RearLedgeInner': 97, 'FrontSill': 22}
LM = {}
for name, i in LANDMARKS.items():
    P = np.array(F['CREASE'][i]); P[:, 1] -= yc
    if P[:, 1].mean() < 0: P[:, 1] *= -1
    LM[name] = P[np.argsort(P[:, 0])] if abs(P[-1, 0] - P[0, 0]) > 0 else P

def cross(P, x):
    """points where polyline P crosses plane x (linear)"""
    out = []
    for a, b in zip(P[:-1], P[1:]):
        if (a[0] - x) * (b[0] - x) <= 0 and a[0] != b[0]:
            t = (x - a[0]) / (b[0] - a[0]); out.append(a + t * (b - a))
    return out

# x breakpoints: ends of landmark curves (merged within 8 cm) + body ends
bp = sorted({X0, X1} | {float(P[:, 0].min()) for P in LM.values()} | {float(P[:, 0].max()) for P in LM.values()})
merged = [bp[0]]
for b in bp[1:]:
    if b - merged[-1] < 8: merged[-1] = (merged[-1] + b) / 2 if merged[-1] not in (X0, X1) else merged[-1]
    else: merged.append(b)
if merged[-1] != X1: merged[-1] = X1
INTERVALS = list(zip(merged[:-1], merged[1:]))

def half_profile(x):
    ls = [L for L in cut_x(V, T, x) if np.linalg.norm(L[0] - L[-1]) > 5]
    if not ls: return None
    L = max(ls, key=length)
    if L[0, 1] > L[-1, 1]: L = L[::-1]
    c = int(np.argmin(np.abs(L[:, 1])))                      # centreline
    H = L[c:].copy(); H[0, 1] = 0.0                           # centre -> +Y end
    return H

def arc(P): return np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]

def resample(P, n):
    s = arc(P); u = np.linspace(0, s[-1], n)
    return np.column_stack([np.interp(u, s, P[:, k]) for k in range(3)])

def nearest_param(H, q):
    """arc-length parameter on H closest to point q (yz distance)"""
    d = np.linalg.norm(H[:, 1:] - q[1:], axis=1); i = int(np.argmin(d))
    return arc(H)[i], d[i]

# refine intervals: split where the half-profile end (z) jumps by more than JUMP cm between fine stations
JUMP = 12.0
fine = np.arange(X0 + 0.5, X1, 3.0); ends = {}
for x in fine:
    H = half_profile(x); ends[x] = H[-1, 2] if H is not None else None
refined = []
for xa, xb in INTERVALS:
    cuts = [xa]
    xs_in = [x for x in fine if xa < x < xb]
    for a, b in zip(xs_in[:-1], xs_in[1:]):
        if ends[a] is not None and ends[b] is not None and abs(ends[a] - ends[b]) > JUMP and (a + b) / 2 - cuts[-1] > 6 and xb - (a + b) / 2 > 6:
            cuts.append((a + b) / 2)
    cuts.append(xb); refined += list(zip(cuts[:-1], cuts[1:]))
INTERVALS = refined

patches = []      # dict(name, interval, strip, sections[list of (n,3)], landmarks)
report = []
for (xa, xb) in INTERVALS:
    active = [n for n, P in LM.items() if P[:, 0].min() <= xa + 3 and P[:, 0].max() >= xb - 3]
    nst = max(2, int(round((xb - xa) / STATION)) + 1)
    xs = np.linspace(xa + 0.3, xb - 0.3, nst)
    rows = []                                                 # per station: (H, [(name, s)])
    for x in xs:
        H = half_profile(x)
        if H is None: continue
        s_tot = arc(H)[-1]; marks = []
        for n in active:
            cs = cross(LM[n], x)
            if not cs: continue
            s, dist = nearest_param(H, cs[0])
            if dist < SNAP and 2 < s < s_tot - 2: marks.append((n, s))
        rows.append((x, H, marks))
    # keep only landmarks found at every station of the interval
    common = [n for n in active if all(any(m[0] == n for m in r[2]) for r in rows)]
    strips_per_station = []
    for x, H, marks in rows:
        ss = sorted([s for n, s in marks if n in common]); S = arc(H)
        bounds = [0.0] + ss + [S[-1]]
        strips = []
        for a, b in zip(bounds[:-1], bounds[1:]):
            u = np.linspace(a, b, 60)
            strips.append(np.column_stack([np.interp(u, S, H[:, k]) for k in range(3)]))
        strips_per_station.append((x, strips))
    names_sorted = sorted(common, key=lambda n: np.mean([dict(m)[n] for _, _, m in rows if n in dict(m)]))
    labels = ['Centre'] + names_sorted + ['End']
    # outermost strip: the profile end height varies (sill vs arch lip vs nose legs) -> cut all stations at the
    # highest end height of the interval; what hangs below becomes separate 'Leg' patches where present
    legs = None
    if strips_per_station:
        ends_z = [st[-1][-1, 2] for _, st in strips_per_station]
        zcut = max(ends_z)
        if max(ends_z) - min(ends_z) > 6:
            legs = []
            for x, st in strips_per_station:
                S = st[-1]; S2 = resample(S, 200)
                below = np.where(S2[:, 2] < zcut - 1.0)[0]
                # first index after which the strip stays below zcut
                idx = len(S2) - 1
                for i in range(len(S2) - 1, -1, -1):
                    if S2[i, 2] >= zcut - 1.0: idx = i; break
                st[-1] = S2[:idx + 1] if idx > 3 else S2[:4]
                legs.append(S2[idx:] if len(S2) - idx > 3 and arc(S2[idx:])[-1] > 4 else None)
    for k in range(len(labels) - 1):
        secs = [st[k] for _, st in strips_per_station]
        mean_len = np.mean([arc(S)[-1] for S in secs])
        if mean_len < 2.5: continue
        n = int(max(4, min(24, round(mean_len / PT_SPACING) + 2)))
        secs = [resample(S, n) for S in secs]
        name = f'P{len(patches)+1:02d}_{labels[k]}_{labels[k+1]}_x{int(xa)}'
        patches.append(dict(name=name, x=[float(xa), float(xb)], between=[labels[k], labels[k + 1]],
                            stations=[float(x) for x, _ in strips_per_station], sections=secs))
    if legs:
        run = []
        for (x, _), L in zip(strips_per_station + [(None, None)], legs + [None]):
            if L is not None: run.append((x, L)); continue
            if len(run) >= 2:
                n = int(max(4, min(24, round(np.mean([arc(S)[-1] for _, S in run]) / PT_SPACING) + 2)))
                name = f'P{len(patches)+1:02d}_Leg_x{int(run[0][0])}'
                patches.append(dict(name=name, x=[float(run[0][0]), float(run[-1][0])], between=['End', 'Floor'],
                                    stations=[float(x) for x, _ in run], sections=[resample(S, n) for _, S in run]))
            run = []
    report.append((xa, xb, len(rows), common))
for r in report: print(f'interval x {r[0]:6.0f}..{r[1]:6.0f}  stations {r[2]:2d}  split at {r[3]}')
print(len(patches), 'patches')

# ---------------------------------------------------------------- deviation per patch (loft point -> mesh)
def dev(patch):
    xs = patch['stations']; secs = patch['sections']; worst = 0; means = []
    for i in range(len(xs) - 1):
        xm = (xs[i] + xs[i + 1]) / 2; chains = cut_x(V, T, xm)
        if not chains: continue
        M = np.vstack([resample(c, max(8, int(length(c)))) for c in chains])
        S = (secs[i] + secs[i + 1]) / 2
        dd = np.min(np.linalg.norm(S[:, None, 1:] - M[None, :, 1:], axis=2), axis=1)
        worst = max(worst, dd.max()); means.append(dd.mean())
    return float(worst), float(np.mean(means) if means else 0)
for p in patches: p['max_dev'], p['mean_dev'] = dev(p)
bad = [p for p in patches if p['max_dev'] > 1.5]
print(f'deviation: mean {np.mean([p["mean_dev"] for p in patches]):.2f} cm, worst {max(p["max_dev"] for p in patches):.2f} cm; {len(bad)} patches > 1.5 cm:')
for p in bad: print(f'   {p["name"]:40s} max {p["max_dev"]:.1f} mean {p["mean_dev"]:.2f}')

# ---------------------------------------------------------------- journal
out = ['# SolidArc native document v1',
       f'# {CAR} — main shell rebuilt panel by panel: strips bounded by the design crease curves, lofted per x-interval; −Y half by mirror.',
       '# Right-handed, Z up, metres. +X front, mirrored about Y=0. Each line is one operation; replay = rebuild.',
       'reset']
step = 1; TINT = '0.80 0.82 0.86'
for side, sign, tag in (('+Y', 1, 'R'), ('-Y', -1, 'L')):
    for p in patches:
        out.append(f'# STEP {step} — {tag}_{p["name"]}: {len(p["sections"])} sections between {p["between"][0]} and {p["between"][1]}, x {p["x"][0]:.0f}..{p["x"][1]:.0f} cm ({side} side)')
        names = []
        for k, S in enumerate(p['sections'], 1):
            S = S[::-1].copy(); S[:, 1] *= sign                 # +Y: run outer -> centre; -Y: mirrored points, same order flips orientation back
            if sign < 0: S = S[::-1]
            pts = ' '.join(f'({q[0]*SCALE:.4f},{q[1]*SCALE:.4f},{q[2]*SCALE:.4f})' for q in S)
            nm = f'{tag}_{p["name"]}_s{k:02d}'; names.append(nm)
            out.append(f'spline {pts} --name={nm}')
        out.append(f'loft {" ".join(names)} --sheet --name={tag}_{p["name"]}')
        out.append(f'delete {" ".join(names)}')
        out.append(f'tint {tag}_{p["name"]} {TINT}')
        step += 1
open(f'{OUTDIR}/{CAR}_Body_Panels.arc', 'w').write('\n'.join(out) + '\n')
json.dump(dict(car=CAR, units='cm', intervals=INTERVALS, landmarks=list(LANDMARKS), patches=[
    dict(name=p['name'], x=p['x'], between=p['between'], stations=p['stations'], max_dev=p['max_dev'], mean_dev=p['mean_dev'],
         sections=[s.round(3).tolist() for s in p['sections']]) for p in patches]), open(f'{OUTDIR}/panels.json', 'w'))
print('journal:', sum(1 for l in out if not l.startswith('#')), 'operations', step, 'steps')

# ---------------------------------------------------------------- proof: original vs rebuild, same camera
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
def camera(az, el):
    az, el = math.radians(az), math.radians(el)
    f = np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)]); r = np.cross([0, 0, 1], f); r /= np.linalg.norm(r)
    return np.array([r, np.cross(f, r), f])
def paint(ax, quads, R, light=(0.3, -0.5, 0.8), tintc=(0.85, 0.87, 0.92), lw=0.0):
    Lv = np.array(light) / np.linalg.norm(light)
    n = np.cross(quads[:, 2] - quads[:, 0], quads[:, 3] - quads[:, 1]); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
    lam = np.abs(n @ Lv); P = quads @ R.T; o = np.argsort(P[:, :, 2].mean(1)); c = np.clip(0.3 + 0.65 * lam, 0, 1)[o]
    rgb = np.column_stack([c * tintc[0], c * tintc[1], c * tintc[2]])
    ax.add_collection(PolyCollection(P[o][:, :, :2], facecolors=rgb, edgecolors=rgb * 0.8 if lw else 'none', linewidths=lw))
    ax.autoscale_view(); ax.set_aspect('equal'); ax.set_axis_off()
mesh_quads = np.concatenate([V[T], V[T][:, 2:3]], axis=1)          # triangles as degenerate quads
def rebuild_quads():
    Q = []
    for p in patches:
        G = np.array(p['sections'])
        for sign in (1, -1):
            Gs = G.copy(); Gs[:, :, 1] *= sign
            A, B, C, D = Gs[:-1, :-1], Gs[1:, :-1], Gs[1:, 1:], Gs[:-1, 1:]
            Q.append(np.stack([A, B, C, D], axis=2).reshape(-1, 4, 3))
    return np.vstack(Q)
RQ = rebuild_quads()
fig, axs = plt.subplots(3, 2, figsize=(24, 21), dpi=100)
for row, (az, el, t) in enumerate([(-50, 24, 'front three-quarter'), (135, 26, 'rear three-quarter'), (-90, 3, 'side')]):
    R = camera(az, el)
    paint(axs[row, 0], mesh_quads, R); axs[row, 0].set_title(f'ORIGINAL Blender shell — {t}')
    paint(axs[row, 1], RQ, R, lw=0.15); axs[row, 1].set_title(f'REBUILD: {len(patches)} crease-bounded panels (+ mirror) — {t}')
    # patch borders on the rebuild
    for p in patches:
        G = np.array(p['sections'])
        for sign in (1, -1):
            for edge in (G[:, 0], G[:, -1], G[0], G[-1]):
                E = edge.copy(); E[:, 1] *= sign; Pp = E @ R.T; axs[row, 1].plot(Pp[:, 0], Pp[:, 1], color='#c03030', lw=0.6)
fig.suptitle(f'{CAR}_Body_Panels.arc — panel lofts vs original; loft→mesh mean {np.mean([p["mean_dev"] for p in patches]):.2f} cm, worst {max(p["max_dev"] for p in patches):.1f} cm', fontsize=14)
plt.tight_layout(); fig.savefig(f'{OUTDIR}/{CAR}_Body_Panels.png'); print('wrote', OUTDIR)

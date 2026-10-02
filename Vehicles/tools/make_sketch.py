#!/usr/bin/env python3
"""Turn Vehicles/<Car>/features.json into a step-journaled SolidArc document.

  python3 Vehicles/tools/make_sketch.py Liger

Writes Vehicles/<Car>/<Car>_Body_Sketch.arc  — a SolidArc native document whose
lines are the operation history: `reset`, then one commented STEP block per
feature group.  Replaying the file rebuilds the sketch; each line is one undoable op.

Content (metres, right-handed, Z up, +X = front, car mirrored about Y = 0):
  STEP 1   3D crease curves of the main shell   (the designer's feature lines)
  STEP 2   3D panel-boundary curves             (apertures, arch lips, panel splits)
  STEP 3   secondary parts' creases/boundaries  (cowl, roof frame ...)
  STEP 4-8 blueprint views: SIDE / TOP / BOTTOM / FRONT / REAR silhouettes, each laid
           flat on a face of the car's bounding "blueprint box", so the five views
           sit around the 3D skeleton like a drafting layout.
Curves are emitted as `polyline` (Douglas-Peucker simplified, tolerance TOL cm),
one object per curve, grouped by name prefix so the outliner folders stay tidy.
"""
import sys, os, json, math

CAR = sys.argv[1]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
D = json.load(open(f'{ROOT}/Vehicles/{CAR}/features.json'))
OUT = f'{ROOT}/Vehicles/{CAR}/{CAR}_Body_Sketch.arc'
TOL = 0.6            # cm, simplification tolerance for 3D curves
TOL_SIL = 0.8        # cm, for silhouettes
MIN_LEN = 4.0        # cm, drop crease fragments shorter than this
SCALE = 0.01         # cm -> m

main = 'Body_Main_Shell'
bb = D['parts'][main]['bbox']
yc = (bb[0][1] + bb[1][1]) / 2.0            # mirror plane (Liger's is off-centre)
lo = [bb[0][0], bb[0][1] - yc, bb[0][2]]; hi = [bb[1][0], bb[1][1] - yc, bb[1][2]]
# whole-body box over all parts
for p in D['parts'].values():
    for i in range(3):
        lo[i] = min(lo[i], p['bbox'][0][i] - (yc if i == 1 else 0)); hi[i] = max(hi[i], p['bbox'][1][i] - (yc if i == 1 else 0))
GAP = 25.0           # cm gap between car and the blueprint planes

def rdp(P, tol):
    if len(P) < 3: return P
    a, b = P[0], P[-1]; n = len(P)
    # point-line distance (works for 2D/3D)
    def dist(p):
        ab = [b[i] - a[i] for i in range(len(a))]; ap = [p[i] - a[i] for i in range(len(a))]
        L2 = sum(x * x for x in ab)
        if L2 < 1e-12: return math.sqrt(sum(x * x for x in ap))
        t = max(0.0, min(1.0, sum(ap[i] * ab[i] for i in range(len(a))) / L2))
        return math.sqrt(sum((ap[i] - t * ab[i]) ** 2 for i in range(len(a))))
    im, dm = 0, -1
    for i in range(1, n - 1):
        d = dist(P[i])
        if d > dm: im, dm = i, d
    if dm > tol:
        return rdp(P[:im + 1], tol)[:-1] + rdp(P[im:], tol)
    return [a, b]

def length(P):
    return sum(math.dist(P[i], P[i + 1]) for i in range(len(P) - 1))

out = []
E = out.append
E('# SolidArc native document v1')
E(f'# {CAR} — exterior body sketch extracted from the Blender design (crease / boundary feature lines + five silhouettes).')
E('# Right-handed, Z up, metres. +X = front, car mirrored about Y=0. Every line below is one operation; replay = rebuild.')
E('reset')

def emit_curves(part, kind, prefix, tint, tol, closed_ok=True, step=None):
    cnt = 0
    for L in D['parts'][part]['lines'][kind]:
        P = [(x, y - yc, z) for x, y, z in L]
        if length(P) < MIN_LEN: continue
        closed = closed_ok and math.dist(P[0], P[-1]) < 1e-3 and len(P) > 3
        if closed: P = P[:-1]
        S = rdp(P, tol) if not closed else rdp(P + [P[0]], tol)[:-1]
        if len(S) < 2: continue
        cnt += 1
        name = f'{prefix}_{cnt:03d}'
        pts = ' '.join(f'({x*SCALE:.4f},{y*SCALE:.4f},{z*SCALE:.4f})' for x, y, z in S)
        E(f'polyline {pts}{" --closed" if closed else ""} --name={name}')
        E(f'tint {name} {tint}')
    return cnt

RED, BLUE, BLACK, GREY = '0.85 0.10 0.10', '0.15 0.35 1.00', '0.05 0.05 0.05', '0.45 0.45 0.50'

E(f'# STEP 1 — {main}: crease curves (subdivision feature lines) in 3D')
n1 = emit_curves(main, 'CREASE', 'Shell_Crease', RED, TOL)
E(f'# STEP 2 — {main}: panel boundary curves (wheel arches, apertures, panel splits)')
n2 = emit_curves(main, 'BOUNDARY', 'Shell_Edge', BLUE, TOL)
step = 3; ns = {}
for part in D['parts']:
    if part == main: continue
    E(f'# STEP {step} — {part}: creases + boundaries')
    ns[part] = (emit_curves(part, 'CREASE', part.replace('Body_', '') + '_Crease', RED, TOL),
                emit_curves(part, 'BOUNDARY', part.replace('Body_', '') + '_Edge', BLUE, TOL))
    step += 1

# ---- blueprint views, each flattened onto a plane of the box around the car
# features.py view projections: side (u=x, v=z) ; top (u=x, v=y) ; bottom (u=x, v=-y) ; front (u=-y, v=z) ; rear (u=y, v=z)
planes = {
    'side':   lambda u, v: (u, lo[1] - GAP, v),            # plane y = -(half width + gap): drawn as seen from -Y
    'top':    lambda u, v: (u, v - yc, hi[2] + GAP),       # plane above the roof
    'bottom': lambda u, v: (u, -v - yc, lo[2] - GAP),      # plane below the floor (un-flip v so it sits under the car)
    'front':  lambda u, v: (hi[0] + GAP, -u, v),           # plane ahead of the nose
    'rear':   lambda u, v: (lo[0] - GAP, u, v),            # plane behind the tail
}
nv = {}
for view, f in planes.items():
    E(f'# STEP {step} — {view.upper()} view silhouette, laid flat on the blueprint plane {["y","z","z","x","x"][list(planes).index(view)]} = const')
    cnt = 0
    for loop in D['views'][view]['silhouette']:
        P2 = [tuple(p) for p in loop]
        if math.dist(P2[0], P2[-1]) < 1e-3: P2 = P2[:-1]
        S = rdp(P2 + [P2[0]], TOL_SIL)[:-1]
        if len(S) < 3: continue
        cnt += 1; name = f'View_{view.capitalize()}_{cnt:02d}'
        pts = ' '.join('(%.4f,%.4f,%.4f)' % tuple(c * SCALE for c in f(u, v)) for u, v in S)
        E(f'polyline {pts} --closed --name={name}')
        E(f'tint {name} {BLACK}')
    nv[view] = cnt; step += 1

E(f'# STEP {step} — blueprint box (reference frame the five views hang on)')
E('polyline (%.4f,%.4f,%.4f) (%.4f,%.4f,%.4f) (%.4f,%.4f,%.4f) (%.4f,%.4f,%.4f) --closed --name=Frame_Ground' % tuple(
    c * SCALE for c in (lo[0], lo[1] - GAP, lo[2] - GAP, hi[0] + GAP, lo[1] - GAP, lo[2] - GAP, hi[0] + GAP, hi[1], lo[2] - GAP, lo[0], hi[1], lo[2] - GAP)))
E(f'tint Frame_Ground {GREY}')
open(OUT, 'w').write('\n'.join(out) + '\n')
nops = sum(1 for l in out if not l.startswith('#'))
print(f'{OUT}: {nops} operations in {step} steps; creases={n1} edges={n2} others={ns} views={nv}')
print(f'car box (cm): X {lo[0]:.0f}..{hi[0]:.0f}  Y {lo[1]:.0f}..{hi[1]:.0f}  Z {lo[2]:.0f}..{hi[2]:.0f}   mirror offset applied: {yc:.1f}')

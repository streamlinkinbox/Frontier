#!/usr/bin/env python3
"""Reference sheet for planning lofts: the original Blender shell (shaded, +Y half) with the +Y feature
curves overlaid and labelled by curves.json index.  Four views.  usage: study_curves.py Liger out.png"""
import json, sys, numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
CAR, DST = sys.argv[1], sys.argv[2]
d = np.load(f'{CAR}/mesh/Body_Main_Shell.npz'); V, T = d['V'], d['T']
curves = json.load(open(f'{CAR}/curves.json'))['curves']
sel = []
for i, it in enumerate(curves):
    if it['part'] != 'Body_Main_Shell' or it['kind'] == 'SILHOUETTE': continue
    P = np.array(it['pts'])
    if P[:, 1].max() < 2 or np.linalg.norm(np.diff(P, axis=0), axis=1).sum() < 25: continue
    sel.append((i, it['kind'], P))

def view(ax, R, title, keep=lambda P: np.ones(len(P), bool)):
    """R: 3x3 rotation to camera (rows = right, up, towards-camera)."""
    Vc = V @ R.T; tri = Vc[T]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]); n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
    k = keep(V[T].mean(1)); tri, n = tri[k], n[k]
    order = np.argsort(tri[:, :, 2].mean(1))
    shade = 0.35 + 0.6 * np.clip(n[:, 2] * 0.6 + n[:, 1] * 0.3 + 0.3, 0, 1)
    pc = PolyCollection(tri[order][:, :, :2], facecolors=plt.cm.gray(shade[order]), edgecolors='none', antialiased=False)
    ax.add_collection(pc)
    for i, kind, P in sel:
        Pc = P @ R.T; c = 'red' if kind == 'CREASE' else 'deepskyblue'
        ax.plot(Pc[:, 0], Pc[:, 1], color=c, lw=1.2)
        m = len(Pc) // 2
        ax.annotate(str(i), (Pc[m, 0], Pc[m, 1]), fontsize=7, color='yellow', ha='center', va='center',
                    bbox=dict(boxstyle='round,pad=0.15', fc='black', ec='none', alpha=0.6))
    ax.set_aspect('equal'); ax.autoscale(); ax.set_title(title); ax.set_facecolor('#202830'); ax.set_xticks([]); ax.set_yticks([])

def rot(yaw, pitch):
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    Rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]]); Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    return (Rx @ Rz)[[0, 2, 1]] * np.array([[1], [1], [-1]])   # right, up, towards camera

fig, axs = plt.subplots(2, 2, figsize=(26, 16), dpi=100)
view(axs[0, 0], np.array([[1, 0, 0], [0, 0, 1], [0, 1, 0]]), 'SIDE from +Y (right side of car)')
view(axs[0, 1], np.array([[1, 0, 0], [0, 1, 0], [0, 0, 1]]), 'TOP (+Y half)', keep=lambda c: c[:, 1] > -1)
view(axs[1, 0], rot(np.radians(-140), np.radians(30)), 'REAR 3/4 from +Y')
view(axs[1, 1], rot(np.radians(-40), np.radians(30)), 'FRONT 3/4 from +Y')
fig.suptitle(f'{CAR} Body_Main_Shell — original mesh + fitted feature curves (red crease, blue boundary; labels = curves.json index)', fontsize=15)
plt.tight_layout(); fig.savefig(DST); print(DST)

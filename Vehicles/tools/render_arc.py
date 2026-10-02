#!/usr/bin/env python3
"""Render a polyline-only SolidArc .arc journal (proof of what the replay draws): iso + side + top."""
import sys, re, numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
src, dst = sys.argv[1], sys.argv[2]
curves = []; tints = {}
for line in open(src):
    if line.startswith('polyline'):
        pts = np.array(re.findall(r'\(([-\d.]+),([-\d.]+),([-\d.]+)\)', line), 'f8')
        name = re.search(r'--name=(\S+)', line).group(1)
        if '--closed' in line: pts = np.vstack([pts, pts[:1]])
        curves.append((name, pts))
    elif line.startswith('tint'):
        _, n, r, g, b = line.split(); tints[n] = (float(r), float(g), float(b))
fig = plt.figure(figsize=(22, 14), dpi=110)
ax1 = fig.add_subplot(2, 1, 1); ax2 = fig.add_subplot(2, 2, 3); ax3 = fig.add_subplot(2, 2, 4)
# isometric: standard CAD iso (x right-down, y right-up, z up)
a = np.radians(30); iso = lambda P: (P[:, 0] * np.cos(a) + P[:, 1] * np.cos(a) * 0 - P[:, 1] * np.cos(a), -P[:, 0] * np.sin(a) * 0.5 - P[:, 1] * np.sin(a) * 0.5 + P[:, 2] * 0.9)
for name, P in curves:
    c = tints.get(name, (0, 0, 0)); lw = 0.5 if name.startswith('Shell_Crease') else 1.0
    u, v = iso(P); ax1.plot(u, v, color=c, lw=lw)
    if not name.startswith('View_') and not name.startswith('Frame'):
        ax2.plot(P[:, 0], P[:, 2], color=c, lw=lw); ax3.plot(P[:, 0], P[:, 1], color=c, lw=lw)
    elif name.startswith('View_Side'): ax2.plot(P[:, 0], P[:, 2], color='k', lw=1.4)
    elif name.startswith('View_Top'): ax3.plot(P[:, 0], P[:, 1], color='k', lw=1.4)
for ax, t in ((ax1, 'isometric: 3D crease (red) + boundary (blue) skeleton with the five silhouettes on the blueprint box'),
              (ax2, 'SIDE: silhouette + 3D feature curves projected'), (ax3, 'TOP: silhouette + 3D feature curves projected')):
    ax.set_aspect('equal'); ax.grid(alpha=.25); ax.set_title(t)
fig.suptitle(f'{src.split("/")[-1]} — {len(curves)} curves replayed', fontsize=14)
plt.tight_layout(); fig.savefig(dst); print(dst)

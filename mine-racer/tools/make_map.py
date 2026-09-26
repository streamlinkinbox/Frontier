"""Top-down circuit map of Hollowvein Mine (design blueprint PNG)."""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyArrowPatch, Circle
import mine_layout as L

G = L.build()
N, CELL = L.N, L.CELL

fig, ax = plt.subplots(figsize=(13, 13), dpi=110)
fig.patch.set_facecolor("#17130f")
ax.set_facecolor("#17130f")

COL = {"ring": "#8f8a80", "ew": "#8f8a80", "ns": "#c98a4b", "bridge": "#e2762c",
       "maze": "#4c4034", "chamber": "#6e5a35", "rock": "#221d18"}

for r in range(N):
    for c in range(N):
        d = G[r][c]
        x, z = L.cell_x(c) - CELL / 2, L.cell_z(r) - CELL / 2
        if d is None:
            ax.add_patch(Rectangle((x, z), CELL, CELL, fc=COL["rock"], ec="none"))
            continue
        if d["tag"] == "ns":
            f = L.ns_floor(r)
            col = plt.cm.YlOrBr(0.25 + 0.5 * f / 4.5)
            ax.add_patch(Rectangle((x, z), CELL, CELL, fc=col, ec="none"))
        else:
            ax.add_patch(Rectangle((x, z), CELL, CELL, fc=COL[d["tag"]], ec="none"))

# grid lines (subtle)
for i in range(N + 1):
    p = (i - N / 2) * CELL
    ax.plot([p, p], [-N / 2 * CELL, N / 2 * CELL], color="#2c261f", lw=0.4, zorder=1)
    ax.plot([-N / 2 * CELL, N / 2 * CELL], [p, p], color="#2c261f", lw=0.4, zorder=1)

# ---- cart routes
o, i = 111.9, 105.1
for (pts, color, lbl, style) in [
    ([(-o, -o), (o, -o), (o, o), (-o, o)], "#ff4b3e", "cart ring CW", "-"),
    ([(-i, i), (i, i), (i, -i), (-i, -i)], "#ff9d4b", "cart ring CCW", "--"),
    ([(-104, -6.9), (104, -6.9)], "#ffd24b", "cart shuttle", ":"),
    ([(-104, -0.1), (104, -0.1)], "#ffd24b", None, ":"),
]:
    xs = [p[0] for p in pts] + ([pts[0][0]] if pts[0] != pts[-1] else [])
    zs = [p[1] for p in pts] + ([pts[0][1]] if pts[0] != pts[-1] else [])
    ax.plot(xs, zs, color=color, lw=2.2, ls=style, zorder=5,
            label=lbl, alpha=0.95)

# ---- hero lap (figure-8) arrow chain
lap = [(0, 108.5), (105, 108.5), (108.5, 100), (108.5, -105), (100, -108.5),
       (-3.5, -104), (-3.5, 104), (-3.5, 108.5), (100, 108.5), (108.5, 100),
       (108.5, -8), (100, -3.5), (-100, -3.5), (-108.5, 0), (-108.5, 100),
       (-100, 108.5), (0, 108.5)]
for a, b in zip(lap[:-1], lap[1:]):
    ax.add_patch(FancyArrowPatch(a, b, arrowstyle="-|>", mutation_scale=13,
                                 color="#57d7ff", lw=2.0, alpha=0.85, zorder=6))

# ---- checkpoints
for cp in L.checkpoints():
    ax.add_patch(Circle((cp["x"], cp["z"]), cp["r"], fc="none", ec="#57d7ff",
                        lw=1.6, ls="--", zorder=7))
    ax.annotate(cp["name"], (cp["x"], cp["z"]), color="#9fe8ff", fontsize=8.5,
                ha="center", va="center", zorder=8,
                path_effects=None, bbox=dict(fc="#10161a", ec="none", alpha=0.65, pad=1.2))

# ---- start
ax.add_patch(Rectangle((-1.2, 101.5), 2.4, 14, fc="#ffe135", ec="#000", lw=1, zorder=8))
ax.annotate("START / FINISH\n(gantry)", (0, 118), color="#ffe135", fontsize=10,
            ha="center", fontweight="bold")
ax.annotate("THE CROSSCUT\nover / under bridge", (-3.5, -16), color="#ffb37a",
            fontsize=10, ha="center", fontweight="bold")
ax.annotate("ramp up 4.5 m", (-14, -62), color="#ffb37a", fontsize=8.5, rotation=90, ha="center")
ax.annotate("ramp down", (7, 62), color="#ffb37a", fontsize=8.5, rotation=270, ha="center")

# chambers
for name, (r, c) in {"DRILL BAY": (9, 9), "ORE DOCK": (9, 25),
                     "WINCH ROOM": (25, 8), "OLD CAMP": (25.5, 25.5)}.items():
    ax.annotate(name, (L.cell_x(c), L.cell_z(r)), color="#d8c08a", fontsize=9,
                ha="center", fontweight="bold", zorder=8,
                bbox=dict(fc="#1c1710", ec="#6e5a35", pad=2.5, alpha=0.85))

# compass + scale
ax.annotate("N", (-118, 112), color="#cfc6b8", fontsize=14, fontweight="bold", ha="center")
ax.add_patch(FancyArrowPatch((-118, 108), (-118, 114), arrowstyle="-|>",
                             mutation_scale=16, color="#cfc6b8"))
ax.plot([90, 104], [-119, -119], color="white", lw=2)
ax.annotate("14 m (2 cells)", (97, -116.5), color="white", fontsize=8, ha="center")

ax.set_xlim(-125, 125); ax.set_ylim(-125, 125)
ax.set_aspect("equal"); ax.set_xticks([]); ax.set_yticks([])
for s in ax.spines.values():
    s.set_color("#3a332a")
ax.set_title("HOLLOWVEIN MINE — Level 01 “The Figure Eight”  |  245 × 245 m  |  "
             "ring 812 m · hero lap ≈ 1.25 km", color="#e8ddc8", fontsize=13, pad=14)
leg = ax.legend(loc="lower left", fontsize=9, facecolor="#1c1710", edgecolor="#3a332a",
                labelcolor="#d8cfc0")
plt.tight_layout()
plt.savefig("../assets/maps/layout_map.png", facecolor="#17130f", bbox_inches="tight")
print("wrote ../assets/maps/layout_map.png")

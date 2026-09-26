"""Hollowvein Mine — level layout generator.

Builds the maze grid for the mine racetrack:
  * 2-cell-wide outer ring road  (the "Highway Ring", cart traffic loops)
  * E-W crosscut at ground level (cart shuttle passes UNDER the bridge)
  * N-S crosscut that ramps up to a bridge over the E-W cut (the figure-8)
  * 4 quadrants filled with a braided maze of narrow tunnels + prop chambers
  * cart routes, checkpoints, spawn/start line, lamp positions

Outputs: layout dict (dict), ascii map (str), and writes layout.json +
layout_map.png for the design doc and the web viewer.

Grid: N=35 cells, CELL=7 m  ->  245 x 245 m mine.
World: x = (c+0.5-N/2)*CELL, z = (r+0.5-N/2)*CELL, Y up. Rows grow south (+z).
"""
import json
import numpy as np

N = 35
CELL = 7.0
RING_W = 2           # ring road width in cells
DECK_H = 6.0         # tunnel height
BRIDGE_F = 4.5       # upper deck floor
BRIDGE_ROOF = 10.5
SEED = 7

rng = np.random.default_rng(SEED)


def cell_x(c):
    return (c + 0.5 - N / 2) * CELL


def cell_z(r):
    return (r + 0.5 - N / 2) * CELL


def mkcell(decks, road=False, tag="maze"):
    """decks: list of (floor, roof, deck_id)"""
    return {"decks": [list(d) for d in decks], "road": road, "tag": tag}


def ns_floor(r):
    """Floor height of the N-S crosscut at row r (ramps to bridge)."""
    dist = abs(r - 16.5)
    return round(min(4.5, max(0.0, 4.5 * (13.5 - dist) / 13.0)), 3)


def build():
    g = [[None] * N for _ in range(N)]

    # ---------------- ring road (rows/cols 1-2 and 32-33)
    for r in range(1, N - 1):
        for c in range(1, N - 1):
            if r <= 2 or r >= N - 3 or c <= 2 or c >= N - 3:
                g[r][c] = mkcell([(0, DECK_H, 0)], road=True, tag="ring")

    # ---------------- E-W crosscut rows 16-17 (ground)
    for r in (16, 17):
        for c in range(3, N - 3):
            if g[r][c] is None:
                g[r][c] = mkcell([(0, DECK_H, 0)], road=True, tag="ew")

    # ---------------- N-S crosscut cols 16-17 (ramps + bridge, deck 1)
    for r in range(3, N - 3):
        for c in (16, 17):
            f = ns_floor(r)
            if r in (16, 17):
                # bridge cells: ground deck (E-W passes under) + upper deck
                g[r][c] = mkcell([(0, 4.2, 0), (BRIDGE_F, BRIDGE_ROOF, 1)],
                                 road=True, tag="bridge")
            else:
                g[r][c] = mkcell([(f, f + DECK_H, 1)], road=True, tag="ns")

    # ---------------- maze fill (recursive backtracker per quadrant)
    regions = {
        "NW": (range(3, 16), range(3, 16)),
        "NE": (range(3, 16), range(18, 32)),
        "SW": (range(18, 32), range(3, 16)),
        "SE": (range(18, 32), range(18, 32)),
    }

    def in_region(reg, r, c):
        rr, cc = regions[reg]
        return r in rr and c in cc

    def carve_maze(reg):
        rr, cc = regions[reg]
        nodes = [(r, c) for r in rr if r % 2 == 1 for c in cc if c % 2 == 1]
        visited = set()
        stack = [nodes[len(nodes) // 2]]
        visited.add(stack[0])
        while stack:
            r, c = stack[-1]
            nbrs = []
            for dr, dc in ((0, 2), (0, -2), (2, 0), (-2, 0)):
                nr, nc = r + dr, c + dc
                if in_region(reg, nr, nc) and (nr, nc) not in visited:
                    nbrs.append((nr, nc, r + dr // 2, c + dc // 2))
            if nbrs:
                nr, nc, wr, wc = nbrs[rng.integers(len(nbrs))]
                g[wr][wc] = mkcell([(0, DECK_H, 0)], tag="maze")
                g[nr][nc] = mkcell([(0, DECK_H, 0)], tag="maze")
                visited.add((nr, nc))
                stack.append((nr, nc))
            else:
                stack.pop()

    for reg in regions:
        carve_maze(reg)

    # ---------------- chambers (3x3 prop rooms, one per quadrant)
    chambers = {
        "Drill Bay":   [(8, 9), (9, 9), (10, 9)],    # (row range start, center)
        "Ore Dock":    [(9, 24), (9, 25), (9, 26)],
        "Winch Room":  [(24, 8), (25, 8), (26, 8)],
        "Old Camp":    [(25, 25), (25, 26), (26, 25)],
    }
    chamber_cells = {}
    for name, spans in chambers.items():
        (r0, c0), (rc, cc), (r1, c1) = spans
        cells = []
        for r in range(r0 - 1, r1 + 2):
            for c in range(c0 - 1, c1 + 2):
                g[r][c] = mkcell([(0, DECK_H + 1.2, 0)], tag="chamber")
                cells.append((r, c))
        chamber_cells[name] = cells

    # ---------------- connections to ring + crosscuts
    openings = [
        # NW
        (3, 6, 2, 6), (3, 12, 2, 12), (5, 3, 5, 2), (11, 3, 11, 2),
        (15, 6, 16, 6), (15, 10, 16, 10), (3, 15, 3, 16),
        # NE
        (3, 20, 2, 20), (3, 26, 2, 26), (5, 31, 5, 32), (11, 31, 11, 32),
        (15, 20, 16, 20), (15, 26, 16, 26), (3, 18, 3, 17),
        # SW
        (30, 6, 31, 6), (30, 12, 31, 12), (19, 3, 19, 2), (25, 3, 25, 2),
        (18, 6, 17, 6), (18, 10, 17, 10), (30, 15, 30, 16),
        # SE
        (30, 20, 31, 20), (30, 26, 31, 26), (19, 31, 19, 32), (25, 31, 25, 32),
        (18, 20, 17, 20), (18, 26, 17, 26), (30, 18, 30, 17),
    ]
    for r1, c1, r2, c2 in openings:
        for r, c in ((r1, c1), (r2, c2)):
            if g[r][c] is None:
                g[r][c] = mkcell([(0, DECK_H, 0)], tag="maze")
        # make sure the maze cell behind the opening is open (connectivity)
        g[r1][c1] = mkcell([(0, DECK_H, 0)], tag="maze")
        g[r2][c2] = g[r2][c2]  # ring/crosser cell already exists

    # close cells that would let cars clip beside the low end of the ramp
    for (r, c) in [(4, 15), (4, 18), (29, 15), (29, 18)]:
        if g[r][c] is not None and g[r][c]["tag"] == "maze":
            g[r][c] = None

    # chamber doors
    for name, spans in chambers.items():
        (r0, c0), (rc, cc), (r1, c1) = spans
        # door to the west or north maze cell
        for dr, dc in ((0, -2), (-2, 0), (0, 2), (2, 0)):
            rr, ccx = rc + dr, cc + dc
            if 0 < rr < N - 1 and 0 < ccx < N - 1 and g[rr][ccx] is not None:
                g[rc + dr // 2][cc + dc // 2] = mkcell([(0, DECK_H, 0)], tag="maze")
                break

    # ---------------- braid: open ~55% of dead ends into loops
    def open_nbrs(r, c):
        out = []
        for dr, dc in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            if g[r + dr][c + dc] is not None:
                out.append((r + dr, c + dc))
        return out

    maze_cells = [(r, c) for r in range(3, N - 3) for c in range(3, N - 3)
                  if g[r][c] is not None and g[r][c]["tag"] == "maze"]
    for r, c in maze_cells:
        if len(open_nbrs(r, c)) == 1 and rng.random() < 0.55:
            cands = []
            for dr, dc in ((0, 2), (0, -2), (2, 0), (-2, 0)):
                rr, ccx = r + dr, c + dc
                if 3 <= rr < N - 3 and 3 <= ccx < N - 3 and g[rr][ccx] is not None:
                    wall = g[r + dr // 2][c + dc // 2]
                    if wall is None:
                        cands.append((r + dr // 2, c + dc // 2))
            if cands:
                wr, wc = cands[rng.integers(len(cands))]
                g[wr][wc] = mkcell([(0, DECK_H, 0)], tag="maze")

    return g


def world_layout(g):
    """Flatten grid to serializable structure with world coords."""
    cells = []
    for r in range(N):
        for c in range(N):
            if g[r][c] is None:
                continue
            d = g[r][c]
            cells.append({
                "r": r, "c": c, "x": cell_x(c), "z": cell_z(r),
                "road": d["road"], "tag": d["tag"],
                "decks": [[f, rf, deck] for f, rf, deck in d["decks"]],
            })
    return {"n": N, "cell": CELL, "cells": cells}


# ---------------------------------------------------------------- routes etc
def cart_routes():
    """World-space waypoints for cart traffic (rails sit on these tracks)."""
    o, i = 111.9, 105.1          # outer / inner ring track offsets
    return [
        {"name": "ring_cw", "loop": True, "speed": 9.0,
         "pts": [(-o, -o), (o, -o), (o, o), (-o, o)]},
        {"name": "ring_ccw", "loop": True, "speed": 8.0,
         "pts": [(-i, i), (i, i), (i, -i), (-i, -i)]},
        {"name": "ew_shuttle_a", "loop": False, "speed": 7.5, "yaw": 90,
         "pts": [(-104, -6.9), (104, -6.9)]},
        {"name": "ew_shuttle_b", "loop": False, "speed": 7.0, "yaw": -90,
         "pts": [(104, -0.1), (-104, -0.1)]},
    ]


def checkpoints():
    """Hero-lap gates (figure-8) in order."""
    return [
        {"name": "SE Corner",      "x": 108.5, "z": 108.5, "r": 9},
        {"name": "NE Corner",      "x": 108.5, "z": -108.5, "r": 9},
        {"name": "Crosscut North", "x": -3.5, "z": -104.0, "r": 8},
        {"name": "The Bridge",     "x": -3.5, "z": -3.5, "r": 8, "y": 4.5},
        {"name": "Crosscut South", "x": -3.5, "z": 104.0, "r": 8},
        {"name": "E-W Cut West",   "x": -104.0, "z": -3.5, "r": 9},
        {"name": "West Straight",  "x": -108.5, "z": 60.0, "r": 9},
    ]


SPAWN = {"x": 0.0, "z": 108.5, "yaw": 90}     # facing +x (east)
START_LINE = {"x": 0.0, "z": 108.5}


def ascii_map(g):
    sym = {"ring": "=", "ew": "-", "ns": "|", "bridge": "X",
           "chamber": "o", "maze": "."}
    lines = []
    for r in range(N):
        row = []
        for c in range(N):
            d = g[r][c]
            row.append("#" if d is None else sym[d["tag"]])
        lines.append("".join(row))
    # mark start
    lines[32] = lines[32][:17] + "S" + lines[32][18:]
    return "\n".join(lines)


if __name__ == "__main__":
    g = build()
    layout = world_layout(g)
    layout["routes"] = cart_routes()
    layout["checkpoints"] = checkpoints()
    layout["spawn"] = SPAWN
    layout["startLine"] = START_LINE
    with open("../assets/maps/layout.json", "w") as f:
        json.dump(layout, f)
    with open("../assets/maps/layout_ascii.txt", "w") as f:
        f.write(ascii_map(g))
    n_open = len(layout["cells"])
    print(f"layout: {n_open} open cells "
          f"({100*n_open/(N*N):.0f}% of {N}x{N})")
    from collections import Counter
    print(Counter(c["tag"] for c in layout["cells"]))
    print(ascii_map(g))

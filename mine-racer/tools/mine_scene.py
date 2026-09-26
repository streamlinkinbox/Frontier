"""Hollowvein Mine — 3D scene builder.

Turns the layout grid into a full glTF scene:
  * rock walls/roofs/floors with deck-aware portals + septums (the over/under)
  * concrete road slabs with cart-grooves + embedded steel rails
  * timber frames in narrow tunnels, steel arch sets on the roads
  * hanging cage lamps, junction floods, signals, red bridge-portal lamps
  * the bridge: deck slab, support bents, under-beams
  * chambers dressed with ore piles, barrels, crates, tipped cart
  * parked carts on the rails (node prefix 'cartpark', viewer swaps for live
    traffic) + the player car at the start line
  * start/finish gantry with checkered banner

Also writes lamps.json (viewer dynamic lights).
"""
import json
import numpy as np
import trimesh
from matlib import (MAT, box, cyl, rod, sphere, place, T, set_mat,
                    merge_by_material)
import mine_layout as L
import boxcar
import minecart

rng = np.random.default_rng(42)
N, CELL = L.N, L.CELL
G = L.build()

WARM_LAMPS, FLOODS = [], []          # exported for the viewer
ROAD_TOP = 0.14


def cx(c):
    return L.cell_x(c)


def cz(r):
    return L.cell_z(r)


def decks(r, c):
    return G[r][c]["decks"] if G[r][c] else None


def tag(r, c):
    return G[r][c]["tag"] if G[r][c] else "rock"


# ================================================================ quads
def rock_quad(axis, fixed, s0, s1, t0, t1, sign, mat="rock", amp=0.13, sub=2):
    """Rocky quad on a plane. sign=+1 -> faces & noise toward +axis, -1 -> -axis.
    axis='x': plane x=fixed, s spans z, t spans y.  axis='z': plane z=fixed,
    s spans x, t spans y.  axis='y': plane y=fixed, s spans x, t spans z."""
    ns = sub + 1
    ss = np.linspace(s0, s1, ns)
    ts = np.linspace(t0, t1, ns)
    verts = []
    for i, s in enumerate(ss):
        for j, t in enumerate(ts):
            d = rng.uniform(-amp, amp) if (0 < i < ns - 1 and 0 < j < ns - 1) else 0.0
            if axis == "x":
                verts.append((fixed + d * sign, t, s))
            elif axis == "z":
                verts.append((s, t, fixed + d * sign))
            else:
                verts.append((s, fixed + d * sign, t))
    faces = []
    for i in range(sub):
        for j in range(sub):
            a = i * ns + j
            faces.append((a, a + ns, a + ns + 1))
            faces.append((a, a + ns + 1, a + 1))
    # default windings: x->-x, z->+z, y->-y ; flip to match sign
    flip = (axis == "x" and sign > 0) or (axis == "z" and sign < 0) or (axis == "y" and sign > 0)
    if flip:
        faces = [(a, c, b) for a, b, c in faces]
    m = trimesh.Trimesh(vertices=np.array(verts), faces=np.array(faces), process=False)
    set_mat(m, mat)
    return m


def tilted_quad(x0, x1, z0, z1, yfun, mat="rock", sub=3, amp=0.05, up=True):
    """Quad with height yfun(x, z). up=True -> faces +y."""
    xs = np.linspace(x0, x1, sub + 1)
    zs = np.linspace(z0, z1, sub + 1)
    verts = []
    for i, x in enumerate(xs):
        for j, z in enumerate(zs):
            y = yfun(x, z)
            if 0 < i < sub and 0 < j < sub:
                y += rng.uniform(-amp, amp)
            verts.append((x, y, z))
    ns = sub + 1
    faces = []
    for i in range(sub):
        for j in range(sub):
            a = i * ns + j
            faces.append((a, a + ns, a + ns + 1))
            faces.append((a, a + ns + 1, a + 1))
    if up:
        faces = [(a, c, b) for a, b, c in faces]
    m = trimesh.Trimesh(vertices=np.array(verts), faces=np.array(faces), process=False)
    set_mat(m, mat)
    return m


def subtract(iv, cuts):
    out = [iv]
    for lo, hi in cuts:
        nxt = []
        for a, b in out:
            if hi <= lo or b <= a:
                nxt.append((a, b))
                continue
            if a < lo:
                nxt.append((a, min(b, lo)))
            if b > hi:
                nxt.append((max(a, hi), b))
        out = nxt
    return [(a, b) for a, b in out if b - a > 0.05]


# ================================================================ walls
def build_walls(parts):
    """Every boundary (cell-cell or map edge) processed exactly once.
    Boundary b in 0..N sits between cols b-1 | b at x = (b - N/2)*CELL.
    Deck-aware portals + concrete septums where two decks collide."""

    def boundary(Wcell, Ecell, axis, fixed, s_center):
        """Wcell: cell on the -axis side; Ecell: cell on the +axis side."""
        s0, s1 = s_center - CELL / 2, s_center + CELL / 2

        def side(cell, sign):
            if not cell:
                return
            for f, rf_, dk in cell["decks"]:
                cuts = []
                other = Ecell if sign < 0 else Wcell
                if other:
                    for f2, r2, d2 in other["decks"]:
                        lo, hi = max(f, f2), min(rf_, r2)
                        if hi - lo > 0.05:
                            if not (dk == d2 or (f <= 0.2 and f2 <= 0.2)):
                                # two decks share this space -> concrete septum
                                if axis == "x":
                                    parts.append(box(0.3, hi - lo + 0.6, CELL,
                                                     pos=(fixed, (lo + hi) / 2, s_center),
                                                     mat="concrete"))
                                else:
                                    parts.append(box(CELL, hi - lo + 0.6, 0.3,
                                                     pos=(s_center, (lo + hi) / 2, fixed),
                                                     mat="concrete"))
                            cuts.append((lo, hi))
                for lo, hi in subtract((f, rf_), cuts):
                    parts.append(rock_quad(axis, fixed, s0, s1, lo - 0.3, hi + 0.3, sign))

        side(Wcell, -1)   # faces -axis
        side(Ecell, +1)   # faces +axis

    # vertical boundaries (plane x), between cols b-1 | b
    for r in range(N):
        for b in range(N + 1):
            W = G[r][b - 1] if 0 <= b - 1 < N else None
            E = G[r][b] if b < N else None
            if W is None and E is None:
                continue
            boundary(W, E, "x", (b - N / 2) * CELL, cz(r))
    # horizontal boundaries (plane z), between rows b-1 | b
    for c in range(N):
        for b in range(N + 1):
            W = G[b - 1][c] if 0 <= b - 1 < N else None
            E = G[b][c] if b < N else None
            if W is None and E is None:
                continue
            boundary(W, E, "z", (b - N / 2) * CELL, cx(c))


# ================================================================ floors/roofs
def build_floors_roofs(parts):
    for r in range(N):
        for c in range(N):
            d = G[r][c]
            if d is None:
                continue
            x0, x1 = cx(c) - CELL / 2, cx(c) + CELL / 2
            z0, z1 = cz(r) - CELL / 2, cz(r) + CELL / 2
            if d["tag"] == "ns":
                f = L.ns_floor(r)
                slope = (L.ns_floor(min(r + 1, 30)) - L.ns_floor(max(r - 1, 3))) / (2 * CELL)
                zc = (z0 + z1) / 2
                parts.append(tilted_quad(x0, x1, z0, z1,
                                         lambda x, z, f=f, s=slope, zc=zc: f - 0.05 + s * (z - zc),
                                         mat="rock", up=True))
                parts.append(tilted_quad(x0, x1, z0, z1,
                                         lambda x, z, f=f, s=slope, zc=zc: f + 6.0 + s * (z - zc),
                                         mat="rock_dark", up=False, amp=0.07))
            elif d["tag"] == "bridge":
                for fa, ra, da in d["decks"]:
                    if da == 0:
                        continue                 # ground: EW slab + deck slab cover it
                    parts.append(rock_quad("y", ra, x0, x1, z0, z1, -1, "rock_dark", amp=0.08))
            else:
                for fa, ra, da in d["decks"]:
                    parts.append(rock_quad("y", fa, x0, x1, z0, z1, +1, "rock", amp=0.09))
                    parts.append(rock_quad("y", ra, x0, x1, z0, z1, -1, "rock_dark", amp=0.08))


# ================================================================ roads
def build_roads(parts):
    def slab(x0, x1, z0, z1, top=ROAD_TOP, mat="concrete", th=0.32):
        parts.append(box(x1 - x0, th, z1 - z0, pos=((x0 + x1) / 2, top - th / 2, (z0 + z1) / 2), mat=mat))

    e, k = 101.8, 115.2
    slab(-e, e, -k, -e); slab(-e, e, e, k)
    slab(-k, -e, -e, e); slab(e, k, -e, e)
    for sx in (-1, 1):
        for sz in (-1, 1):
            slab(sx * e, sx * k, sz * e, sz * k)
    slab(-e, e, -10.2, 3.2)                              # E-W cut
    # N-S ramps (tilted slabs, cols 16-17 -> x in [-10.5, 3.5])
    for r in range(3, 31):
        if r in (16, 17):
            continue
        f = L.ns_floor(r)
        slope = (L.ns_floor(min(r + 1, 30)) - L.ns_floor(max(r - 1, 3))) / (2 * CELL)
        z0, z1 = cz(r) - CELL / 2, cz(r) + CELL / 2
        zc = (z0 + z1) / 2
        parts.append(tilted_quad(-10.2, 3.2, z0 - 0.02, z1 + 0.02,
                                 lambda x, z, f=f, s=slope, zc=zc: f + ROAD_TOP + s * (z - zc),
                                 mat="concrete", sub=2, amp=0.0, up=True))
    # bridge deck + supports
    parts.append(box(14.0, 0.3, 14.0, pos=(-3.5, 4.35, -3.5), mat="concrete"))
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(0.5, 4.2, 0.5, pos=(-3.5 + sx * 6.5, 2.1, -3.5 + sz * 6.5), mat="steel_dark"))
    for bx in (-8.5, 1.5):
        parts.append(box(0.35, 0.3, 13.6, pos=(bx, 4.05, -3.5), mat="steel"))

    # ---- grooves + rails
    def track_straight(axis, center, a0, a1):
        for off in (-0.45, 0.45):
            if axis == "z":
                parts.append(box(0.12, 0.16, a1 - a0, pos=(center + off, 0.08, (a0 + a1) / 2), mat="rail"))
            else:
                parts.append(box(a1 - a0, 0.16, 0.12, pos=((a0 + a1) / 2, 0.08, center + off), mat="rail"))
        if axis == "z":
            parts.append(box(1.5, 0.028, a1 - a0, pos=(center, 0.005, (a0 + a1) / 2), mat="concrete_d"))
        else:
            parts.append(box(a1 - a0, 0.028, 1.5, pos=((a0 + a1) / 2, 0.005, center), mat="concrete_d"))

    o, i = 111.9, 105.1
    for t in (o, -o):
        track_straight("z", t, -112.4, 112.4)
        track_straight("x", t, -112.4, 112.4)
    for t in (i, -i):
        track_straight("z", t, -106.2, 106.2)
        track_straight("x", t, -106.2, 106.2)
    track_straight("x", -6.9, -106.6, 106.6)
    track_straight("x", -0.1, -106.6, 106.6)

    # ---- cable trays along road walls
    for sgn in (-1, 1):
        parts.append(box(0.15, 0.08, 203.0, pos=(-108.5 + sgn * 6.6, 2.4, 0), mat="steel_dark"))
        parts.append(box(0.15, 0.08, 203.0, pos=(108.5 + sgn * 6.6, 2.4, 0), mat="steel_dark"))
        parts.append(box(203.0, 0.08, 0.15, pos=(0, 2.4, -108.5 + sgn * 6.6), mat="steel_dark"))
        parts.append(box(203.0, 0.08, 0.15, pos=(0, 2.4, 108.5 + sgn * 6.6), mat="steel_dark"))


# ================================================================ supports
def build_supports(parts):
    # timber frames in narrow maze tunnels
    for r in range(3, N - 3):
        for c in range(3, N - 3):
            if tag(r, c) != "maze" or (r + c) % 2:
                continue
            nz = (decks(r - 1, c) is not None, decks(r + 1, c) is not None)
            nx = (decks(r, c - 1) is not None, decks(r, c + 1) is not None)
            straight_z = nz[0] and nz[1] and not nx[0] and not nx[1]
            straight_x = nx[0] and nx[1] and not nz[0] and not nz[1]
            if not (straight_z or straight_x):
                continue
            if straight_z:
                for px in (cx(c) - 3.32, cx(c) + 3.32):
                    parts.append(box(0.36, 6.0, 0.36, pos=(px, 3.0, cz(r)), mat="timber"))
                parts.append(box(7.0, 0.42, 0.42, pos=(cx(c), 5.62, cz(r)), mat="timber_d"))
                parts.append(box(6.6, 0.2, 0.2, pos=(cx(c), 5.2, cz(r)), mat="timber"))
            else:
                for pz in (cz(r) - 3.32, cz(r) + 3.32):
                    parts.append(box(0.36, 6.0, 0.36, pos=(cx(c), 3.0, pz), mat="timber"))
                parts.append(box(0.42, 0.42, 7.0, pos=(cx(c), 5.62, cz(r)), mat="timber_d"))
                parts.append(box(0.2, 0.2, 6.6, pos=(cx(c), 5.2, cz(r)), mat="timber"))

    # steel arch sets on road straights
    def arch_set(x, z, along_z):
        if along_z:
            for px in (x - 6.55, x + 6.55):
                parts.append(box(0.3, 6.0, 0.3, pos=(px, 3.0, z), mat="steel_dark"))
            parts.append(box(13.4, 0.4, 0.34, pos=(x, 5.72, z), mat="steel"))
            parts.append(rod((x - 6.3, 5.4, z), (x - 3.2, 5.7, z), r=0.05, mat="steel"))
            parts.append(rod((x + 6.3, 5.4, z), (x + 3.2, 5.7, z), r=0.05, mat="steel"))
        else:
            for pz in (z - 6.55, z + 6.55):
                parts.append(box(0.3, 6.0, 0.3, pos=(x, 3.0, pz), mat="steel_dark"))
            parts.append(box(0.34, 0.4, 13.4, pos=(x, 5.72, z), mat="steel"))
            parts.append(rod((x, 5.4, z - 6.3), (x, 5.7, z - 3.2), r=0.05, mat="steel"))
            parts.append(rod((x, 5.4, z + 6.3), (x, 5.7, z + 3.2), r=0.05, mat="steel"))

    for t in range(-96, 97, 32):
        if abs(t + 3.5) > 12:
            arch_set(-108.5, t, True); arch_set(108.5, t, True)
            arch_set(t, -108.5, False); arch_set(t, 108.5, False)
    for t in (-90, -58, -26, 26, 58, 90):
        arch_set(t, -3.5, False)
    # timber on the N-S ramp
    for r in range(3, 31, 3):
        if r in (15, 16, 17, 18):
            continue
        f = L.ns_floor(r)
        for px in (-10.3, 3.3):
            parts.append(box(0.36, 6.0, 0.36, pos=(px, f + 3.0, cz(r)), mat="timber"))
        parts.append(box(14.0, 0.42, 0.42, pos=(-3.5, f + 5.62, cz(r)), mat="timber_d"))

    # chamber roof posts
    from scipy import ndimage
    mask = np.array([[1 if tag(r, c) == "chamber" else 0 for c in range(N)] for r in range(N)])
    lbl, ncomp = ndimage.label(mask)
    for ci in range(1, ncomp + 1):
        rs, cs = np.where(lbl == ci)
        r0, r1, c0, c1 = rs.min(), rs.max(), cs.min(), cs.max()
        x0, x1 = cx(c0) - 3.0, cx(c1) + 3.0
        z0, z1 = cz(r0) - 3.0, cz(r1) + 3.0
        for px in (x0, x1):
            for pz in (z0, z1):
                parts.append(box(0.45, 7.2, 0.45, pos=(px, 3.6, pz), mat="timber"))
        for (bx, bz, sx, sz) in (((x0 + x1) / 2, z0, x1 - x0 + 0.6, 0.5),
                                 ((x0 + x1) / 2, z1, x1 - x0 + 0.6, 0.5),
                                 (x0, (z0 + z1) / 2, 0.5, z1 - z0 + 0.6),
                                 (x1, (z0 + z1) / 2, 0.5, z1 - z0 + 0.6)):
            parts.append(box(sx, 0.5, sz, pos=(bx, 6.8, bz), mat="timber_d"))


# ================================================================ lights
def build_lights(parts):
    def hang_lamp(x, y_roof, z, warm=True):
        parts.append(rod((x, y_roof, z), (x, y_roof - 0.85, z), r=0.015, mat="steel_dark"))
        shade = trimesh.creation.cone(radius=0.30, height=0.24, sections=12)
        set_mat(shade, "lamp")
        parts.append(place(shade, (x, y_roof - 0.95, z), (np.pi / 2, 0, 0)))
        parts.append(sphere(0.085, pos=(x, y_roof - 1.06, z), mat="warm" if warm else "cool", seg=2))
        (WARM_LAMPS if warm else FLOODS).append((round(x, 1), round(y_roof - 1.0, 1), round(z, 1)))

    for i, t in enumerate(range(-105, 106, 14)):
        for (x, z, along_z) in ((-108.5, t, True), (108.5, t, True),
                                (t, -108.5, False), (t, 108.5, False)):
            s = 2.6 if (i % 2 == 0) else -2.6
            if along_z:
                hang_lamp(x + s, 6.0, z)
            else:
                hang_lamp(x, 6.0, z + s)
    for t in range(-94, 95, 21):
        hang_lamp(t, 6.0, -3.5 + (2.6 if t % 42 else -2.6))
    for r in range(3, 31, 2):
        f = L.ns_floor(r)
        hang_lamp(-3.5 + (2.6 if r % 4 else -2.6), f + 6.0, cz(r))
    for r in range(4, N - 4, 5):
        for c in range(4, N - 4, 5):
            if tag(r, c) in ("maze", "chamber"):
                hang_lamp(cx(c), 6.0 if tag(r, c) == "maze" else 7.2, cz(r))
    # junction floods (cool)
    for (x, z) in ((-108.5, -3.5), (108.5, -3.5), (-3.5, -108.5), (-3.5, 108.5),
                   (-108.5, 108.5), (108.5, 108.5), (-108.5, -108.5), (108.5, -108.5)):
        parts.append(box(0.6, 0.35, 0.2, pos=(x, 5.2, z), mat="lamp"))
        parts.append(box(0.45, 0.05, 0.3, pos=(x, 5.0, z), mat="cool"))
        FLOODS.append((round(x, 1), 5.0, round(z, 1)))
    # red lamps at bridge portals
    for (x, y, z) in [(-10.8, 3.5, -8.0), (-10.8, 3.5, 1.0), (3.8, 3.5, -8.0), (3.8, 3.5, 1.0),
                      (-8.5, 5.4, -10.8), (1.5, 5.4, -10.8), (-8.5, 5.4, 3.8), (1.5, 5.4, 3.8)]:
        parts.append(box(0.22, 0.22, 0.22, pos=(x, y, z), mat="red"))
    # signals where cart rails cross road junctions
    for (x, z, ry) in [(-104.5, -9.6, 0), (-104.5, 2.6, 0), (104.5, -9.6, 0), (104.5, 2.6, 0),
                       (-9.6, -104.5, 90), (2.6, -104.5, 90), (-9.6, 104.5, 90), (2.6, 104.5, 90)]:
        parts.append(cyl(0.07, 2.4, seg=10, pos=(x, 1.2, z), mat="steel_dark", axis="y"))
        parts.append(box(0.35, 0.8, 0.18, pos=(x, 2.7, z), rot=(0, np.radians(ry), 0), mat="lamp"))
        parts.append(box(0.16, 0.16, 0.06, pos=(x, 2.95, z), rot=(0, np.radians(ry), 0), mat="red"))
        parts.append(box(0.16, 0.16, 0.06, pos=(x, 2.55, z), rot=(0, np.radians(ry), 0), mat="green"))
        parts.append(cyl(0.09, 0.18, seg=10, pos=(x, 2.15, z), mat="steel_dark", axis="y"))
    # glowing direction signs at the crosscut junctions
    for (x, z, ry) in [(-99.0, -8.0, 90), (99.0, 1.0, -90), (-8.0, -99.0, 180), (1.0, 99.0, 0)]:
        parts.append(cyl(0.05, 2.0, seg=8, pos=(x, 1.0, z), mat="steel_dark", axis="y"))
        parts.append(box(1.1, 0.4, 0.07, pos=(x, 2.2, z), rot=(0, np.radians(ry), 0), mat="sign"))
        parts.append(box(0.9, 0.22, 0.08, pos=(x, 2.2, z), rot=(0, np.radians(ry), 0), mat="sign_glow"))


# ================================================================ props
def chamber_centers():
    from scipy import ndimage
    mask = np.array([[1 if tag(r, c) == "chamber" else 0 for c in range(N)] for r in range(N)])
    lbl, ncomp = ndimage.label(mask)
    out = []
    for ci in range(1, ncomp + 1):
        rs, cs = np.where(lbl == ci)
        out.append((cx(int(cs.mean())), cz(int(rs.mean()))))
    return out


def build_props(parts):
    for i, (x, z) in enumerate(chamber_centers()):
        pile = trimesh.creation.icosphere(subdivisions=2, radius=1.0)
        pile.apply_scale((2.6 + i * 0.3, 0.9, 2.2))
        set_mat(pile, "ore")
        parts.append(place(pile, (x + 2.0, 0.25, z - 1.5)))
        for j in range(3):
            parts.append(cyl(0.3, 0.92, seg=12,
                             pos=(x - 3.4 + j * 0.75, 0.46, z + 2.6 + 0.3 * (j % 2)),
                             mat="barrel_b" if j % 2 else "barrel_r", axis="y"))
        parts.append(box(0.95, 0.95, 0.95, pos=(x + 3.6, 0.48, z + 2.4), rot=(0, 0.3, 0), mat="crate"))
        parts.append(box(0.75, 0.75, 0.75, pos=(x + 3.5, 1.33, z + 2.5), rot=(0, -0.2, 0), mat="crate"))
        parts.append(box(1.6, 0.14, 1.2, pos=(x - 3.2, 0.07, z - 3.0), mat="timber_d"))
        for j in range(4):
            a = rng.uniform(0, 6.28)
            rr = rng.uniform(3.5, 5.5)
            rock = trimesh.creation.icosphere(subdivisions=1, radius=rng.uniform(0.2, 0.5))
            set_mat(rock, "rock_dark")
            parts.append(place(rock, (x + np.cos(a) * rr, 0.15, z + np.sin(a) * rr)))
    # tipped cart in the last chamber
    cs_ = chamber_centers()
    if len(cs_) >= 4:
        x, z = cs_[3]
        for m in minecart.build_cart(loaded=False):
            m.apply_transform(T((x + 1.5, 0.9, z + 3.4), (np.radians(102), 0.4, 0.2)))
            parts.append(m)


# ================================================================ statics
PARKED_CARTS = [(-111.9, -80, 0), (-111.9, 10, 0), (111.9, -40, 180), (111.9, 60, 180),
                (-80, 111.9, 90), (40, 111.9, 90), (-60, -105.1, 270), (70, -105.1, 270),
                (-90, -6.9, 90), (60, -0.1, 270)]


def car_parts_at(x, z, y=ROAD_TOP, deg=90):
    """Box car placed facing `deg` (0=+z, 90=+x)."""
    rot = (0, np.radians(deg), 0)
    base = T((x, y, z), rot)
    out = []
    for m in boxcar.build_body():
        m.apply_transform(base)
        out.append(m)
    for wx, wz in ((0.72, 1.08), (-0.72, 1.08), (0.72, -1.08), (-0.72, -1.08)):
        wt = T((wx, boxcar.R_WHEEL, wz))
        for m in boxcar.build_wheel():
            m.apply_transform(base @ wt)
            out.append(m)
    return out


def build_statics(parts):
    # parked carts (on rails) — separate node group, hidden by the live viewer
    # player car at spawn — separate node group
    pass


def build_gantry(parts):
    gx, gz = L.START_LINE["x"], L.START_LINE["z"]
    for pz in (gz - 6.6, gz + 6.6):
        parts.append(box(0.45, 5.4, 0.45, pos=(gx, 2.7, pz), mat="steel_dark"))
    parts.append(box(0.5, 0.5, 13.9, pos=(gx, 5.35, gz), mat="steel"))
    for iz, z in enumerate(np.arange(gz - 6.5, gz + 6.6, 0.74)):
        for iy, y in enumerate((4.55, 5.15)):
            mat = "banner" if (iz + iy) % 2 == 0 else "plastic"
            parts.append(box(0.06, 0.6, 0.74, pos=(gx, y, z), mat=mat))
    for z in np.arange(gz - 6.0, gz + 6.1, 1.2):
        parts.append(box(0.55, 0.02, 0.55, pos=(gx, ROAD_TOP + 0.011, z), mat="plastic"))


# ================================================================ main
def main():
    parts = []
    build_walls(parts)
    build_floors_roofs(parts)
    build_roads(parts)
    build_supports(parts)
    build_lights(parts)
    build_props(parts)
    build_gantry(parts)

    scene = trimesh.Scene()
    for mat_name, mesh in merge_by_material(parts).items():
        scene.add_geometry(mesh, node_name="mine_" + mat_name, geom_name=mat_name)

    # parked carts on the rails — their own nodes so the viewer can swap them
    park = []
    for (x, z, deg) in PARKED_CARTS:
        for m in minecart.build_cart():
            m.apply_transform(T((x, 0.16, z), (0, np.radians(deg), 0)))
            park.append(m)
    for mat_name, mesh in merge_by_material(park).items():
        scene.add_geometry(mesh, node_name="cartpark_" + mat_name, geom_name="p_" + mat_name)

    # player car at the start line
    sp = L.SPAWN
    car = car_parts_at(sp["x"], sp["z"], deg=90)
    for mat_name, mesh in merge_by_material(car).items():
        scene.add_geometry(mesh, node_name="canary_" + mat_name, geom_name="c_" + mat_name)

    scene.export("../assets/models/mine_scene.glb")
    n_tri = sum(len(m.faces) for m in scene.dump())
    import os
    print(f"mine_scene.glb | {n_tri:,} tris | "
          f"{os.path.getsize('../assets/models/mine_scene.glb')/1e6:.1f} MB | "
          f"{len(scene.graph.nodes_geometry)} nodes")
    with open("../assets/maps/lamps.json", "w") as f:
        json.dump({"warm": WARM_LAMPS, "flood": FLOODS}, f)
    print(f"lamps: {len(WARM_LAMPS)} warm, {len(FLOODS)} floods")


if __name__ == "__main__":
    main()

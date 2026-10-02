"""Plane-section helpers: cut a triangle mesh with x = const, chain the segments into polylines."""
import numpy as np
from collections import defaultdict

def cut_x(V, T, x0):
    """return list of polylines (N,3) where the mesh crosses the plane x = x0"""
    P = V[T]                                  # (n,3,3)
    s = P[:, :, 0] - x0
    segs = []
    for tri, ss in zip(P, s):
        signs = ss > 0
        if signs.all() or (~signs).all(): continue
        pts = []
        for i in range(3):
            j = (i + 1) % 3
            if (ss[i] > 0) != (ss[j] > 0):
                t = ss[i] / (ss[i] - ss[j]); pts.append(tri[i] + t * (tri[j] - tri[i]))
        if len(pts) == 2: segs.append((pts[0], pts[1]))
    return chain(segs)

def chain(segs, tol=1e-3):
    """join segments sharing endpoints (quantised) into polylines"""
    if not segs: return []
    key = lambda p: tuple(np.round(p / tol).astype(np.int64))
    adj = defaultdict(list); pts = {}
    for a, b in segs:
        ka, kb = key(a), key(b); pts.setdefault(ka, a); pts.setdefault(kb, b)
        if ka == kb: continue
        adj[ka].append(kb); adj[kb].append(ka)
    used = set(); out = []
    def walk(start):
        line = [start]; cur = start; prev = None
        while True:
            nxt = [n for n in adj[cur] if n != prev and (min(cur, n), max(cur, n)) not in used]
            if not nxt: break
            n = nxt[0]; used.add((min(cur, n), max(cur, n))); line.append(n); prev, cur = cur, n
            if cur == start: break
        return line
    for s in list(adj):              # open chains first (endpoints)
        if len(adj[s]) == 1 and not any((min(s, n), max(s, n)) in used for n in adj[s]):
            out.append(walk(s))
    for s in list(adj):              # remaining closed loops
        if any((min(s, n), max(s, n)) not in used for n in adj[s]):
            out.append(walk(s))
    return [np.array([pts[k] for k in l]) for l in out if len(l) > 1]

def length(P): return float(np.linalg.norm(np.diff(P, axis=0), axis=1).sum())

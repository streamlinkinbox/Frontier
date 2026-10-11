/**
 * Sheet-metal denting: an offline elasto-PLASTIC shell solve, baked into a
 * vertex-animation texture (VAT).
 *
 * Why bake at all
 * ---------------
 * A car panel does not fracture, it yields. Getting that right needs a real
 * shell solve with plastic flow and elastic springback, at a cost (tens of
 * thousands of constraint projections per millimetre of crush) that you cannot
 * pay inside a 16 ms frame while also running the rest of the game. But a dent
 * is *deterministic*: the same corner hit at the same angle always deforms the
 * same way, only the depth changes. So the expensive part is precomputable.
 *
 * What is baked
 * -------------
 * For each impact site on the body we run a quasi-static crush: press a rigid
 * impactor in by 1/F of the maximum depth, relax, let the plastic strains
 * accumulate, then REMOVE the impactor and relax again so the panel springs
 * back onto its new plastic rest shape. That released state is frame 1. Press
 * deeper, repeat, frame 2 ... frame F. The result is a monotone damage
 * sequence: the VAT's time axis is really a *damage* axis, so at runtime the
 * impact energy simply decides how far along the sequence to play, and
 * successive hits keep advancing it. Nothing is ever "reset" — the plastic
 * state is the animation cursor.
 *
 * Each frame stores, per vertex: position delta (xyz), accumulated plastic
 * strain (w, drives cracked/scuffed paint), and the recomputed normal.
 *
 * The solver
 * ----------
 * Position-based dynamics with plastic constraint creep (Müller et al. 2007),
 * which is the standard stable way to do large plastic deformation:
 *
 *   stretch constraint  |xi - xj| = L0        (membrane, stiff)
 *   bend constraint     |xa - xb| = B0        (across an edge, soft)
 *   plastic flow        if |eps| > epsY:  L0 += sign(eps)(|eps| - epsY) L0 k
 *
 * Why crushed metal CRUMPLES instead of dishing
 * ---------------------------------------------
 * This is the whole game, and it is a statement about the two stiffnesses of a
 * thin shell. Membrane stiffness goes as E*t, bending as E*t^3/12, so for
 * 1.2 mm steel over a 30 cm patch bending is ~5 orders of magnitude cheaper
 * than stretching. A confined sheet therefore refuses to change its metric: it
 * stays nearly ISOMETRIC and gets rid of the excess by folding. The energy
 * collects into narrow ridges and conical points (Witten's stretching ridges
 * and developable cones, Rev. Mod. Phys. 79, 643) whose width is only
 * ~sqrt(t*L) ~ 2 cm, with flat, essentially undeformed facets in between.
 *
 * Three things have to be true in the solver or you get a smooth bowl:
 *
 *   1. the membrane may yield only a few per cent  (steel necks at ~20 %, but
 *      a panel dent is nearly all rotation and almost no stretch)
 *   2. a plastic HINGE must SOFTEN: once a line of the sheet has yielded in
 *      bending it is the cheapest place to keep bending, so curvature runs
 *      away there and the fold becomes a crease instead of spreading out
 *   3. the flat sheet needs an IMPERFECTION field, because a perfectly
 *      symmetric plate has no reason to pick one buckling mode over another.
 *      Real panels have rolling texture, swage lines and spot welds; here it
 *      is a correlated ~0.3 mm noise, which is what makes fold lines run in
 *      long coherent arcs rather than dissolving into per-vertex fuzz.
 */

import { V3, v3, clamp, norm, sub, add, mul, dot } from '../core/math';
import { IndexedShell, shellNormals } from '../geom/carbody';

export interface DentSite {
  label: string;
  /** contact point in body-local space */
  p: V3;
  /** inward crush direction (unit, points INTO the body) */
  d: V3;
  /** impactor radius [m] */
  radius: number;
  /** maximum crush depth at full damage [m] */
  depth: number;
  /**
   * 'sphere'  = a pole / bollard / another car's corner: local crater
   * 'barrier' = a flat obstacle or another car's bumper bar: the panel is
   *             planed off flat and folds at the edges, which is what frontal
   *             impacts actually look like.
   */
  shape?: 'sphere' | 'barrier';
  /** half-width of a barrier contact [m] */
  span?: number;
  /** how far back the crush zone reaches [m] (barrier only) */
  zone?: number;
  /**
   * IN-PLANE draw-in, as a fraction of the crush depth: how much surrounding
   * sheet is fed into the damaged area while the impactor goes in.
   *
   * This is the difference between a dent and a crumple, and it took a
   * rewrite to see it. Pressing a ball into a plate that is clamped all round
   * is a *stretching* problem - the metal has to get longer to go deeper - and
   * the correct answer really is a smooth dish. That is what a hailstone or a
   * trolley does to a door, and it is what this bake produced.
   *
   * A crash is the opposite: the structure carrying the panel is collapsing,
   * so the panel's own edges move TOWARDS each other and the sheet suddenly
   * has more material than space. A near-inextensible sheet cannot absorb
   * that by compressing (it would buckle at a stress ~E(t/L)^2, essentially
   * zero), so it buckles - and because bending is 5 orders of magnitude
   * cheaper than stretching, it buckles into flat facets joined by narrow
   * plastic ridges. That is the crumpled bonnet.
   */
  drawIn?: number;
  /** elongate a barrier footprint along local +X (1 = round, 3 = a bar) */
  elong?: number;
}

export interface ShellTopo {
  n: number;
  x0: Float32Array;
  tris: Uint32Array;
  edge: Uint32Array;      // 2 per constraint
  edgeRest: Float32Array;
  bend: Uint32Array;      // 2 per constraint (opposite vertices across an edge)
  bendRest: Float32Array;
}

export function buildTopology(shell: IndexedShell): ShellTopo {
  const { pos, tris, n } = shell;
  const edgeMap = new Map<number, number[]>();   // packed edge key -> opposite verts
  const key = (a: number, b: number) => (a < b ? a * 1e6 + b : b * 1e6 + a);
  for (let t = 0; t < tris.length; t += 3) {
    const v = [tris[t], tris[t + 1], tris[t + 2]];
    for (let e = 0; e < 3; e++) {
      const a = v[e], b = v[(e + 1) % 3], o = v[(e + 2) % 3];
      const k = key(a, b);
      let l = edgeMap.get(k);
      if (!l) { l = []; edgeMap.set(k, l); }
      l.push(o);
    }
  }
  const edge: number[] = [], bend: number[] = [];
  for (const [k, opp] of edgeMap) {
    const a = Math.floor(k / 1e6), b = k - a * 1e6;
    edge.push(a, b);
    if (opp.length >= 2 && opp[0] !== opp[1]) bend.push(opp[0], opp[1]);
  }
  const dist = (i: number, j: number) =>
    Math.hypot(pos[i * 3] - pos[j * 3], pos[i * 3 + 1] - pos[j * 3 + 1], pos[i * 3 + 2] - pos[j * 3 + 2]);
  const edgeRest = new Float32Array(edge.length / 2);
  for (let i = 0; i < edgeRest.length; i++) edgeRest[i] = dist(edge[i * 2], edge[i * 2 + 1]);
  const bendRest = new Float32Array(bend.length / 2);
  for (let i = 0; i < bendRest.length; i++) bendRest[i] = dist(bend[i * 2], bend[i * 2 + 1]);
  return { n, x0: pos, tris, edge: new Uint32Array(edge), edgeRest, bend: new Uint32Array(bend), bendRest };
}

export interface DentOptions {
  frames?: number;
  iters?: number;
  release?: number;
  /** membrane yield strain (steel: sigma_y/E ~ 0.00125) */
  yieldStretch?: number;
  /** bending yield (a hinge forms far sooner than the metal stretches) */
  yieldBend?: number;
  creep?: number;
  /**
   * How far the MEMBRANE may plastically stretch. Small on purpose: sheet
   * steel necks at ~20 % and a panel dent is nearly pure rotation. Letting
   * this go large is exactly what turns a crumple into a smooth dish.
   */
  maxPlastic?: number;
  /**
   * How far a BENDING pair may plastically shorten, i.e. how tight a fold the
   * hinge can take. 0.55 of the two-step chord is a ~125 degree fold.
   */
  bendMaxPlastic?: number;
  bendCreep?: number;
  /**
   * Plastic-hinge SOFTENING. A hinge that has already yielded gets weaker and
   * yields sooner, so bending localises into a crease line instead of
   * spreading over the panel. 0 = no localisation (smooth dish).
   */
  soften?: number;
  /** out-of-plane imperfection amplitude [m] that seeds the buckling modes */
  imperfection?: number;
  /** stiffness of the chassis rails / sills the panel hangs off */
  chassis?: number;
}

export interface BakedDent {
  /**
   * SPARSE bake. A dent only moves the vertices inside its patch (typically a
   * few percent of the body), so the VAT only stores those. `verts[s]` is the
   * mesh vertex id of slot `s`; the shader looks a vertex up through a slot
   * table and skips it entirely if it is not in this patch. That is the
   * difference between a 3 MB texture and a 60 MB one.
   */
  verts: Uint32Array;
  /** frames * count * 4 : dx, dy, dz, plastic strain */
  pos: Float32Array;
  /** frames * count * 4 : nx, ny, nz, 0 */
  nrm: Float32Array;
  frames: number;
  /** patch size */
  count: number;
  ms: number;
}

/**
 * Quasi-static crush bake for one site. Deterministic: same input, same bytes.
 */
export function bakeDent(topo: ShellTopo, site: DentSite, o: DentOptions = {}): BakedDent {
  const t0 = (typeof performance !== 'undefined' ? performance.now() : Date.now());
  const F = o.frames ?? 14;
  const iters = o.iters ?? 24;
  const relIters = o.release ?? 14;
  const epsY = o.yieldStretch ?? 0.0015;
  const bendY = o.yieldBend ?? 0.02;
  const creep = o.creep ?? 0.45;
  const maxPl = o.maxPlastic ?? 0.055;
  const bendMaxPl = o.bendMaxPlastic ?? 0.55;
  const bendCreep = o.bendCreep ?? 0.85;
  const soften = o.soften ?? 44;
  const imperf = o.imperfection ?? 8.5e-4;
  const chassis = o.chassis ?? 0.14;

  const n = topo.n;
  const x = new Float32Array(topo.x0);          // live positions
  const x0 = topo.x0;
  const w = new Float32Array(n);                // inverse mass (0 = welded to the chassis)
  const plastic = new Float32Array(n);          // accumulated |plastic strain|
  // Panels are not uniform: swage lines, spot welds, bracing and rolling
  // texture all vary the local yield. This has to be CORRELATED in space -
  // white per-vertex noise just makes the surface sandy, whereas a field with
  // a ~6 cm wavelength gives fold lines something coherent to follow, which is
  // why real creases run in long arcs.
  const hash3 = (i: number, j: number, k: number) => {
    let h = i * 374761393 + j * 668265263 + k * 2147483647;
    h = (h ^ (h >>> 13)) * 1274126177;
    return ((h ^ (h >>> 16)) >>> 0) / 4294967295 - 0.5;
  };
  const vnoise = (px: number, py: number, pz: number, L: number) => {
    const fx = px / L, fy = py / L, fz = pz / L;
    const ix = Math.floor(fx), iy = Math.floor(fy), iz = Math.floor(fz);
    const tx = fx - ix, ty = fy - iy, tz = fz - iz;
    const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty), sz = tz * tz * (3 - 2 * tz);
    let acc = 0;
    for (let a = 0; a < 8; a++) {
      const bx = a & 1, by = (a >> 1) & 1, bz = (a >> 2) & 1;
      const wgt = (bx ? sx : 1 - sx) * (by ? sy : 1 - sy) * (bz ? sz : 1 - sz);
      acc += wgt * hash3(ix + bx, iy + by, iz + bz);
    }
    return acc * 2;
  };
  const weak = new Float32Array(n);             // 1 +- local yield variation
  for (let i = 0; i < n; i++) {
    const px = x0[i * 3], py = x0[i * 3 + 1], pz = x0[i * 3 + 2];
    weak[i] = 1 + (vnoise(px, py, pz, 0.115) * 0.95 + vnoise(px, py, pz, 0.042) * 0.30) * 0.22;
  }
  const jitter = (i: number) => weak[i];

  // ---- active region. Beyond ~3 impactor radii the body shell is stiff
  // enough (and tied to the rails) that nothing moves, so we solve locally.
  const smooth = (a: number, b: number, x: number) => {
    const t = clamp((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
  };
  const dirN = norm(site.d);
  const spanA = site.span ?? site.radius;
  const zoneA = site.zone ?? spanA * 0.9;
  const reach = site.radius * 2.7 + site.depth * 1.1;
  let active = 0;
  for (let i = 0; i < n; i++) {
    const dx = x0[i * 3] - site.p.x, dy = x0[i * 3 + 1] - site.p.y, dz = x0[i * 3 + 2] - site.p.z;
    let m: number;
    if (site.shape === 'barrier') {
      // the solve only needs to cover the crush zone the barrier drives,
      // not a sphere around the contact point
      const oa = dx * dirN.x + dy * dirN.y + dz * dirN.z;
      const el = site.elong ?? 1;
      const lat = Math.hypot((dx - oa * dirN.x) / el, dy - oa * dirN.y, dz - oa * dirN.z);
      m = smooth(spanA * 1.45, spanA * 0.95, lat) * smooth(zoneA * 1.75, zoneA * 0.95, Math.max(oa, 0));
    } else {
      const d = Math.hypot(dx, dy, dz);
      // smooth falloff to zero at the edge of the patch: no visible seam ring
      const t = clamp((reach - d) / (reach * 0.45), 0, 1);
      m = t * t * (3 - 2 * t);
    }
    // the floorpan and sills carry the structure
    const yn = x0[i * 3 + 1];
    if (yn < -0.32) m *= chassis;
    w[i] = m;
    if (m > 0.02) active++;
  }

  // ---- in-plane draw-in field. The surrounding panel feeds material into
  // the damaged zone: peak inward motion at mid radius, returning to zero at
  // the patch rim so the stamped dent still meets the undamaged body exactly.
  const drawIn = site.drawIn ?? 0;
  const pull = new Float32Array(drawIn > 0 ? n * 3 : 0);
  const pullG = new Float32Array(drawIn > 0 ? n : 0);
  if (drawIn > 0) {
    for (let i = 0; i < n; i++) {
      if (w[i] <= 0) continue;
      const dx = x0[i * 3] - site.p.x, dy = x0[i * 3 + 1] - site.p.y, dz = x0[i * 3 + 2] - site.p.z;
      const oa = dx * dirN.x + dy * dirN.y + dz * dirN.z;
      let rx = dx - oa * dirN.x, ry = dy - oa * dirN.y, rz = dz - oa * dirN.z;
      const r = Math.hypot(rx, ry, rz);
      if (r < 1e-6) continue;
      const t = clamp(r / reach, 0, 1);
      const g = smooth(0, 0.46, t) * (1 - smooth(0.60, 0.98, t));
      const a = -g / r;                       // unit inward * profile
      pull[i * 3] = rx * a; pull[i * 3 + 1] = ry * a; pull[i * 3 + 2] = rz * a;
      pullG[i] = g;
    }
  }
  let pullAmt = 0;

  // constraint subsets that touch the active patch
  const eSel: number[] = [], bSel: number[] = [];
  for (let e = 0; e < topo.edgeRest.length; e++) {
    const a = topo.edge[e * 2], b = topo.edge[e * 2 + 1];
    if (w[a] > 0 || w[b] > 0) eSel.push(e);
  }
  for (let b = 0; b < topo.bendRest.length; b++) {
    const p = topo.bend[b * 2], q = topo.bend[b * 2 + 1];
    if (w[p] > 0 || w[q] > 0) bSel.push(b);
  }
  const eRest = new Float32Array(topo.edgeRest);   // mutable (plastic) rest lengths
  const bRest = new Float32Array(topo.bendRest);
  const eRest0 = topo.edgeRest, bRest0 = topo.bendRest;

  const ks = 0.985;        // membrane: steel is all but inextensible
  // Bending stiffness sets the BUCKLE WAVELENGTH. Push it too low and the
  // cheapest mode is a fold every single cell, which is numerical hash, not
  // crumple; a real panel picks ~5-15 cm facets because it is curved and
  // ribbed, so the effective bending length is far larger than sqrt(t*L) for
  // bare 1.2 mm sheet. This is tuned to fold over ~6 cells.
  const kb = 0.45;
  const ka = 0.012;        // weak pull back to the undeformed shape (elasticity)

  // accumulated plastic hinge rotation per bend pair, which drives softening
  const hinge = new Float32Array(topo.bendRest.length);

  const project = (
    sel: number[], list: Uint32Array, rest: Float32Array, k: number,
    soft?: Float32Array,
  ) => {
    for (let s = 0; s < sel.length; s++) {
      const c = sel[s];
      const i = list[c * 2], j = list[c * 2 + 1];
      const wi = w[i], wj = w[j];
      const sw = wi + wj;
      if (sw <= 0) continue;
      const ax = x[i * 3], ay = x[i * 3 + 1], az = x[i * 3 + 2];
      const bx = x[j * 3], by = x[j * 3 + 1], bz = x[j * 3 + 2];
      let dx = bx - ax, dy = by - ay, dz = bz - az;
      const L = Math.hypot(dx, dy, dz);
      if (L < 1e-9) continue;
      // a yielded hinge is a weak hinge: that is what localises the fold
      // Softening only after a real plastic rotation: if a hinge goes weak the
      // instant it yields, the very first (grid-scale) wrinkle wins and the
      // elastic bending never gets to choose the facet size.
      const kk = soft ? k / (1 + Math.min(Math.max(soft[c] - 0.06, 0) * soften, 9)) : k;
      let diff = (L - rest[c]) / L * kk;
      // A fold is metal lying against metal, not metal through metal. Without
      // a floor on edge length the solver happily collapses a cell to nothing
      // and you get spikes instead of a crease.
      if (!soft && L < rest[c] * 0.62) diff = (L - rest[c] * 0.62) / L;
      dx *= diff; dy *= diff; dz *= diff;
      const fi = wi / sw, fj = wj / sw;
      x[i * 3] += dx * fi; x[i * 3 + 1] += dy * fi; x[i * 3 + 2] += dz * fi;
      x[j * 3] -= dx * fj; x[j * 3 + 1] -= dy * fj; x[j * 3 + 2] -= dz * fj;
    }
  };

  const flow = (
    sel: number[], list: Uint32Array, rest: Float32Array, rest0: Float32Array,
    yl: number, cr: number, lim0: number, soft?: Float32Array,
  ) => {
    for (let s = 0; s < sel.length; s++) {
      const c = sel[s];
      const i = list[c * 2], j = list[c * 2 + 1];
      const L = Math.hypot(
        x[j * 3] - x[i * 3], x[j * 3 + 1] - x[i * 3 + 1], x[j * 3 + 2] - x[i * 3 + 2],
      );
      const r = rest[c];
      const eps = (L - r) / r;
      const ae = Math.abs(eps);
      // yielded hinges also yield SOONER next time (kinematic softening)
      const relax = soft ? 1 / (1 + Math.min(Math.max(soft[c] - 0.06, 0) * soften, 9)) : 1;
      const ylv = yl * (jitter(i) + jitter(j)) * 0.5 * relax;
      if (ae <= ylv) continue;
      const dl = Math.sign(eps) * (ae - ylv) * r * cr;
      let nr = r + dl;
      const lim = rest0[c] * lim0;
      nr = clamp(nr, rest0[c] - lim, rest0[c] + lim);
      const moved = Math.abs(nr - r) / rest0[c];
      rest[c] = nr;
      if (soft) soft[c] += moved;
      plastic[i] += moved * 0.5; plastic[j] += moved * 0.5;
    }
  };

  const anchor = () => {
    for (let i = 0; i < n; i++) {
      if (w[i] <= 0) {
        x[i * 3] = x0[i * 3]; x[i * 3 + 1] = x0[i * 3 + 1]; x[i * 3 + 2] = x0[i * 3 + 2];
      } else if (pullAmt > 0) {
        // the surface the panel is elastically tied to is itself being drawn
        // inward, which is what supplies the excess material
        const t = ka * (1 - w[i]) + (1 - w[i]) * (1 - w[i]) * 0.30;
        for (let c = 0; c < 3; c++) {
          const tgt = x0[i * 3 + c] + pull[i * 3 + c] * pullAmt;
          x[i * 3 + c] += (tgt - x[i * 3 + c]) * t;
        }
      } else {
        // Residual elastic tie to the original surface. It has to be nearly
        // zero in the middle of the patch: a uniform pull-back is a smoothing
        // operator, and it will happily iron a crease flat again. Only the rim
        // (where the panel is actually welded to something) is held.
        const t = ka * (1 - w[i]) + (1 - w[i]) * (1 - w[i]) * 0.45;
        x[i * 3] += (x0[i * 3] - x[i * 3]) * t;
        x[i * 3 + 1] += (x0[i * 3 + 1] - x[i * 3 + 1]) * t;
        x[i * 3 + 2] += (x0[i * 3 + 2] - x[i * 3 + 2]) * t;
      }
    }
  };

  const patch: number[] = [];
  for (let i = 0; i < n; i++) if (w[i] > 0.004) patch.push(i);
  const verts = new Uint32Array(patch);
  const P = verts.length;

  // ---- imperfection. A perfectly flat, perfectly uniform plate is a
  // degenerate buckling problem: every mode is equally good, so the solver
  // picks the average of all of them, which is a smooth bowl. Real sheet has
  // rolling waviness of a few tenths of a millimetre. Seed it.
  if (imperf > 0) {
    const nrm0 = shellNormals(x0, topo.tris);
    for (let i = 0; i < n; i++) {
      if (w[i] <= 0) continue;
      const a = vnoise(x0[i * 3] + 11.3, x0[i * 3 + 1] - 4.7, x0[i * 3 + 2] + 2.1, 0.105) * imperf * w[i];
      x[i * 3] += nrm0[i * 3] * a;
      x[i * 3 + 1] += nrm0[i * 3 + 1] * a;
      x[i * 3 + 2] += nrm0[i * 3 + 2] * a;
    }
  }

  const outPos = new Float32Array(F * P * 4);
  const outNrm = new Float32Array(F * P * 4);
  const nrmScratch = new Float32Array(n * 3);
  const dir = norm(site.d);
  const R = site.radius;
  const barrier = site.shape === 'barrier';
  const elong = site.elong ?? 1;

  for (let f = 0; f < F; f++) {
    // progressive crush: slightly super-linear, as the panel work-hardens
    const depth = site.depth * Math.pow((f + 1) / F, 1.12);
    pullAmt = depth * drawIn;
    // impactor centre: sphere just touching the surface, pushed in by `depth`
    const C = add(site.p, add(mul(dir, depth), mul(dir, -R)));

    // Flat barrier, with the crush tapering to nothing at the edge of the
    // contact footprint. A hard-edged plane would punch a clean disc out of
    // the nose; a real barrier drags the surrounding metal in with it.
    const span = site.span ?? site.radius;
    const zone = site.zone ?? span * 0.9;

    for (let it = 0; it < iters; it++) {
      // rigid impactor collision
      for (let i = 0; i < n; i++) {
        if (w[i] <= 0) continue;
        if (barrier) {
          // Frontal/rear impacts do not just emboss the skin: the whole crush
          // zone shortens. The barrier prescribes a longitudinal compaction
          // field (full depth at the contact face, decaying to zero `zone`
          // metres back) and the shell relaxation turns the excess sheet
          // length into buckles and folds.
          const ox = x0[i * 3] - site.p.x, oy = x0[i * 3 + 1] - site.p.y, oz = x0[i * 3 + 2] - site.p.z;
          const oa = ox * dir.x + oy * dir.y + oz * dir.z;             // >0 = behind the face
          if (oa < -0.02) continue;
          let lx = ox - oa * dir.x, ly = oy - oa * dir.y, lz = oz - oa * dir.z;
          if (elong > 1) {
            // squash the footprint along local +X so the contact is a BAR
            // (bumper beam, kerb, guard rail) instead of a round punch
            const alx = lx / elong;
            lx = alx;
          }
          const l = Math.hypot(lx, ly, lz);
          if (l > span) continue;
          const u6 = Math.pow(l / span, 6);
          const lat = Math.max(0, 1 - u6);   // flat inside the footprint, sharp at its rim
          const back = clamp(1 - oa / zone, 0, 1);
          const move = depth * lat * back * back;
          const tx = x0[i * 3] + dir.x * move, ty = x0[i * 3 + 1] + dir.y * move, tz = x0[i * 3 + 2] + dir.z * move;
          const kc = 0.55;
          x[i * 3] += (tx - x[i * 3]) * kc;
          x[i * 3 + 1] += (ty - x[i * 3 + 1]) * kc;
          x[i * 3 + 2] += (tz - x[i * 3 + 2]) * kc;
          // and nothing may end up in front of the barrier face itself
          const rx = x[i * 3] - site.p.x - dir.x * depth;
          const ry = x[i * 3 + 1] - site.p.y - dir.y * depth;
          const rz = x[i * 3 + 2] - site.p.z - dir.z * depth;
          const along = rx * dir.x + ry * dir.y + rz * dir.z;
          if (along < 0) {
            x[i * 3] -= dir.x * along; x[i * 3 + 1] -= dir.y * along; x[i * 3 + 2] -= dir.z * along;
          }
        } else {
          const dx = x[i * 3] - C.x, dy = x[i * 3 + 1] - C.y, dz = x[i * 3 + 2] - C.z;
          const d = Math.hypot(dx, dy, dz);
          if (d < R && d > 1e-9) {
            const push = (R - d) / d;
            x[i * 3] += dx * push; x[i * 3 + 1] += dy * push; x[i * 3 + 2] += dz * push;
          }
        }
      }
      // The collapsing structure around the dent is a DISPLACEMENT boundary
      // condition, not a suggestion: it drags the surrounding sheet in at
      // whatever force it takes, and the sheet has to find somewhere to put
      // the extra material. Applying it through the soft elastic tie instead
      // let the panel quietly stretch its way out of trouble, which is
      // exactly how you end up with a smooth dish again.
      if (drawIn > 0) {
        for (let i = 0; i < n; i++) {
          const g = pullG[i];
          if (g <= 0) continue;
          const kd = 0.45 * g;
          for (let cc = 0; cc < 3; cc++) {
            const tgt = x0[i * 3 + cc] + pull[i * 3 + cc] * pullAmt;
            x[i * 3 + cc] += (tgt - x[i * 3 + cc]) * kd;
          }
        }
      }
      project(eSel, topo.edge, eRest, ks);
      project(bSel, topo.bend, bRest, kb, hinge);
      anchor();
      if (it % 4 === 3) {
        flow(eSel, topo.edge, eRest, eRest0, epsY, creep, maxPl);
        flow(bSel, topo.bend, bRest, bRest0, bendY, bendCreep, bendMaxPl, hinge);
      }
    }

    // ---- release: impactor gone, elastic springback onto the plastic shape
    for (let it = 0; it < relIters; it++) {
      project(eSel, topo.edge, eRest, ks);
      project(bSel, topo.bend, bRest, kb, hinge);
      anchor();
    }

    shellNormals(x, topo.tris, nrmScratch);
    const base = f * P * 4;
    for (let s = 0; s < P; s++) {
      const i = verts[s];
      outPos[base + s * 4] = x[i * 3] - x0[i * 3];
      outPos[base + s * 4 + 1] = x[i * 3 + 1] - x0[i * 3 + 1];
      outPos[base + s * 4 + 2] = x[i * 3 + 2] - x0[i * 3 + 2];
      outPos[base + s * 4 + 3] = Math.min(plastic[i] * 0.3, 1);
      outNrm[base + s * 4] = nrmScratch[i * 3];
      outNrm[base + s * 4 + 1] = nrmScratch[i * 3 + 1];
      outNrm[base + s * 4 + 2] = nrmScratch[i * 3 + 2];
      outNrm[base + s * 4 + 3] = 0;
    }
  }

  // ---- prune: only vertices that actually moved need a VAT slot. The solve
  // has to cover a wide patch for a smooth falloff, but most of that patch
  // ends up within a fraction of a millimetre of where it started.
  const keep: number[] = [];
  for (let s2 = 0; s2 < P; s2++) {
    let mx = 0;
    for (let f = 0; f < F; f++) {
      const b = f * P * 4 + s2 * 4;
      mx = Math.max(mx, Math.abs(outPos[b]) + Math.abs(outPos[b + 1]) + Math.abs(outPos[b + 2]));
    }
    if (mx > 3e-4) keep.push(s2);
  }
  const K = keep.length;
  const kVerts = new Uint32Array(K);
  const kPos = new Float32Array(F * K * 4);
  const kNrm = new Float32Array(F * K * 4);
  for (let a = 0; a < K; a++) {
    kVerts[a] = verts[keep[a]];
    for (let f = 0; f < F; f++) {
      for (let c = 0; c < 4; c++) {
        kPos[f * K * 4 + a * 4 + c] = outPos[f * P * 4 + keep[a] * 4 + c];
        kNrm[f * K * 4 + a * 4 + c] = outNrm[f * P * 4 + keep[a] * 4 + c];
      }
    }
  }

  const t1 = (typeof performance !== 'undefined' ? performance.now() : Date.now());
  void active;
  return { verts: kVerts, pos: kPos, nrm: kNrm, frames: F, count: K, ms: t1 - t0 };
}

// ------------------------------------------------------------------- lattice

/**
 * Deformation cage (free-form lattice) wrapped around the body.
 *
 * The per-vertex VAT is exact but is bound to one specific mesh. Production
 * pipelines bake the same solve into a lattice instead: the cage is authored
 * once around the body, and *every* part bound to it (panel, lights, badges,
 * glass, decals, the LODs) is deformed by trilinear lookup of the cage. One
 * bake, any mesh, any LOD, and the texture is ~10x smaller. The price is that
 * a trilinear cage rounds off the sharpest creases, which is why this demo
 * lets you A/B the two at runtime.
 */
export interface Cage {
  nx: number; ny: number; nz: number;
  min: V3; size: V3;
  nodes: number;
}

export function makeCage(min: V3, max: V3, nx = 26, ny = 12, nz = 12, pad = 0.04): Cage {
  const mn = v3(min.x - pad, min.y - pad, min.z - pad);
  const mx = v3(max.x + pad, max.y + pad, max.z + pad);
  return { nx, ny, nz, min: mn, size: sub(mx, mn), nodes: (nx + 1) * (ny + 1) * (nz + 1) };
}

/** Scatter a per-vertex displacement field onto the cage nodes (least-squares-ish). */
export function fitCage(
  cage: Cage, x0: Float32Array, delta: Float32Array, verts?: Uint32Array,
): Float32Array {
  const N = cage.nodes;
  const acc = new Float32Array(N * 4);
  const wsum = new Float32Array(N);
  const nIdx = (i: number, j: number, k: number) => (k * (cage.ny + 1) + j) * (cage.nx + 1) + i;
  const nv = verts ? verts.length : x0.length / 3;
  for (let s = 0; s < nv; s++) {
    const v = verts ? verts[s] : s;
    const dx = delta[s * 4], dy = delta[s * 4 + 1], dz = delta[s * 4 + 2];
    const st = delta[s * 4 + 3];
    if (dx === 0 && dy === 0 && dz === 0) continue;
    const fx = clamp((x0[v * 3] - cage.min.x) / cage.size.x, 0, 1) * cage.nx;
    const fy = clamp((x0[v * 3 + 1] - cage.min.y) / cage.size.y, 0, 1) * cage.ny;
    const fz = clamp((x0[v * 3 + 2] - cage.min.z) / cage.size.z, 0, 1) * cage.nz;
    const i0 = Math.min(Math.floor(fx), cage.nx - 1), j0 = Math.min(Math.floor(fy), cage.ny - 1);
    const k0 = Math.min(Math.floor(fz), cage.nz - 1);
    const tx = fx - i0, ty = fy - j0, tz = fz - k0;
    for (let a = 0; a < 2; a++) for (let b = 0; b < 2; b++) for (let c = 0; c < 2; c++) {
      const wgt = (a ? tx : 1 - tx) * (b ? ty : 1 - ty) * (c ? tz : 1 - tz);
      if (wgt <= 0) continue;
      const id = nIdx(i0 + a, j0 + b, k0 + c);
      acc[id * 4] += dx * wgt; acc[id * 4 + 1] += dy * wgt;
      acc[id * 4 + 2] += dz * wgt; acc[id * 4 + 3] += st * wgt;
      wsum[id] += wgt;
    }
  }
  const out = new Float32Array(N * 4);
  for (let i = 0; i < N; i++) {
    if (wsum[i] > 1e-6) {
      out[i * 4] = acc[i * 4] / wsum[i]; out[i * 4 + 1] = acc[i * 4 + 1] / wsum[i];
      out[i * 4 + 2] = acc[i * 4 + 2] / wsum[i]; out[i * 4 + 3] = acc[i * 4 + 3] / wsum[i];
    }
  }
  return out;
}

/** CPU trilinear cage evaluation (used by the headless tests and for hit tests). */
export function evalCage(cage: Cage, node: Float32Array, p: V3): V3 {
  const nIdx = (i: number, j: number, k: number) => (k * (cage.ny + 1) + j) * (cage.nx + 1) + i;
  const fx = clamp((p.x - cage.min.x) / cage.size.x, 0, 1) * cage.nx;
  const fy = clamp((p.y - cage.min.y) / cage.size.y, 0, 1) * cage.ny;
  const fz = clamp((p.z - cage.min.z) / cage.size.z, 0, 1) * cage.nz;
  const i0 = Math.min(Math.floor(fx), cage.nx - 1), j0 = Math.min(Math.floor(fy), cage.ny - 1);
  const k0 = Math.min(Math.floor(fz), cage.nz - 1);
  const tx = fx - i0, ty = fy - j0, tz = fz - k0;
  const r = v3();
  for (let a = 0; a < 2; a++) for (let b = 0; b < 2; b++) for (let c = 0; c < 2; c++) {
    const wgt = (a ? tx : 1 - tx) * (b ? ty : 1 - ty) * (c ? tz : 1 - tz);
    const id = nIdx(i0 + a, j0 + b, k0 + c);
    r.x += node[id * 4] * wgt; r.y += node[id * 4 + 1] * wgt; r.z += node[id * 4 + 2] * wgt;
  }
  return r;
}

export { dot };

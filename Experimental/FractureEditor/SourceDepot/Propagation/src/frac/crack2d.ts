/**
 * DYNAMIC CRACK NETWORK ON A SHELL   (glass panes, plastic panels, drywall)
 * =========================================================================
 *
 * Again: no Voronoi. This is an explicit dynamic-fracture simulation of crack
 * *tips* running through a stressed plate. Every fragment boundary is the
 * trace of a tip that obeyed:
 *
 *   speed        v = v_term * (1 - Gc/G)          (Mott / LEFM energy balance)
 *                v_term ~ 0.6 cR, cR = Rayleigh speed (soda-lime: ~3100 m/s,
 *                so cracks run at ~1500-1900 m/s -- matches Doll's plate-glass
 *                measurements)
 *
 *   branching    v > 0.4 cR AND G > 2 Gc   (micro-branching instability,
 *                Fineberg & Sharon; Sundaram & Tippur for soda-lime glass).
 *                Branch half-angle 12-30 deg.
 *
 *   deflection   the tip turns toward the local maximum-hoop-stress direction,
 *                is perturbed by a Weibull flaw field, and is *shielded* by
 *                nearby cracks (a crack cannot run into the unloaded wake of
 *                another) -- this is what produces T-junctions instead of
 *                X-crossings, the single most obvious tell of a real crack
 *                network versus a Voronoi diagram.
 *
 *   arrest       tip dies when it reaches another crack, leaves the plate, or
 *                when the global surface-energy budget (Griffith) runs out.
 *
 * Pattern generators, all emergent from the above plus the loading:
 *
 *   RADIAL      a point impact bends the plate; the back face goes into radial
 *               tension first, so radial cracks nucleate there and run outward.
 *               Their count scales linearly with dissipated energy (observed
 *               experimentally in spring-block and ballistic studies).
 *   CONCENTRIC  the triangular petals between radials keep bending; when a
 *               petal's span exceeds its flexural limit it snaps across, giving
 *               the circumferential arcs that terminate on radials.
 *   DICING      tempered glass stores ~7e4 J/m^3 of residual strain energy.
 *               Once the compressive skin is breached, that energy nucleates a
 *               self-sustaining branching front that sweeps the whole pane at
 *               ~1500 m/s and dices it into ~cm cubes.
 */

import { Material, rayleighSpeed, branchSpeed, terminalSpeed } from '../sim/materials';
import { rng, clamp, fbm3, weibull } from '../core/math';

export interface Tip {
  x: number; y: number;
  dx: number; dy: number;
  v: number;            // current speed [m/s]
  G: number;            // energy release rate [J/m^2]
  path: number;         // index into paths
  parent: number;       // path id it branched from (-1)
  age: number;          // steps since birth
  gen: number;
  alive: boolean;
  kind: 'radial' | 'ring' | 'branch' | 'dice';
  /** ring cracks curve around the impact at fixed radius */
  ringR?: number;
  ringDir?: number;
  /** distance travelled */
  s: number;
}

export interface CrackPath {
  pts: number[];        // flat x,y
  id: number;
  kind: string;
  width: number;        // visual opening
}

export interface Crack2DConfig {
  width: number;        // plate width [m]
  height: number;       // plate height [m]
  thickness: number;    // [m]
  material: Material;
  /** grid resolution along the longer axis */
  res?: number;
  seed?: number;
}

export interface ImpactEvent {
  x: number; y: number;      // plate coords [m]
  energy: number;            // [J]
  radius: number;            // contact radius [m]
  /** 0..1, how much of the impact went through the plate (perforation) */
  penetration: number;
}

export class CrackNetwork {
  readonly W: number; readonly H: number; readonly t: number;
  readonly mat: Material;
  readonly nx: number; readonly ny: number; readonly h: number;
  /** occupancy grid: 0 empty, else pathId+1 */
  readonly grid: Int32Array;
  paths: CrackPath[] = [];
  tips: Tip[] = [];
  /** remaining crack length the energy budget can pay for [m] */
  budget = 0;
  spent = 0;
  time = 0;
  dirty = true;
  impacts: ImpactEvent[] = [];
  private rand: () => number;
  /** how far the radial system has reached, persists after the tips die */
  private maxRadial = 0;
  private vTerm: number;
  private vBranch: number;
  private cR: number;
  private Gc: number;
  private pendingRings: { r: number; spawned: boolean }[] = [];

  constructor(cfg: Crack2DConfig) {
    this.W = cfg.width; this.H = cfg.height; this.t = cfg.thickness;
    this.mat = cfg.material;
    const res = cfg.res ?? 384;
    const long = Math.max(this.W, this.H);
    this.h = long / res;
    this.nx = Math.max(8, Math.round(this.W / this.h));
    this.ny = Math.max(8, Math.round(this.H / this.h));
    this.grid = new Int32Array(this.nx * this.ny);
    this.rand = rng(cfg.seed ?? 12345);
    this.cR = rayleighSpeed(this.mat);
    this.vTerm = terminalSpeed(this.mat);
    this.vBranch = branchSpeed(this.mat);
    this.Gc = this.mat.Gc;
  }

  get done(): boolean { return this.tips.length === 0; }
  get crackLength(): number { return this.spent; }

  // ------------------------------------------------------------------ fields

  /**
   * Hoop (circumferential) tensile stress of the flexural wave at radius r --
   * the stress that opens a RADIAL crack.
   *
   * Energy density -> stress: an impact energy E spread over the loaded annulus
   * (pi r^2 t) gives a strain energy density U = E/(pi r^2 t), and sigma ~
   * kappa * sqrt(E_mod * U). Calibrated so a 10 J hit on 6 mm float glass
   * peaks near 150 MPa at the contact -- about 3x its 45 MPa tensile strength,
   * which is what it takes to break a window.
   */
  hoop(x: number, y: number): number {
    let s = 0;
    for (const im of this.impacts) {
      const r = Math.max(Math.hypot(x - im.x, y - im.y), Math.max(im.radius, this.h));
      s += 0.25 * Math.sqrt(this.mat.E * this.energyDensity(im, r));
    }
    return s;
  }

  /**
   * Strain-energy density at radius r.
   *
   * Near the contact the energy is spread over the loaded disc (pi r^2 t).
   * Further out it rides an annular flexural wavefront of width Lw, so it is
   * spread over (2 pi r Lw t) instead -- amplitude then decays like 1/sqrt(r)
   * rather than 1/r, which is why radial cracks can run the whole width of a
   * window pane.
   */
  private energyDensity(im: ImpactEvent, r: number): number {
    const Lw = 0.3;
    const areaDisc = Math.PI * r * r;
    const areaRing = 2 * Math.PI * r * Lw;
    const area = Math.min(areaDisc, areaRing) + Math.PI * Math.max(im.radius, this.h) ** 2;
    return im.energy / (area * this.t);
  }

  /** Radial (through-thickness bending) stress that opens CONCENTRIC cracks. */
  radialBending(x: number, y: number): number {
    let s = 0;
    for (const im of this.impacts) {
      const r = Math.max(Math.hypot(x - im.x, y - im.y), Math.max(im.radius, this.h));
      const U = this.energyDensity(im, r);
      // The radial bending moment that snaps the petals is zero under the
      // contact, peaks a few contact radii out, then decays slowly (log-like
      // for a point-loaded plate) -- which is exactly why real windows have
      // concentric cracks packed near the impact and sparse further out.
      const u = r / (Math.max(im.radius, this.h) * 7);
      s += 0.25 * Math.sqrt(this.mat.E * U) * (1.5 * u / (1 + Math.pow(u, 1.25)));
    }
    return s;
  }

  /** Weibull flaw field: smooth spatial noise standing in for surface flaws. */
  private flaw(x: number, y: number): number {
    const k = 1 / Math.max(this.mat.flawSpacing, 1e-3);
    return fbm3(x * k, y * k, 3.7, 3) - 0.5;
  }

  // -------------------------------------------------------------- grid marks

  private cellIdx(x: number, y: number): number {
    const i = Math.floor((x + this.W * 0.5) / this.h);
    const j = Math.floor((y + this.H * 0.5) / this.h);
    if (i < 0 || j < 0 || i >= this.nx || j >= this.ny) return -1;
    return j * this.nx + i;
  }

  private probe(x: number, y: number): number {
    const c = this.cellIdx(x, y);
    return c < 0 ? -2 : this.grid[c];
  }

  /** Rasterise a segment; returns id of a *foreign* crack that was hit, or 0. */
  private mark(x0: number, y0: number, x1: number, y1: number, pathId: number, ignore: number[]): number {
    const steps = Math.max(1, Math.ceil(Math.hypot(x1 - x0, y1 - y0) / (this.h * 0.7)));
    let hit = 0;
    for (let s = 0; s <= steps; s++) {
      const t = s / steps;
      const x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
      // mark a 2-cell wide stroke so flood fill can never leak through
      for (let oy = -1; oy <= 1; oy++) {
        for (let ox = -1; ox <= 1; ox++) {
          if (ox && oy) continue;
          const c = this.cellIdx(x + ox * this.h * 0.55, y + oy * this.h * 0.55);
          if (c < 0) continue;
          const g = this.grid[c];
          if (g === 0) this.grid[c] = pathId + 1;
          else if (!hit && g - 1 !== pathId && !ignore.includes(g - 1)) hit = g;
        }
      }
    }
    return hit;
  }

  // ------------------------------------------------------------- nucleation

  /**
   * Nucleate the crack system for an impact.
   * Returns the number of primary radial cracks created.
   */
  impact(ev: ImpactEvent): number {
    this.impacts.push(ev);
    const mat = this.mat;

    // Griffith budget: usable energy -> metres of crack (new area = L * t,
    // two faces). Only a few percent of an impact ends up as surface; the rest
    // is fragment kinetic energy, plate vibration, sound and the projectile.
    const stored = mat.residual * this.W * this.H * this.t;
    const eff = 0.06 + 0.25 * mat.ductility;
    const usable = eff * ev.energy + 0.09 * stored;
    let add = usable / (mat.Gc * this.t * 2);
    // Physical floor on fragment size: you cannot dice finer than the flaw
    // spacing, so cap the crack length the network can hold.
    const cap = (2.2 * this.W * this.H) / Math.max(mat.flawSpacing, 1e-3);
    this.budget = Math.min(this.budget + add, cap);
    void add;

    if (mat.residual > 1e3) return this.nucleateDicing(ev);

    // --- annealed / brittle plate: radial star -----------------------------
    // Experimentally the number of radial cracks grows ~linearly with the
    // energy dissipated in cracking.
    const sigma = this.hoop(ev.x, ev.y);
    const drive = sigma / mat.sigT;
    let n = Math.round(clamp(3 + 1.1 * Math.sqrt(Math.max(0, ev.energy)) * (0.6 + drive * 0.25), 3, 26));
    if (mat.ductility > 0.5) n = Math.max(2, Math.round(n * 0.35)); // ductile: few tears

    const a0 = this.rand() * Math.PI * 2;
    // Nucleation ring: the crack mouths must be at least a few cells apart or
    // they would arrest on each other before they ever start running.
    const rNuc = Math.max(ev.radius * 1.15, (n * 3.2 * this.h) / (2 * Math.PI));
    for (let i = 0; i < n; i++) {
      const ang = a0 + (i / n) * Math.PI * 2 + (this.rand() - 0.5) * (1.2 / n);
      const r0 = rNuc;
      this.spawn(
        ev.x + Math.cos(ang) * r0, ev.y + Math.sin(ang) * r0,
        Math.cos(ang), Math.sin(ang), 'radial', -1, 0,
      );
    }
    // Schedule concentric cracks. Their radii grow geometrically: each ring
    // forms when the petal between radials has bent past its limit, which
    // happens at a roughly constant *strain*, i.e. constant r_{k+1}/r_k.
    const ratio = 1.45 + 0.3 * this.rand();
    let r = Math.max(ev.radius * 3.0, 0.035) * (1 + this.rand());
    const rmax = Math.hypot(this.W, this.H) * 0.55;
    this.pendingRings = [];
    while (r < rmax) {
      this.pendingRings.push({ r, spawned: false });
      r *= ratio;
    }
    return n;
  }

  /**
   * Tempered glass: the stored energy is enough to make ~1000x more surface
   * than the impact itself, so a dicing front sweeps the pane. We seed it as a
   * radially expanding nucleation wave of short, heavily branching cracks.
   */
  private nucleateDicing(ev: ImpactEvent): number {
    const lambda = this.mat.flawSpacing;       // target dice size
    const area = this.W * this.H;
    // One nucleus per few dice; branching fills in the rest, which is exactly
    // how the real fragmentation front works.
    const seeds = Math.round(clamp((area / (lambda * lambda)) * 0.85, 30, 900));
    for (let i = 0; i < seeds; i++) {
      const x = (this.rand() - 0.5) * this.W * 0.99;
      const y = (this.rand() - 0.5) * this.H * 0.99;
      const ang = this.rand() * Math.PI * 2;
      const d = Math.hypot(x - ev.x, y - ev.y);
      // Delay activation so the fragmentation front travels outward at ~cR/2.
      const delay = -Math.round(d / (this.cR * 0.5) / 2e-6);
      // A flaw opens into a crack that runs BOTH ways.
      for (const s of [1, -1]) {
        const t = this.spawn(x, y, Math.cos(ang) * s, Math.sin(ang) * s, 'dice', -1, 0);
        t.age = delay;
        t.v = this.vTerm * 0.6;
      }
    }
    return seeds;
  }

  private spawn(
    x: number, y: number, dx: number, dy: number,
    kind: Tip['kind'], parent: number, gen: number, ringR?: number, ringDir?: number,
  ): Tip {
    const id = this.paths.length;
    this.paths.push({ pts: [x, y], id, kind, width: 0 });
    const tip: Tip = {
      x, y, dx, dy, v: this.vTerm * 0.35, G: this.Gc * 1.2,
      path: id, parent, age: 0, gen, alive: true, kind, ringR, ringDir, s: 0,
    };
    this.tips.push(tip);
    return tip;
  }

  // ----------------------------------------------------------------- stepping

  /**
   * Advance the crack system by dt seconds of *physical* time.
   * (The app runs this in bullet-time so a 300 microsecond event is visible.)
   */
  step(dt: number): void {
    if (!this.tips.length) return;
    this.time += dt;
    const mat = this.mat;
    const alive: Tip[] = [];
    const newTips: Tip[] = [];

    for (const tip of this.tips) {
      if (tip.age < 0) { tip.age++; alive.push(tip); continue; }
      if (!tip.alive) continue;
      if (this.budget <= 0) { tip.alive = false; continue; }

      // ---- driving force at the tip
      const sigma = tip.kind === 'ring'
        ? this.radialBending(tip.x, tip.y)
        : this.hoop(tip.x, tip.y);
      // Plane-stress energy release rate for a crack of length ~ s.
      const aEff = Math.max(tip.s, mat.flawSpacing);
      let G = (Math.PI * sigma * sigma * aEff) / mat.E;
      if (tip.kind === 'dice') {
        // Residual field does the work: it is uniform, so G stays high until
        // the local volume has relaxed (approximated by neighbour density).
        G = (mat.residual * 2 * this.t) * 0.9;
      }
      // Flaw field modulates local toughness (weakest-link scatter).
      const GcLocal = this.Gc * (1 + 0.55 * this.flaw(tip.x, tip.y)) *
        (1 + 2.5 * mat.ductility * clamp(tip.s / 0.15, 0, 1));
      tip.G = G;

      if (G < GcLocal) {
        // Arrest: below Griffith the crack simply stops.
        tip.alive = false;
        continue;
      }

      // ---- speed: Mott's relation, saturating at the terminal velocity
      const vTarget = this.vTerm * (1 - GcLocal / G);
      tip.v += (vTarget - tip.v) * clamp(dt * 4e5, 0, 1);
      tip.v = clamp(tip.v, 0, this.vTerm);

      let step = tip.v * dt;
      const maxStep = this.h * 1.2;
      if (step > maxStep) step = maxStep;
      if (step < this.h * 0.25) step = this.h * 0.25;

      // ---- steering
      let { dx, dy } = tip;
      if (tip.kind === 'ring' && tip.ringR !== undefined) {
        // Curve around the impact centre at constant radius.
        const im = this.impacts[0];
        const rx = tip.x - im.x, ry = tip.y - im.y;
        const r = Math.hypot(rx, ry) || 1e-6;
        const tx = (-ry / r) * (tip.ringDir ?? 1), ty = (rx / r) * (tip.ringDir ?? 1);
        // radial correction pulls it back onto the ring
        const err = (tip.ringR - r) / tip.ringR;
        dx = tx + (rx / r) * err * 2.2;
        dy = ty + (ry / r) * err * 2.2;
      } else {
        // Turn toward the local maximum tensile direction (radially outward
        // for hoop-driven cracks), plus flaw-field wander.
        // A mode-I crack follows the direction of maximum hoop tension, which
        // for a point impact is radially outward. Blend toward it rather than
        // adding to it, so the path stays straight instead of curling.
        const im = this.nearestImpact(tip.x, tip.y);
        const rx = tip.x - im.x, ry = tip.y - im.y;
        const r = Math.hypot(rx, ry) || 1e-6;
        const k = tip.kind === 'dice' ? 0 : 0.12;
        dx += ((rx / r) - dx) * k;
        dy += ((ry / r) - dy) * k;
        // Flaw-field wander: the tip deviates toward locally weaker material.
        const wob = this.flaw(tip.x * 1.7 + 5.1, tip.y * 1.7 - 2.3);
        const turn = wob * (0.15 + 0.35 * mat.roughness + 0.35 * mat.ductility) *
          (tip.kind === 'dice' ? 0.09 : 1);
        const c = Math.cos(turn), s = Math.sin(turn);
        [dx, dy] = [dx * c - dy * s, dx * s + dy * c];
      }

      // Crack shielding: probe ahead-left / ahead-right; the unloaded wake of a
      // neighbouring crack repels the tip, which is what bends cracks into
      // perpendicular T-junctions.
      const nl = Math.hypot(dx, dy) || 1e-6; dx /= nl; dy /= nl;
      const look = this.h * 4;
      const pl = this.probe(tip.x + (dx * 0.7 - dy * 0.7) * look, tip.y + (dy * 0.7 + dx * 0.7) * look);
      const pr = this.probe(tip.x + (dx * 0.7 + dy * 0.7) * look, tip.y + (dy * 0.7 - dx * 0.7) * look);
      const foreign = (g: number) => g > 0 && g - 1 !== tip.path && g - 1 !== tip.parent;
      if (foreign(pl) !== foreign(pr)) {
        const sgn = foreign(pl) ? -1 : 1;
        const a = sgn * 0.5;
        const c = Math.cos(a), s = Math.sin(a);
        [dx, dy] = [dx * c - dy * s, dx * s + dy * c];
      }

      const nx2 = Math.hypot(dx, dy) || 1e-6;
      tip.dx = dx / nx2; tip.dy = dy / nx2;

      const px = tip.x, py = tip.y;
      tip.x += tip.dx * step;
      tip.y += tip.dy * step;
      tip.s += step;
      tip.age++;
      if (tip.kind === 'radial' || tip.kind === 'branch') {
        const im0 = this.impacts[0];
        if (im0) this.maxRadial = Math.max(this.maxRadial, Math.hypot(tip.x - im0.x, tip.y - im0.y));
      }
      this.spent += step;
      this.budget -= step;
      this.dirty = true;

      // out of plate?
      if (Math.abs(tip.x) > this.W * 0.5 || Math.abs(tip.y) > this.H * 0.5) {
        const cx = clamp(tip.x, -this.W * 0.5, this.W * 0.5);
        const cy = clamp(tip.y, -this.H * 0.5, this.H * 0.5);
        this.mark(px, py, cx, cy, tip.path, [tip.parent]);
        this.paths[tip.path].pts.push(cx, cy);
        tip.alive = false;
        continue;
      }

      const ignore = tip.age < 6 ? [tip.parent, tip.path] : [tip.path];
      const hit = this.mark(px, py, tip.x, tip.y, tip.path, ignore);
      this.paths[tip.path].pts.push(tip.x, tip.y);

      // Radial cracks all nucleate on the same little ring around the contact,
      // so give them a short immunity while they fan out.
      // Radial cracks all nucleate on the same little ring around the contact
      // and concentric cracks are born sitting on a radial, so both get a
      // short immunity while they get clear of their birthplace.
      const young = (tip.kind === 'radial' && tip.age < 8) || (tip.kind === 'ring' && tip.age < 4);
      if (hit && !young) {
        tip.alive = false;
        // Re-nucleation: in a self-sustaining fragmentation front (tempered
        // glass) the arrested tip dumps its energy into the intact material
        // beside it and a new crack starts off at a steep angle. This is what
        // keeps the front sweeping and what closes the dice cells.
        if (tip.kind === 'dice' && this.budget > 0.3 && this.rand() < 0.75) {
          const a = (Math.PI * 0.5 + (this.rand() - 0.5) * 0.5) * (this.rand() < 0.5 ? 1 : -1);
          const c = Math.cos(a), s2 = Math.sin(a);
          const bx = tip.dx * c - tip.dy * s2, by = tip.dx * s2 + tip.dy * c;
          const back = this.h * 2.5;
          const nt = this.spawn(tip.x - tip.dx * back, tip.y - tip.dy * back, bx, by, 'dice', tip.path, tip.gen);
          nt.v = tip.v * 0.85;
          newTips.push(nt);
        }
        continue;
      }  // T-junction: arrest

      // ---- micro-branching instability
      // Above ~0.4 cR a crack front can no longer radiate all the energy
      // flowing into it through a single tip, and sheds side branches. The
      // branch *density* (events per metre) is what is roughly constant, so
      // convert it to a per-step probability.
      const dice = tip.kind === 'dice';
      const canBranch = tip.v > this.vBranch && G > GcLocal * 3 &&
        tip.gen < (dice ? 9 : 3) && tip.s > this.h * 4;
      // In a dicing front the branch spacing IS the fragment size: the stored
      // field can only relax over about one dice width before the next branch
      // has to fire. Elsewhere branching is rare (~1 event per 0.45 m).
      const lamBranch = dice ? mat.flawSpacing * 0.85 : 0.45 * (1 + 3 * mat.ductility);
      const pBranch = clamp(step / lamBranch, 0, 0.3);
      if (canBranch && this.rand() < pBranch && this.budget > 0.2) {
        // Dice branches fork wide (they are carving out blocks); a running
        // crack in annealed glass forks at the classic shallow 12-30 deg.
        const half = dice
          ? (38 + 34 * this.rand()) * Math.PI / 180
          : (12 + 18 * this.rand()) * Math.PI / 180;
        for (const sgn of [-1, 1]) {
          const c = Math.cos(half * sgn), s2 = Math.sin(half * sgn);
          const bx = tip.dx * c - tip.dy * s2, by = tip.dx * s2 + tip.dy * c;
          const nt = this.spawn(tip.x, tip.y, bx, by, 'branch', tip.path, tip.gen + 1);
          nt.v = tip.v * (dice ? 0.95 : 0.72);   // branching splits the energy flux
          nt.s = tip.s * 0.5;
          newTips.push(nt);
        }
        tip.alive = false;    // the parent is replaced by its two branches
        continue;
      }

      alive.push(tip);
    }

    // ---- concentric crack nucleation
    if (this.pendingRings.length && this.impacts.length) {
      const im = this.impacts[0];
      // The petals between the radials only snap once the radials have run
      // past them, so a ring waits for the radial front to clear its radius.
      const maxR = this.maxRadial;
      for (const pr of this.pendingRings) {
        if (pr.spawned || this.budget <= 0) continue;
        if (maxR > pr.r * 1.25) {
          pr.spawned = true;
          const nArcs = Math.max(3, Math.round(3 + this.rand() * 4));
          for (let k = 0; k < nArcs; k++) {
            const a = this.rand() * Math.PI * 2;
            const x = im.x + Math.cos(a) * pr.r, y = im.y + Math.sin(a) * pr.r;
            if (Math.abs(x) > this.W * 0.5 || Math.abs(y) > this.H * 0.5) continue;
            // Only nucleate if the local bending field can actually drive it.
            const sB = this.radialBending(x, y);
            const GB = (Math.PI * sB * sB * this.mat.flawSpacing) / this.mat.E;
            if (GB < this.Gc) continue;
            for (const dir of [-1, 1]) {
              const tx = -Math.sin(a) * dir, ty = Math.cos(a) * dir;
              const t = this.spawn(x, y, tx, ty, 'ring', -1, 1, pr.r, dir);
              t.v = this.vTerm * 0.3;
              newTips.push(t);
            }
          }
        }
      }
    }

    this.tips = alive.concat(newTips.filter((t) => t.alive));
    if (this.budget <= 0) {
      for (const t of this.tips) t.alive = false;
      this.tips = [];
    }
  }

  private nearestImpact(x: number, y: number): ImpactEvent {
    let best = this.impacts[0], bd = Infinity;
    for (const im of this.impacts) {
      const d = (im.x - x) ** 2 + (im.y - y) ** 2;
      if (d < bd) { bd = d; best = im; }
    }
    return best;
  }

  /** Total number of crack tips still running. */
  get activeTips(): number { return this.tips.filter((t) => t.alive).length; }

  /** Diagnostics for the HUD. */
  stats() {
    return {
      paths: this.paths.length,
      tips: this.activeTips,
      length: this.spent,
      budget: Math.max(0, this.budget),
      cR: this.cR,
      vTerm: this.vTerm,
      surfaceEnergy: this.spent * this.t * this.mat.Gc,
    };
  }
}

export { weibull };

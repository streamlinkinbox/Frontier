// Terrain sculpting / painting for the demo. Works directly on the heightfield the solver uses,
// so edits immediately change the water: raising the bed squeezes the water aside, lowering it
// lets water flow in. The water surface is preserved by FluidSim.applyBed (a small GPU pass).
import { screenRay, rayHeightfield } from './picking.js';

export const TERRAIN_TOOLS = [
  { id: 'raise', label: 'Raise' },
  { id: 'lower', label: 'Lower' },
  { id: 'smooth', label: 'Smooth' },
  { id: 'flatten', label: 'Flatten' },
  { id: 'paint', label: 'Paint' },
];
export const TERRAIN_MATERIALS = [
  { id: 'grass', label: 'Grass', ch: -1 },
  { id: 'dirt', label: 'Dirt track', ch: 0 },
  { id: 'gravel', label: 'Gravel', ch: 1 },
  { id: 'sand', label: 'Sand', ch: 2 },
  { id: 'mud', label: 'Mud', ch: 3 },
];

export class TerrainEditor {
  constructor(app) {
    this.app = app;
    this.enabled = false;
    this.tool = 'raise';
    this.material = 'dirt';
    this.radius = 9;
    this.strength = 0.55;
    this.painting = false;
    this.point = null;
    this.ndc = null;
    this.flatLevel = 0;
    this._lastApply = 0;
    this._lastTime = 0;
    this._undo = [];
    this._maxUndo = 8;
    this.strokeDirty = false;
  }

  setEnabled(on) {
    this.enabled = !!on;
    if (this.enabled) this.app.setCameraMode('free');
    if (!this.enabled) { this.painting = false; this.point = null; this.app.renderer.setBrush(null); }
  }

  /** Screen-space cursor position (NDC) – call on every pointer move. */
  hover(ndcX, ndcY) {
    this.ndc = [ndcX, ndcY];
    if (!this.painting) this._updatePoint();
  }

  _updatePoint() {
    const a = this.app;
    if (!this.enabled || !this.ndc || !a.cam.pos || !a.cam.target) { this.point = null; return null; }
    const { origin, dir } = screenRay(a.cam.pos, a.cam.target, a.cam.fov, a.aspect, this.ndc[0], this.ndc[1]);
    const t = rayHeightfield(origin, dir, (x, z) => a.heightAt(x, z), 400);
    this.point = t == null ? null : [origin[0] + dir[0] * t, origin[1] + dir[1] * t, origin[2] + dir[2] * t];
    return this.point;
  }

  begin(ndcX, ndcY) {
    if (!this.enabled) return false;
    this.ndc = [ndcX, ndcY];
    if (!this._updatePoint() || !this.point) return false;
    const sc = this.app.scene;
    // keep a margin from the domain border so the near terrain still meets the far terrain
    const m = Math.min(this.radius, 20);
    if (this.point[0] < sc.origin[0] + m || this.point[0] > sc.origin[0] + sc.size - m ||
        this.point[2] < sc.origin[1] + m || this.point[2] > sc.origin[1] + sc.size - m) return false;
    this._pushUndo();
    this.painting = true;
    this.strokeDirty = false;
    this.flatLevel = this.point[1];
    this._lastTime = performance.now();
    this._apply(this.point, 0.016);
    return true;
  }

  move(ndcX, ndcY) {
    this.ndc = [ndcX, ndcY];
    this._updatePoint();
    if (!this.painting || !this.point) return;
    const now = performance.now();
    const dt = Math.min(0.05, Math.max(0.008, (now - this._lastTime) / 1000));
    this._lastTime = now;
    this._apply(this.point, dt);
  }

  end() {
    if (!this.painting) return;
    this.painting = false;
    this._commit(true);
  }

  undo() {
    const snap = this._undo.pop();
    if (!snap) return false;
    this.app.scene.bed.set(snap.bed);
    this.app.scene.mask.set(snap.mask);
    this.app.syncTerrain(true);
    return true;
  }

  reset() {
    const sc = this.app.scene;
    this._pushUndo();
    sc.bed.set(sc.bed0);
    sc.mask.set(sc.mask0);
    this.app.syncTerrain(true);
    return true;
  }

  _pushUndo() {
    const sc = this.app.scene;
    this._undo.push({ bed: sc.bed.slice(), mask: sc.mask.slice() });
    if (this._undo.length > this._maxUndo) this._undo.shift();
  }

  _falloff(d2, r) {
    const x = Math.max(0, 1 - d2 / (r * r));
    return x * x * (3 - 2 * x);
  }

  _apply(p, dt) {
    const sc = this.app.scene;
    const R = this.radius, r2 = R * R;
    const dx = sc.dx, nx = sc.nx, ny = sc.ny;
    const ci = Math.floor((p[0] - sc.origin[0]) / dx), cj = Math.floor((p[2] - sc.origin[1]) / dx);
    const ir = Math.ceil(R / dx) + 1;
    const i0 = Math.max(0, ci - ir), i1 = Math.min(nx - 1, ci + ir);
    const j0 = Math.max(0, cj - ir), j1 = Math.min(ny - 1, cj + ir);
    const bed = sc.bed, mask = sc.mask;
    const k = this.strength;
    const tool = this.tool;
    const mat = TERRAIN_MATERIALS.find((m) => m.id === this.material) ?? TERRAIN_MATERIALS[1];
    for (let j = j0; j <= j1; j++) {
      for (let i = i0; i <= i1; i++) {
        const x = sc.origin[0] + (i + 0.5) * dx, z = sc.origin[1] + (j + 0.5) * dx;
        const d2 = (x - p[0]) ** 2 + (z - p[2]) ** 2;
        if (d2 > r2) continue;
        const w = this._falloff(d2, R);
        const idx = j * nx + i;
        if (tool === 'raise') bed[idx] += w * k * 1.5 * dt;
        else if (tool === 'lower') bed[idx] -= w * k * 1.5 * dt;
        else if (tool === 'flatten') bed[idx] += (this.flatLevel - bed[idx]) * Math.min(1, w * k * 7 * dt);
        else if (tool === 'smooth') {
          let sum = 0, cnt = 0;
          for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
            const ii = Math.min(nx - 1, Math.max(0, i + di)), jj = Math.min(ny - 1, Math.max(0, j + dj));
            sum += bed[jj * nx + ii]; cnt++;
          }
          bed[idx] += (sum / cnt - bed[idx]) * Math.min(1, w * k * 9 * dt);
        } else if (tool === 'paint') {
          const o = idx * 4;
          const amt = Math.min(1, w * k * 6 * dt);
          const ch = mat.ch;
          if (ch < 0) { for (let c = 0; c < 4; c++) mask[o + c] = Math.round(mask[o + c] * (1 - amt)); }
          else {
            mask[o + ch] = Math.min(255, Math.round(mask[o + ch] + 255 * amt));
            for (let c = 0; c < 4; c++) if (c !== ch) mask[o + c] = Math.round(mask[o + c] * (1 - amt * 0.7));
          }
        }
      }
    }
    // clamp absurd edits
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
      const idx = j * nx + i;
      if (bed[idx] > 90) bed[idx] = 90;
      if (bed[idx] < -15) bed[idx] = -15;
    }
    this.strokeDirty = true;
    this._commit(false);
  }

  _commit(force) {
    const now = performance.now();
    if (!force && now - this._lastApply < 55) return;
    this._lastApply = now;
    this.app.syncTerrain(false);
  }

  /**
   * World-space brush cursor as a LINE LIST (pairs of points): the ring (dashed), four spokes and a
   * small centre cross. Returns null when the cursor is off the ground.
   */
  ringPoints() {
    if (!this.enabled || !this.point) return null;
    const a = this.app, p = this.point, R = this.radius, n = 48;
    const h = (x, z) => a.heightAt(x, z) + 0.08;
    const at = (th, r) => {
      const x = p[0] + Math.cos(th) * r, z = p[2] + Math.sin(th) * r;
      return [x, h(x, z), z];
    };
    const out = [];
    const seg = (A, B) => out.push(A[0], A[1], A[2], B[0], B[1], B[2]);
    // dashed ring: 3 of every 4 segments drawn
    for (let i = 0; i < n; i++) {
      if (i % 4 === 3) continue;
      const t0 = (i / n) * Math.PI * 2, t1 = ((i + 1) / n) * Math.PI * 2;
      seg(at(t0, R), at(t1, R));
    }
    // four spokes from the centre out to the ring
    for (let q = 0; q < 4; q++) seg([p[0], h(p[0], p[2]), p[2]], at((q * Math.PI) / 2, R));
    // centre cross
    const c = [p[0], h(p[0], p[2]), p[2]], k = Math.max(0.5, R * 0.12);
    seg([c[0] - k, c[1], c[2]], [c[0] + k, c[1], c[2]]);
    seg([c[0], c[1], c[2] - k], [c[0], c[1], c[2] + k]);
    return new Float32Array(out);
  }
}

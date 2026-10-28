// 2.5D water: heightfield wave-equation tile (waves / ripples / foam mask).
// One tile follows the camera focus, snapped to texel size so it never swims.
// World-space splats arrive from the ImpactRegistry (falling splash water,
// dropped bodies, rain, clicks) — this is the "actual" water surface sim.
import * as THREE from 'three';

export class Heightfield {
  constructor(size = 160, worldSize = 90) {
    this.n = size; this.worldSize = worldSize;
    this.h = new Float32Array(size * size);
    this.v = new Float32Array(size * size);
    this.foam = new Float32Array(size * size);
    this.center = new THREE.Vector2(0, 0);
    this.data = new Uint8Array(size * size * 4);
    this.tex = new THREE.DataTexture(this.data, size, size, THREE.RGBAFormat);
    this.tex.magFilter = this.tex.minFilter = THREE.LinearFilter;
    this.tex.needsUpdate = true;
    this.params = { waveSpeed: 42, damping: 0.996, foamDecay: 0.985, foamSpread: 0.22 };
  }
  get texel() { return this.worldSize / this.n; }
  // Snap-follow a world focus; returns true if the tile moved.
  follow(x, z) {
    const t = this.texel;
    const nx = Math.round(x / t) * t, nz = Math.round(z / t) * t;
    if (nx === this.center.x && nz === this.center.y) return false;
    // Shift existing field so ripples stay in world space when the tile moves.
    const dx = Math.round((nx - this.center.x) / t), dz = Math.round((nz - this.center.y) / t);
    if (Math.abs(dx) < this.n && Math.abs(dz) < this.n) {
      const shift = (src) => { const o = new Float32Array(src);
        for (let j = 0; j < this.n; j++) for (let i = 0; i < this.n; i++) {
          const si = i + dx, sj = j + dz;
          src[j * this.n + i] = (si >= 0 && sj >= 0 && si < this.n && sj < this.n) ? o[sj * this.n + si] : 0; } };
      shift(this.h); shift(this.v); shift(this.foam);
    } else { this.h.fill(0); this.v.fill(0); this.foam.fill(0); }
    this.center.set(nx, nz); return true;
  }
  worldToCell(x, z) {
    const u = (x - this.center.x) / this.worldSize + 0.5, w = (z - this.center.y) / this.worldSize + 0.5;
    return [u * (this.n - 1), w * (this.n - 1), u, w];
  }
  inside(x, z) { const [i, j] = this.worldToCell(x, z); return i > 1 && j > 1 && i < this.n - 2 && j < this.n - 2; }
  heightAt(x, z) {
    const [fx, fz] = this.worldToCell(x, z); const i = Math.round(fx), j = Math.round(fz);
    if (i < 0 || j < 0 || i >= this.n || j >= this.n) return 0;
    return this.h[j * this.n + i];
  }
  foamAt(x, z) { const [fx, fz] = this.worldToCell(x, z); const i = Math.round(fx), j = Math.round(fz);
    if (i < 0 || j < 0 || i >= this.n || j >= this.n) return 0; return this.foam[j * this.n + i]; }

  /** Register a falling-water / body impact: displacement impulse + foam deposit. */
  splat(x, z, radius, strength, foamAmount = 0.6) {
    const [cx, cz] = this.worldToCell(x, z); const r = Math.max(1, radius / this.texel);
    const r2 = r * r, x0 = Math.max(1, Math.floor(cx - r)), x1 = Math.min(this.n - 2, Math.ceil(cx + r));
    const z0 = Math.max(1, Math.floor(cz - r)), z1 = Math.min(this.n - 2, Math.ceil(cz + r));
    for (let j = z0; j <= z1; j++) for (let i = x0; i <= x1; i++) {
      const d2 = (i - cx) ** 2 + (j - cz) ** 2; if (d2 > r2) continue;
      const g = Math.exp(-d2 / (r2 * 0.45)), k = j * this.n + i;
      this.v[k] -= strength * g;                       // push surface down, wave eq. rebounds it
      this.foam[k] = Math.min(1.5, this.foam[k] + foamAmount * g);
    }
  }
  step(dt) {
    const { n, h, v, foam } = this, p = this.params;
    const c2 = Math.min(0.45, p.waveSpeed * dt * dt / (this.texel * this.texel) * 0.22);
    for (let j = 1; j < n - 1; j++) for (let i = 1; i < n - 1; i++) {
      const k = j * n + i;
      const lap = h[k - 1] + h[k + 1] + h[k - n] + h[k + n] - 4 * h[k];
      v[k] = (v[k] + c2 * lap) * p.damping;
    }
    let foamLap = 0;
    for (let j = 1; j < n - 1; j++) for (let i = 1; i < n - 1; i++) {
      const k = j * n + i; h[k] += v[k] * dt * 6;
      // foam advects/diffuses with the surface and fades
      foamLap = (foam[k - 1] + foam[k + 1] + foam[k - n] + foam[k + n]) * 0.25;
      foam[k] = (foam[k] + (foamLap - foam[k]) * p.foamSpread) * p.foamDecay
             + Math.max(0, Math.abs(v[k]) - 0.55) * 0.012; // breaking ripples whiten
    }
    // upload: R=height(normalised) G=foam B=velocity
    for (let k = 0; k < n * n; k++) {
      this.data[k * 4] = THREE.MathUtils.clamp(128 + h[k] * 42, 0, 255);
      this.data[k * 4 + 1] = THREE.MathUtils.clamp(foam[k] * 200, 0, 255);
      this.data[k * 4 + 2] = THREE.MathUtils.clamp(128 + v[k] * 30, 0, 255);
      this.data[k * 4 + 3] = 255;
    }
    this.tex.needsUpdate = true;
  }
}

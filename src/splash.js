// Volumetric splash fluid: SPH-lite particles on a SPARSE 3D hash grid.
// The grid is world-space and unbounded — a cell only exists where water is,
// so splashes work anywhere (any ocean tile, any "whatever grid" cell size),
// not inside a fixed tank. Neighbour search, density and pressure all read
// the same hash grid. Foam is a second particle pool that lives on the 2.5D
// surface, then dissolves back into it.
const key = (x, y, z) => x * 73856093 ^ y * 19349663 ^ z * 83492791;

export class SplashFluid {
  constructor(scene, THREE, max = 7000) {
    this.THREE = THREE; this.max = max; this.count = 0; this.foamCount = 0;
    this.pos = new Float32Array(max * 3); this.vel = new Float32Array(max * 3);
    this.life = new Float32Array(max); this.dens = new Float32Array(max);
    this.grid = new Map();               // sparse volumetric grid: hash -> [indices]
    this.params = { cellSize: 0.55, gravity: -9.8, viscosity: 0.35, pressure: 55,
                    splashSize: 1, foamAmount: 1, drag: 0.12, vorticity: 0.6 };
    this.onImpact = null;                // (x,z,energy,radius) => registry entry

    const geo = new THREE.SphereGeometry(0.5, 8, 6);
    const mat = new THREE.MeshPhysicalMaterial({ color: 0x9fd4e8, roughness: 0.05,
      transmission: 0.35, transparent: true, opacity: 0.82, metalness: 0 });
    this.mesh = new THREE.InstancedMesh(geo, mat, max);
    this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage); this.mesh.frustumCulled = false;
    scene.add(this.mesh);
    const fgeo = new THREE.SphereGeometry(0.5, 6, 5);
    const fmat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.9, transparent: true, opacity: 0.9 });
    this.foamMesh = new THREE.InstancedMesh(fgeo, fmat, 3000);
    this.foamMesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage); this.foamMesh.frustumCulled = false;
    scene.add(this.foamMesh);
    this.foam = []; // {x,y,z,vx,vz,life,size}
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._s = new THREE.Vector3(); this._p = new THREE.Vector3();
    this.activeCells = 0;
  }
  spawn(x, y, z, vx, vy, vz, n, spread = 0.6, up = 1) {
    for (let s = 0; s < n && this.count < this.max; s++) {
      const i = this.count++, k = i * 3;
      this.pos[k] = x + (Math.random() - .5) * spread; this.pos[k + 1] = y + Math.random() * .2; this.pos[k + 2] = z + (Math.random() - .5) * spread;
      this.vel[k] = vx + (Math.random() - .5) * 1.6; this.vel[k + 1] = vy * (0.7 + Math.random() * 0.6) * up; this.vel[k + 2] = vz + (Math.random() - .5) * 1.6;
      this.life[i] = 4;
    }
  }
  spawnFoam(x, y, z, n, energy = 1) {
    for (let s = 0; s < n && this.foam.length < 3000; s++)
      this.foam.push({ x: x + (Math.random() - .5) * 1.4, y, z: z + (Math.random() - .5) * 1.4,
        vx: (Math.random() - .5) * energy, vz: (Math.random() - .5) * energy,
        life: 1.5 + Math.random() * 2.5, size: .12 + Math.random() * .3 * this.params.splashSize });
  }
  rebuildGrid() {
    this.grid.clear(); const cs = this.params.cellSize;
    for (let i = 0; i < this.count; i++) {
      const k = i * 3, h = key(Math.floor(this.pos[k] / cs), Math.floor(this.pos[k + 1] / cs), Math.floor(this.pos[k + 2] / cs));
      let a = this.grid.get(h); if (!a) this.grid.set(h, a = []); a.push(i);
    }
    this.activeCells = this.grid.size;
  }
  step(dt, surfaceY /*(x,z)=>world height of 2.5D+ocean*/) {
    const p = this.params, cs = p.cellSize, h2 = cs * cs;
    this.rebuildGrid();
    // density + pressure/viscosity forces from sparse-grid neighbours (SPH-lite)
    for (let i = 0; i < this.count; i++) {
      const k = i * 3, cx = Math.floor(this.pos[k] / cs), cy = Math.floor(this.pos[k + 1] / cs), cz = Math.floor(this.pos[k + 2] / cs);
      let dens = 0, fx = 0, fy = 0, fz = 0;
      for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (let c = -1; c <= 1; c++) {
        const cell = this.grid.get(key(cx + a, cy + b, cz + c)); if (!cell) continue;
        for (const j of cell) { if (j === i) continue; const m = j * 3;
          const dx = this.pos[k] - this.pos[m], dy = this.pos[k + 1] - this.pos[m + 1], dz = this.pos[k + 2] - this.pos[m + 2];
          const d2 = dx * dx + dy * dy + dz * dz; if (d2 > h2 || d2 < 1e-6) continue;
          const d = Math.sqrt(d2), w = 1 - d / cs; dens += w * w;
          const press = w * p.pressure / d, visc = w * p.viscosity;
          fx += dx * press + (this.vel[m] - this.vel[k]) * visc;
          fy += dy * press + (this.vel[m + 1] - this.vel[k + 1]) * visc;
          fz += dz * press + (this.vel[m + 2] - this.vel[k + 2]) * visc;
        }
      }
      this.dens[i] = dens;
      // cheap curl/vorticity: swirl around Y proportional to density gradient noise
      const sw = p.vorticity * Math.min(1, dens * .3);
      fx += Math.sin(this.pos[k + 1] * 2.1 + this.pos[k + 2]) * sw; fz += Math.cos(this.pos[k] - this.pos[k + 1] * 1.7) * sw;
      this.vel[k] += fx * dt; this.vel[k + 1] += (fy + p.gravity) * dt; this.vel[k + 2] += fz * dt;
    }
    // integrate + two-way coupling: hitting the 2.5D surface registers an impact
    let write = 0;
    for (let i = 0; i < this.count; i++) {
      const k = i * 3; this.life[i] -= dt;
      const drag = 1 - p.drag * dt;
      this.vel[k] *= drag; this.vel[k + 2] *= drag;
      this.pos[k] += this.vel[k] * dt; this.pos[k + 1] += this.vel[k + 1] * dt; this.pos[k + 2] += this.vel[k + 2] * dt;
      const sy = surfaceY(this.pos[k], this.pos[k + 2]);
      if (this.pos[k + 1] <= sy && this.vel[k + 1] < 0) {
        const energy = Math.min(3, Math.abs(this.vel[k + 1]) * .45 + this.dens[i] * .12);
        if (energy > .35 && this.onImpact) this.onImpact(this.pos[k], this.pos[k + 2], energy, .5 + energy * .5);
        if (Math.random() < .25 * p.foamAmount) this.spawnFoam(this.pos[k], sy + .05, this.pos[k + 2], 1, energy);
        continue; // particle rejoins the 2.5D water
      }
      if (this.life[i] <= 0 || this.pos[k + 1] < sy - 4) continue;
      if (write !== i) { const w = write * 3;
        for (let a = 0; a < 3; a++) { this.pos[w + a] = this.pos[k + a]; this.vel[w + a] = this.vel[k + a]; }
        this.life[write] = this.life[i]; this.dens[write] = this.dens[i]; }
      write++;
    }
    this.count = write;
    // foam rides the surface, spreads, dissolves
    this.foam = this.foam.filter(f => (f.life -= dt) > 0);
    for (const f of this.foam) { f.x += f.vx * dt; f.z += f.vz * dt; f.vx *= .96; f.vz *= .96; f.y = surfaceY(f.x, f.z) + .06; }
    this.render();
  }
  render() {
    const T = this.THREE, size = .16 * this.params.splashSize;
    for (let i = 0; i < this.count; i++) { const k = i * 3;
      const s = size * (0.7 + Math.min(1, this.dens[i]) * .7);
      this._p.set(this.pos[k], this.pos[k + 1], this.pos[k + 2]); this._s.set(s, s * .85, s);
      this._m.compose(this._p, this._q, this._s); this.mesh.setMatrixAt(i, this._m); }
    this.mesh.count = this.count; this.mesh.instanceMatrix.needsUpdate = true;
    this.foam.forEach((f, i) => { this._p.set(f.x, f.y, f.z); const s = f.size * Math.min(1, f.life);
      this._s.set(s, s * .35, s); this._m.compose(this._p, this._q, this._s); this.foamMesh.setMatrixAt(i, this._m); });
    this.foamMesh.count = this.foam.length; this.foamMesh.instanceMatrix.needsUpdate = true;
  }
}

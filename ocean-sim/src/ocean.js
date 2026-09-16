import * as THREE from 'three';
import { GRID_HALF } from './config.js?v=5';
import { OCEAN_VS, OCEAN_FS } from './glsl.js?v=5';

// Camera-grade graded grid: dense at the center, coarse at the rim.
export function buildGradedGrid(n, half, power) {
  const pos = new Float32Array(n * n * 3);
  const flat = new Float32Array(n * n);
  let p = 0;
  for (let j = 0; j < n; j++) {
    const v = (j / (n - 1)) * 2 - 1;
    const z = Math.sign(v) * Math.pow(Math.abs(v), power) * half;
    for (let i = 0; i < n; i++) {
      const u = (i / (n - 1)) * 2 - 1;
      const x = Math.sign(u) * Math.pow(Math.abs(u), power) * half;
      pos[p * 3] = x; pos[p * 3 + 1] = 0; pos[p * 3 + 2] = z;
      flat[p] = 0;
      p++;
    }
  }
  const idx = new Uint32Array((n - 1) * (n - 1) * 6);
  let q = 0;
  for (let j = 0; j < n - 1; j++) {
    for (let i = 0; i < n - 1; i++) {
      const a = j * n + i, b = a + 1, c = a + n, d = c + 1;
      idx[q++] = a; idx[q++] = c; idx[q++] = b;
      idx[q++] = b; idx[q++] = c; idx[q++] = d;
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('aFlat', new THREE.BufferAttribute(flat, 1));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  return g;
}

export function buildFarFrame() {
  // Four flat panels forming a frame OUTSIDE the near grid (no overlap, so a
  // storm trough can never clip through). aFlat=1 skips wave displacement.
  const S = 9000, E = 640;
  const pieces = [
    { w: 2 * S, d: S - E, x: 0, z: -(E + (S - E) / 2) },
    { w: 2 * S, d: S - E, x: 0, z: (E + (S - E) / 2) },
    { w: S - E, d: 2 * E, x: -(E + (S - E) / 2), z: 0 },
    { w: S - E, d: 2 * E, x: (E + (S - E) / 2), z: 0 },
  ];
  const geos = [];
  for (const pc of pieces) {
    const g = new THREE.PlaneGeometry(pc.w, pc.d, 1, 1);
    g.rotateX(-Math.PI / 2);
    g.translate(pc.x, 0, pc.z);
    const n = g.getAttribute('position').count;
    g.setAttribute('aFlat', new THREE.BufferAttribute(new Float32Array(n).fill(1), 1));
    geos.push(g);
  }
  return geos;
}

export const QUALITY_GRID = { low: 160, medium: 224, high: 288, ultra: 384 };

export class Ocean {
  constructor(uniforms) {
    this.material = new THREE.ShaderMaterial({
      uniforms,
      vertexShader: OCEAN_VS,
      fragmentShader: OCEAN_FS,
      side: THREE.DoubleSide,
      transparent: true,
      depthWrite: true,
    });
    this.mesh = new THREE.Mesh(buildGradedGrid(QUALITY_GRID.high, GRID_HALF, 2.0), this.material);
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = 1;
    this.far = new THREE.Group();
    for (const g of buildFarFrame()) {
      const m = new THREE.Mesh(g, this.material);
      m.position.y = -0.5;
      m.frustumCulled = false;
      m.renderOrder = 0;
      this.far.add(m);
    }
  }

  setQuality(q) {
    const n = QUALITY_GRID[q] || QUALITY_GRID.high;
    this.mesh.geometry.dispose();
    this.mesh.geometry = buildGradedGrid(n, GRID_HALF, 2.0);
  }

  addTo(scene) {
    scene.add(this.mesh);
    scene.add(this.far);
  }
}

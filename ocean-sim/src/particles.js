import * as THREE from 'three';
import { PARAMS } from './config.js';
import { POINTS_VS, POINTS_FS } from './glsl.js';
import { reefXAt, shoreXAt } from './bathy.js';

export const PMAX = 60000;

// Spray ONLY at active breakers — the persistent foam lives in the FoamSim
// advection buffer now. Classes: 0 = reef-breaker spray, 1 = shoreline burst.
function mulberry(seed) {
  let s = seed | 0;
  return function () {
    s = (s + 0x6D2B79F5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export class SprayParticles {
  constructor(uniforms) {
    this.material = new THREE.ShaderMaterial({
      uniforms,
      vertexShader: POINTS_VS,
      fragmentShader: POINTS_FS,
      transparent: true,
      depthWrite: false,
      depthTest: true,
    });
    const pos = new Float32Array(PMAX * 3);
    const seed = new Float32Array(PMAX * 4);
    this.buildAnchors(pos, seed);
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('aSeed', new THREE.BufferAttribute(seed, 4));
    this.points = new THREE.Points(g, this.material);
    this.points.frustumCulled = false;
    this.points.renderOrder = 5;
    this.setCount(PARAMS.particles);
  }

  // Deterministic from seed; follows live reef/shore params so spray tracks
  // the break even when the user reshapes it.
  buildAnchors(pos, seed) {
    const rnd = mulberry(4242);
    const cls = new Uint8Array(PMAX);
    const bound = Math.floor(PMAX * 0.75);
    for (let i = 0; i < PMAX; i++) cls[i] = i < bound ? 0 : 1;
    for (let i = PMAX - 1; i > 0; i--) {
      const j = (rnd() * (i + 1)) | 0;
      const t = cls[i]; cls[i] = cls[j]; cls[j] = t;
    }
    for (let i = 0; i < PMAX; i++) {
      const c = cls[i];
      let x, z;
      if (c === 0) {
        z = (rnd() * 2 - 1) * 170;
        x = reefXAt(z) + rnd() * 24 - 8;
      } else {
        z = (rnd() * 2 - 1) * 250;
        x = shoreXAt(z) + rnd() * 10 - 5;
      }
      pos[i * 3] = x; pos[i * 3 + 1] = 0; pos[i * 3 + 2] = z;
      seed[i * 4] = rnd(); seed[i * 4 + 1] = rnd();
      seed[i * 4 + 2] = rnd(); seed[i * 4 + 3] = c;
    }
  }

  rebuild() {
    const g = this.points.geometry;
    this.buildAnchors(g.getAttribute('position').array, g.getAttribute('aSeed').array);
    g.getAttribute('position').needsUpdate = true;
    g.getAttribute('aSeed').needsUpdate = true;
  }

  setCount(n) {
    this.points.geometry.setDrawRange(0, Math.min(Math.max(n | 0, 0), PMAX));
  }

  addTo(scene) {
    scene.add(this.points);
  }
}

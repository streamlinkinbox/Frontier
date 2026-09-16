import * as THREE from 'three';
import { PARAMS } from './config.js';
import { POINTS_VS, POINTS_FS } from './glsl.js';
import { bathyJS, reefXAt, shoreXAt } from './bathy.js';

export const PMAX = 200000;

// Anchor classes: 0 spray, 1 crest foam, 2 shoreline wash, 3 advected streaks.
const CLASS_FRAC = [0.35, 0.30, 0.15, 0.20];

function mulberry(seed) {
  let s = seed | 0;
  return function () {
    s = (s + 0x6D2B79F5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export class FoamParticles {
  constructor(uniforms) {
    this.material = new THREE.ShaderMaterial({
      uniforms,
      vertexShader: POINTS_VS,
      fragmentShader: POINTS_FS,
      transparent: true,
      depthWrite: false,
      depthTest: true,
    });
    const rnd = mulberry(4242);
    const pos = new Float32Array(PMAX * 3);
    const seed = new Float32Array(PMAX * 4);
    // shuffled class assignment so any draw-range prefix stays representative
    const cls = new Uint8Array(PMAX);
    let c0 = 0;
    const bounds = [CLASS_FRAC[0] * PMAX, (CLASS_FRAC[0] + CLASS_FRAC[1]) * PMAX,
      (CLASS_FRAC[0] + CLASS_FRAC[1] + CLASS_FRAC[2]) * PMAX];
    for (let i = 0; i < PMAX; i++) {
      cls[i] = i < bounds[0] ? 0 : i < bounds[1] ? 1 : i < bounds[2] ? 2 : 3;
    }
    for (let i = PMAX - 1; i > 0; i--) {
      const j = (rnd() * (i + 1)) | 0;
      const t = cls[i]; cls[i] = cls[j]; cls[j] = t;
    }
    for (let i = 0; i < PMAX; i++) {
      const c = cls[i];
      let x = 0, z = 0;
      if (c === 0) {
        z = (rnd() * 2 - 1) * 170;
        x = reefXAt(z) + rnd() * 30 - 10;
      } else if (c === 1) {
        for (let tries = 0; tries < 8; tries++) {
          x = -350 + rnd() * 450; z = (rnd() * 2 - 1) * 300;
          if (bathyJS(x, z) > 2) break;
        }
      } else if (c === 2) {
        z = (rnd() * 2 - 1) * 250;
        x = shoreXAt(z) + rnd() * 14 - 7;
      } else {
        for (let tries = 0; tries < 8; tries++) {
          x = -250 + rnd() * 300; z = (rnd() * 2 - 1) * 250;
          if (bathyJS(x, z) > 1.5) break;
        }
      }
      pos[i * 3] = x; pos[i * 3 + 1] = 0; pos[i * 3 + 2] = z;
      seed[i * 4] = rnd(); seed[i * 4 + 1] = rnd(); seed[i * 4 + 2] = rnd(); seed[i * 4 + 3] = c;
      c0++;
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('aSeed', new THREE.BufferAttribute(seed, 4));
    this.points = new THREE.Points(g, this.material);
    this.points.frustumCulled = false;
    this.points.renderOrder = 5;
    this.setCount(PARAMS.particles);
  }

  setCount(n) {
    this.points.geometry.setDrawRange(0, Math.min(Math.max(n | 0, 0), PMAX));
  }

  addTo(scene) {
    scene.add(this.points);
  }
}

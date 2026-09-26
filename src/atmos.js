import * as THREE from 'three';
import { terrainHeight } from './terrain.js';
import { TANKS, TRUCKS, CAR_WRECKS, CRATERS } from './layout.js';

// ---------------------------------------------------------------------------
// Battlefield atmosphere: smoke columns over wrecks, drifting low-poly clouds,
// static shell craters. All cheap, all flat-shaded.
// ---------------------------------------------------------------------------

const SMOKE_GEO = new THREE.IcosahedronGeometry(1, 0);
const SMOKE_MAT = new THREE.MeshBasicMaterial({
  color: '#6a6a68', transparent: true, opacity: 0.32, depthWrite: false,
});
const FIRE_MAT = new THREE.MeshBasicMaterial({
  color: '#e2732e', transparent: true, opacity: 0.75, depthWrite: false,
});

const CLOUD_MAT = new THREE.MeshStandardMaterial({
  color: '#f4f7f6', flatShading: true, roughness: 1,
});

const CRATER_MAT = new THREE.MeshStandardMaterial({ color: '#4d4032', flatShading: true, roughness: 1 });
const CRATER_RIM = new THREE.MeshStandardMaterial({ color: '#8d785c', flatShading: true, roughness: 1 });

export function buildAtmosphere(scene) {
  const group = new THREE.Group();
  group.name = 'atmosphere';
  scene.add(group);

  const smokePuffs = [];

  // --- smoke columns over the wrecks ---
  const wrecks = [
    ...TANKS.filter((t) => t.variant === 'wreck').map((t) => ({ x: t.x, z: t.z, fire: true })),
    ...TRUCKS.filter((t) => t.variant === 'wreck').map((t) => ({ x: t.x, z: t.z, fire: true })),
    ...CAR_WRECKS.map((c) => ({ x: c.x, z: c.z, fire: false })),
  ];

  for (const w of wrecks) {
    const baseY = terrainHeight(w.x, w.z) + 1.6;
    const n = 7;
    for (let i = 0; i < n; i++) {
      const m = new THREE.Mesh(SMOKE_GEO, SMOKE_MAT.clone());
      const phase = (i / n);
      const scale = 0.55 + phase * 1.9;
      m.scale.setScalar(scale);
      m.position.set(w.x, baseY + phase * 9, w.z);
      group.add(m);
      smokePuffs.push({
        mesh: m,
        x: w.x,
        z: w.z,
        baseY,
        phase,
        speed: 0.45 + (i % 3) * 0.08,
        rise: 9.5,
        drift: 0.9 + (i % 2) * 0.5,
      });
    }
    if (w.fire) {
      const flame = new THREE.Mesh(SMOKE_GEO, FIRE_MAT);
      flame.scale.setScalar(0.7);
      flame.position.set(w.x, baseY - 0.7, w.z);
      group.add(flame);
      smokePuffs.push({
        mesh: flame, x: w.x, z: w.z, baseY: baseY - 0.7,
        phase: 0, speed: 0, rise: 0.001, drift: 0, flame: true,
      });
    }
  }

  // --- drifting low-poly clouds ---
  const clouds = [];
  const cloudDefs = [
    { x: -180, y: 150, z: -80, s: 22 },
    { x: -60, y: 170, z: 120, s: 28 },
    { x: 90, y: 145, z: -40, s: 20 },
    { x: 210, y: 165, z: 180, s: 26 },
    { x: -240, y: 155, z: 220, s: 24 },
    { x: 40, y: 185, z: 280, s: 30 },
    { x: 150, y: 138, z: 60, s: 18 },
    { x: -120, y: 172, z: -180, s: 25 },
  ];

  for (const c of cloudDefs) {
    const cloud = new THREE.Group();
    const parts = 3 + Math.floor((c.s % 3));
    for (let i = 0; i < parts; i++) {
      const puff = new THREE.Mesh(SMOKE_GEO, CLOUD_MAT);
      const ang = (i / parts) * Math.PI * 2;
      puff.position.set(Math.cos(ang) * c.s * 0.55, Math.sin(i * 2.3) * c.s * 0.1, Math.sin(ang) * c.s * 0.35);
      puff.scale.set(c.s * (0.5 + (i % 2) * 0.25), c.s * 0.24, c.s * 0.42);
      cloud.add(puff);
    }
    cloud.position.set(c.x, c.y, c.z);
    group.add(cloud);
    clouds.push({ mesh: cloud, speed: 1.1 + (c.s % 5) * 0.14 });
  }

  // --- static shell craters ---
  for (const cr of CRATERS) {
    const y = terrainHeight(cr.x, cr.z);
    const bowl = new THREE.Mesh(new THREE.CylinderGeometry(cr.r * 0.72, cr.r, 0.14, 9), CRATER_MAT);
    bowl.position.set(cr.x, y + 0.04, cr.z);
    const rim = new THREE.Mesh(new THREE.RingGeometry(cr.r * 0.9, cr.r * 1.28, 9), CRATER_RIM);
    rim.rotation.x = -Math.PI / 2;
    rim.position.set(cr.x, y + 0.1, cr.z);
    bowl.receiveShadow = true;
    rim.receiveShadow = true;
    group.add(bowl, rim);
  }

  return {
    group,
    update(time) {
      for (const p of smokePuffs) {
        if (p.flame) {
          const k = 0.75 + Math.sin(time * 9 + p.x) * 0.18;
          p.mesh.scale.setScalar(k);
          p.mesh.material.opacity = 0.55 + Math.sin(time * 11 + p.z) * 0.2;
          continue;
        }
        const frac = (time * p.speed * 0.06 + p.phase) % 1;
        p.mesh.position.set(
          p.x + frac * p.drift + Math.sin(time * 0.35 + p.phase * 7) * 0.5,
          p.baseY + frac * p.rise,
          p.z + Math.cos(time * 0.3 + p.phase * 5) * 0.4,
        );
        const s = 0.55 + frac * 2.1;
        p.mesh.scale.setScalar(s);
        p.mesh.material.opacity = 0.34 * (1 - frac * 0.85);
      }
      for (const c of clouds) {
        c.mesh.position.x += c.speed * 0.016;
        if (c.mesh.position.x > 320) c.mesh.position.x = -320;
      }
    },
  };
}

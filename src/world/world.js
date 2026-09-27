/**
 * THE DEPOT — a fuel yard at last light.
 *
 * The stage is built specifically to exercise the locomotion system: rough
 * broken ground, a smooth poured-concrete pad, a vertical concrete wall, a
 * glass inspection panel (the "no purchase required" smooth-surface test), and
 * a riveted steel fuel tank with a filler vent the mosquito probes.
 *
 * Scale: 1 unit = 1 mm. The insect is ~5 mm long, so the tank is 760 mm wide
 * and 1400 mm tall — the mosquito is genuinely tiny, which is the point.
 */

import * as THREE from 'three';
import { SurfaceField, Heightfield, CylinderShell, BoxSurface, DiscSurface, TubeSurface, CapDisc } from './surface.js';
import { makeTerrainMap, makeTankMap, makeStencilAtlas, fbmField, mulberry } from '../render/textures.js';

export const WORLD = {
  extent: 2400,          // half-size of the walkable ground patch, mm
  tank: { x: 0, z: -260, r: 380, y0: -40, y1: 1400 },
  wall: { min: [420, 0, 380], max: [1500, 1500, 640] },
  glass: { min: [-1500, 0, 300], max: [-700, 1100, 340] },
  crate: { min: [-150, 0, 560], max: [330, 340, 1010] },
  pad: { x: 250, z: 420, r: 560 },   // smooth concrete pad
};

/* ------------------------------------------------- height function ---- */

function buildHeightField() {
  const N = 513;                     // 513² samples over ±2400 mm
  const ext = WORLD.extent;
  const h = new Float32Array(N * N);
  const n1 = fbmField(N, N, 5, 1337, 0.52);
  const n2 = fbmField(N, N, 4, 4242, 0.5);
  const n3 = fbmField(N, N, 3, 909, 0.45);

  for (let j = 0; j < N; j++) {
    const z = (j / (N - 1)) * 2 * ext - ext;
    for (let i = 0; i < N; i++) {
      const x = (i / (N - 1)) * 2 * ext - ext;
      const k = j * N + i;
      const r = Math.hypot(x, z);

      // Base rolling ground
      let y = (n1[k] - 0.5) * 78;

      // A rubble ridge sweeping across the west of the yard
      const ridge = Math.exp(-Math.pow((x + 900) / 620, 2)) * Math.exp(-Math.pow((z - 200) / 900, 2));
      y += ridge * 46 * (n2[k] - 0.35);

      // Coarse gravel bumps
      y += (n2[k] - 0.5) * 16 * Math.min(1, r / 500);

      // Fine chip texture — this is what the feet actually feel
      y += (n3[k] - 0.5) * 3.2;

      // Smooth poured pad: flatten to a gentle crown
      const pd = Math.hypot(x - WORLD.pad.x, z - WORLD.pad.z);
      if (pd < WORLD.pad.r) {
        const t = 1 - Math.min(1, pd / WORLD.pad.r);
        const smooth = t * t * (3 - 2 * t);
        const padY = 8 + 5 * Math.exp(-Math.pow(pd / (WORLD.pad.r * 0.6), 2));
        y = y * (1 - smooth) + padY * smooth;
        // Pad joint lines
        const jx = Math.abs(((x - WORLD.pad.x) % 420 + 420) % 420 - 210);
        const jz = Math.abs(((z - WORLD.pad.z) % 420 + 420) % 420 - 210);
        const joint = Math.min(jx, jz);
        if (joint < 2.2) y -= (2.2 - joint) * 0.55 * smooth;
      }

      // Flatten a landing/approach apron in front of the wall
      const ad = Math.hypot(x - 900, z - 900);
      if (ad < 320) { const t = 1 - ad / 320; y = y * (1 - t * 0.85) + y * 0.0 * (t * 0.85) + (y * 0.15) * (t * 0.85); }

      // Tank pad: level ground right under the hull
      const td = Math.hypot(x - WORLD.tank.x, z - WORLD.tank.z);
      if (td < WORLD.tank.r + 190) {
        const t = 1 - Math.min(1, (td - WORLD.tank.r) / 190);
        const s = t * t * (3 - 2 * t);
        const ap = 14 + 3 * Math.sin(x * 0.01) * Math.cos(z * 0.01);
        y = y * (1 - s) + ap * s;
      }

      h[k] = y;
    }
  }

  const cell = (2 * ext) / (N - 1);
  const sample = (x, z) => {
    const fx = (x + ext) / cell, fz = (z + ext) / cell;
    const i = Math.max(0, Math.min(N - 2, Math.floor(fx)));
    const j = Math.max(0, Math.min(N - 2, Math.floor(fz)));
    const tx = Math.max(0, Math.min(1, fx - i)), tz = Math.max(0, Math.min(1, fz - j));
    const a = h[j * N + i], b = h[j * N + i + 1];
    const c = h[(j + 1) * N + i], d = h[(j + 1) * N + i + 1];
    return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
  };
  const gradient = (x, z, out) => {
    const e = 4.0;
    const dx = (sample(x + e, z) - sample(x - e, z)) / (2 * e);
    const dz = (sample(x, z + e) - sample(x, z - e)) / (2 * e);
    return out.set(-dx, 1, -dz).normalize();
  };
  return { sample, gradient, data: h, N, cell, ext };
}

/* ------------------------------------------------------------ sky/env */

function buildSky() {
  const geo = new THREE.SphereGeometry(12000, 40, 24);
  const mat = new THREE.ShaderMaterial({
    side: THREE.BackSide,
    depthWrite: false,
    uniforms: {
      uSun: { value: new THREE.Vector3(-0.42, 0.30, 0.86).normalize() },
      uTop: { value: new THREE.Color(0x2b4a72) },
      uHorizon: { value: new THREE.Color(0xd8a05a) },
      uGround: { value: new THREE.Color(0x2a231c) },
    },
    vertexShader: `
      varying vec3 vDir;
      void main(){ vDir = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }
    `,
    fragmentShader: `
      uniform vec3 uSun, uTop, uHorizon, uGround;
      varying vec3 vDir;
      void main(){
        float h = vDir.y;
        vec3 sky = mix(uHorizon, uTop, pow(clamp(h,0.0,1.0), 0.55));
        vec3 col = mix(uGround, sky, smoothstep(-0.12, 0.02, h));
        // Sun disc + broad glow
        float d = max(dot(vDir, normalize(uSun)), 0.0);
        col += vec3(1.0,0.72,0.42) * pow(d, 900.0) * 6.0;
        col += vec3(1.0,0.62,0.34) * pow(d, 12.0) * 0.35;
        // Warm band at the horizon
        col += vec3(0.9,0.52,0.24) * exp(-abs(h)*9.0) * 0.35;
        gl_FragColor = vec4(col, 1.0);
      }
    `,
  });
  const m = new THREE.Mesh(geo, mat);
  m.name = 'Sky';
  return m;
}

/* ================================================================ */

/**
 * Analytic surfaces for the fuel-vent stub, matching the rendered mesh: a tube
 * whose axis is radial out of the hull, the cap disc the mosquito actually
 * stands on, and the flange the stub grows out of.
 *
 * Exported so the headless suite can prove the vent is genuinely standable
 * without needing a WebGL renderer for the rest of the world. Without these
 * the vent is decoration — the leg planner would find only the hull underneath
 * and the insect could never reach the filler neck.
 */
export function addVentSurfaces(field, VENT, T, ventR) {
  const outward = new THREE.Vector3(VENT.x - T.x, 0, VENT.z - T.z).normalize();
  const base = new THREE.Vector3(VENT.x, VENT.y, VENT.z);
  // The mesh stack is 66 long and centred, its cap face sits at +38+8 = +46
  // and the breather hole opens at +45.
  const stubBase = base.clone().addScaledVector(outward, -33);
  const stubTop = base.clone().addScaledVector(outward, 46);
  field.add(new TubeSurface({ c0: stubBase.toArray(), c1: stubTop.toArray(), r: ventR, name: 'vent' }));
  field.add(new CapDisc({ c: stubTop.toArray(), axis: outward.toArray(), r: ventR * 0.92, hole: 6, name: 'ventCap' }));
  field.add(new CapDisc({
    c: base.clone().addScaledVector(outward, -39).toArray(),
    axis: outward.toArray().map((v) => -v), r: ventR * 1.35, name: 'ventFlange',
  }));
  return { outward, stubTop, stubBase };
}

export function buildWorld(renderer, scene, quality = 1) {
  const group = new THREE.Group();
  group.name = 'World';
  scene.add(group);

  const field = new SurfaceField();
  const HF = buildHeightField();
  const surface = field.add(new Heightfield({
    fn: HF.sample, gradient: HF.gradient, extent: WORLD.extent + 200, name: 'ground',
  }));

  /* --------------------------------------------------------- lighting */
  const sky = buildSky();
  group.add(sky);

  const sun = new THREE.DirectionalLight(0xffd2a0, 3.1);
  sun.position.set(-2400, 1700, 3600);
  sun.castShadow = true;
  const sh = 2048;
  sun.shadow.camera.left = -1600; sun.shadow.camera.right = 1600;
  sun.shadow.camera.top = 1600; sun.shadow.camera.bottom = -1600;
  sun.shadow.camera.near = 500; sun.shadow.camera.far = 9000;
  sun.shadow.mapSize.set(sh, sh);
  sun.shadow.bias = -0.0008;
  sun.shadow.normalBias = 1.2;
  group.add(sun);
  group.add(sun.target);
  sun.target.position.set(0, 0, 0);

  // Cool bounce from the opposite side keeps the shadowed cuticle readable.
  const fill = new THREE.DirectionalLight(0x88a8d0, 0.75);
  fill.position.set(2600, 900, -2200);
  group.add(fill);
  const hemi = new THREE.HemisphereLight(0x9dc0e8, 0x3a3026, 0.55);
  group.add(hemi);

  // IBL generated from the sky itself
  const pmrem = new THREE.PMREMGenerator(renderer);
  pmrem.compileEquirectangularShader();
  const envScene = new THREE.Scene();
  const envSky = buildSky();
  envScene.add(envSky);
  const envRT = pmrem.fromScene(envScene, 0.04);
  const envMap = envRT.texture;
  pmrem.dispose();
  envSky.geometry.dispose();
  scene.environment = envMap;
  scene.environmentIntensity = 0.95;

  /* ---------------------------------------------------------- terrain */
  const terrainMat = new THREE.MeshStandardMaterial({
    map: makeTerrainMap(quality >= 1 ? 1024 : 512),
    roughness: 0.94, metalness: 0.0,
    envMapIntensity: 0.55,
  });
  terrainMat.map.repeat.set(26, 26);

  const SEG = quality >= 1 ? 300 : 180;
  const terrainGeo = new THREE.PlaneGeometry(WORLD.extent * 2, WORLD.extent * 2, SEG, SEG);
  terrainGeo.rotateX(-Math.PI / 2);
  {
    const pos = terrainGeo.attributes.position;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i), z = pos.getZ(i);
      pos.setY(i, HF.sample(x, z));
    }
    terrainGeo.computeVertexNormals();
  }
  const terrain = new THREE.Mesh(terrainGeo, terrainMat);
  terrain.receiveShadow = true;
  terrain.name = 'Terrain';
  group.add(terrain);

  // A flat skirt out to the horizon so the ground never ends in mid-air.
  const skirt = new THREE.Mesh(
    new THREE.RingGeometry(WORLD.extent * 0.98, 9000, 64, 1),
    new THREE.MeshStandardMaterial({ color: 0x3a3129, roughness: 1.0, metalness: 0 }));
  skirt.rotateX(-Math.PI / 2);
  skirt.position.y = -6;
  group.add(skirt);

  /* ------------------------------------------------------------- tank */
  const T = WORLD.tank;
  const tankMat = new THREE.MeshStandardMaterial({
    map: makeTankMap(quality >= 1 ? 1024 : 512),
    roughness: 0.55, metalness: 0.42, envMapIntensity: 1.0,
  });
  tankMat.map.repeat.set(4, 2);
  field.add(new CylinderShell({ x: T.x, z: T.z, y0: T.y0, y1: T.y1, r: T.r, name: 'tank' }));

  const tank = new THREE.Group();
  tank.name = 'FuelTank';
  tank.position.set(T.x, 0, T.z);
  {
    const shell = new THREE.Mesh(
      new THREE.CylinderGeometry(T.r, T.r * 1.02, T.y1 - T.y0, 96, 1, true), tankMat);
    shell.position.y = (T.y0 + T.y1) / 2;
    shell.castShadow = true; shell.receiveShadow = true;
    tank.add(shell);
    field.add(new CylinderShell({ x: T.x, z: T.z, y0: T.y1, y1: T.y1, r: T.r, name: 'tank' }));

    // Lid
    const lid = new THREE.Mesh(new THREE.CircleGeometry(T.r, 96), tankMat);
    lid.rotation.x = -Math.PI / 2;
    lid.position.y = T.y1;
    lid.receiveShadow = true;
    tank.add(lid);
    field.add(new DiscSurface({ x: T.x, y: T.y1, z: T.z, r: T.r, name: 'tankLid' }));

    // Rolled reinforcing ribs
    for (const y of [220, 620, 1020]) {
      const rib = new THREE.Mesh(
        new THREE.TorusGeometry(T.r + 9, 9, 10, 96),
        new THREE.MeshStandardMaterial({ color: 0x3e4a3c, roughness: 0.6, metalness: 0.5 }));
      rib.rotation.x = Math.PI / 2;
      rib.position.y = y;
      rib.castShadow = true;
      tank.add(rib);
    }
    // Saddle feet
    for (const s of [1, -1]) {
      const sad = new THREE.Mesh(
        new THREE.BoxGeometry(T.r * 0.7, 90, 130),
        new THREE.MeshStandardMaterial({ color: 0x33332e, roughness: 0.85, metalness: 0.3 }));
      sad.position.set(0, 45, s * T.r * 0.85);
      sad.castShadow = true; sad.receiveShadow = true;
      tank.add(sad);
    }
    // Stencils
    const stencil = new THREE.Mesh(
      new THREE.PlaneGeometry(520, 520),
      new THREE.MeshBasicMaterial({ map: makeStencilAtlas(512), transparent: true, depthWrite: false, opacity: 0.55 }));
    const atlas = stencil.material.map;
    const pick = (i) => {
      const c = document.createElement('canvas'); c.width = c.height = 256;
      const g = c.getContext('2d');
      g.drawImage(atlas.image, (i % 2) * 256, Math.floor(i / 2) * 256, 256, 256, 0, 0, 256, 256);
      const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
    };
    for (let i = 0; i < 3; i++) {
      const m = new THREE.Mesh(new THREE.PlaneGeometry(300, 300),
        new THREE.MeshBasicMaterial({ map: pick(i), transparent: true, depthWrite: false, opacity: 0.6 }));
      const a = (i / 3) * Math.PI * 2 + 0.6;
      m.position.set(Math.cos(a) * (T.r + 2), 820, Math.sin(a) * (T.r + 2));
      m.lookAt(0, 820, 0);
      m.rotateY(Math.PI);
      tank.add(m);
    }
  }
  group.add(tank);

  /* ------------------------------- the fuel vent (the feeding target) */
  const vent = new THREE.Group();
  vent.name = 'FuelVent';
  const VENT = { x: T.x + T.r * 0.55, y: 640, z: T.z + T.r * 0.84 };
  const ventR = 34;
  {
    const stack = new THREE.Mesh(
      new THREE.CylinderGeometry(ventR, ventR * 1.08, 66, 32),
      new THREE.MeshStandardMaterial({ color: 0x5a5f52, roughness: 0.42, metalness: 0.75, envMapIntensity: 1.2 }));
    stack.castShadow = true;
    vent.add(stack);
    const flange = new THREE.Mesh(
      new THREE.CylinderGeometry(ventR * 1.35, ventR * 1.35, 12, 32),
      new THREE.MeshStandardMaterial({ color: 0x4a4e44, roughness: 0.4, metalness: 0.8 }));
    flange.position.y = -33;
    vent.add(flange);
    // Cap with a central breather hole — this is what the stylet enters
    const cap = new THREE.Mesh(
      new THREE.CylinderGeometry(ventR * 0.92, ventR * 0.92, 16, 32),
      new THREE.MeshStandardMaterial({ color: 0x646a5c, roughness: 0.38, metalness: 0.85 }));
    cap.position.y = 38;
    vent.add(cap);
    const hole = new THREE.Mesh(
      new THREE.CylinderGeometry(6, 7, 18, 20),
      new THREE.MeshStandardMaterial({ color: 0x06070a, roughness: 0.15, metalness: 0.9 }));
    hole.position.y = 45;
    vent.add(hole);
    vent.position.set(VENT.x, VENT.y, VENT.z);
    // Orient the stack so its axis points away from the hull (the mosquito
    // has to negotiate a re-entrant 90° corner to reach it — exactly the
    // hard case for a leg planner).
    const outward = new THREE.Vector3(VENT.x - T.x, 0, VENT.z - T.z).normalize();
    const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), outward);
    vent.quaternion.copy(q);
  }
  group.add(vent);

  addVentSurfaces(field, VENT, T, ventR);

  /* -------------------------------------------------------------- wall */
  const W = WORLD.wall;
  field.add(new BoxSurface({ min: W.min, max: W.max, name: 'wall' }));
  {
    const mat = new THREE.MeshStandardMaterial({
      map: makeTerrainMap(512), color: 0x9a978d, roughness: 0.92, metalness: 0.0,
    });
    mat.map.repeat.set(4, 4);
    const wall = new THREE.Mesh(new THREE.BoxGeometry(W.max[0] - W.min[0], W.max[1] - W.min[1], W.max[2] - W.min[2]), mat);
    wall.position.set((W.min[0] + W.max[0]) / 2, (W.min[1] + W.max[1]) / 2, (W.min[2] + W.max[2]) / 2);
    wall.castShadow = true; wall.receiveShadow = true;
    wall.name = 'Wall';
    group.add(wall);
    // Rebar / formwork seams so it is not a blank slab
    for (let i = 0; i < 7; i++) {
      const seam = new THREE.Mesh(
        new THREE.BoxGeometry(W.max[0] - W.min[0], 5, 3),
        new THREE.MeshStandardMaterial({ color: 0x5e5c56, roughness: 1 }));
      seam.position.set((W.min[0] + W.max[0]) / 2, W.min[1] + 160 + i * 190, W.min[2] - 1.5);
      group.add(seam);
    }
  }

  /* ------------------------------------------------------ glass panel */
  const G = WORLD.glass;
  {
    field.add(new BoxSurface({ min: G.min, max: G.max, name: 'glass' }));
    const glass = new THREE.Mesh(
      new THREE.BoxGeometry(G.max[0] - G.min[0], G.max[1] - G.min[1], G.max[2] - G.min[2]),
      new THREE.MeshPhysicalMaterial({
        color: 0xcfe2e6, roughness: 0.03, metalness: 0.0,
        transmission: 0.92, thickness: 6, ior: 1.5,
        transparent: true, opacity: 0.30, envMapIntensity: 1.6,
        side: THREE.DoubleSide,
      }));
    glass.position.set((G.min[0] + G.max[0]) / 2, (G.min[1] + G.max[1]) / 2, (G.min[2] + G.max[2]) / 2);
    glass.name = 'GlassPanel';
    group.add(glass);
    // Frame
    const fr = new THREE.MeshStandardMaterial({ color: 0x3a3d3c, roughness: 0.5, metalness: 0.7 });
    for (const [ax, val, sz] of [['x', G.min[0], [G.max[1] - G.min[1], 26, 30]], ['x', G.max[0], [G.max[1] - G.min[1], 26, 30]]]) {
      const bar = new THREE.Mesh(new THREE.BoxGeometry(sz[0], sz[1], sz[2]), fr);
      bar.position.set(val, (G.min[1] + G.max[1]) / 2, (G.min[2] + G.max[2]) / 2);
      bar.castShadow = true;
      group.add(bar);
    }
  }

  /* ------------------------------------------------------------ crate */
  const C = WORLD.crate;
  {
    field.add(new BoxSurface({ min: C.min, max: C.max, name: 'crate' }));
    const mat = new THREE.MeshStandardMaterial({ color: 0x6a5a42, roughness: 0.92 });
    const crate = new THREE.Mesh(new THREE.BoxGeometry(C.max[0] - C.min[0], C.max[1] - C.min[1], C.max[2] - C.min[2]), mat);
    crate.position.set((C.min[0] + C.max[0]) / 2, (C.min[1] + C.max[1]) / 2, (C.min[2] + C.max[2]) / 2);
    crate.castShadow = true; crate.receiveShadow = true;
    group.add(crate);
    const slat = new THREE.MeshStandardMaterial({ color: 0x4e412f, roughness: 0.95 });
    for (let i = 0; i < 4; i++) {
      const s = new THREE.Mesh(new THREE.BoxGeometry(C.max[0] - C.min[0] + 4, 14, C.max[2] - C.min[2] + 4), slat);
      s.position.set((C.min[0] + C.max[0]) / 2, C.min[1] + 30 + i * 80, (C.min[2] + C.max[2]) / 2);
      group.add(s);
    }
  }

  /* ------------------------------------------------------------ lights */
  scene.fog = new THREE.FogExp2(0xa9a08e, 0.00026);

  return { group, field, surface, tank, vent, sun, fill, hemi, envMap, heightField: HF, VENT, ventR };
}

import * as THREE from 'three';
import { SKEL, COLOR } from './anatomy.js';
import { loftGeometry } from './Body.js';
import { RigidFeathers } from './Feathers.js';

const lerp = (a, b, t) => a + (b - a) * t;
const clamp = (x, a, b) => Math.min(Math.max(x, a), b);

// Scale texture (height -> normal, plus albedo modulation) for bare skin of tarsus & toes.
// u around the limb (front = 0.5), v along it. Front: large transverse scutes ("scutellate");
// sides & back: small reticulate polygonal scales.
function scaleTextures(scuteRows, frontWidth, seed) {
  const W = 512, H = 512;
  const h = document.createElement('canvas'); h.width = W; h.height = H;
  const g = h.getContext('2d');
  g.fillStyle = '#000'; g.fillRect(0, 0, W, H);
  let s = seed; const R = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
  // reticulate scales
  const cell = 14;
  for (let y = 0; y < H + cell; y += cell * 0.86) for (let x = 0; x < W + cell; x += cell) {
    const ox = x + ((Math.round(y / (cell * 0.86)) % 2) * cell) / 2 + (R() - 0.5) * 4, oy = y + (R() - 0.5) * 4;
    const r = cell * (0.46 + R() * 0.1);
    const grd = g.createRadialGradient(ox, oy, 0, ox, oy, r);
    grd.addColorStop(0, 'rgb(230,230,230)'); grd.addColorStop(0.7, 'rgb(160,160,160)'); grd.addColorStop(1, 'rgb(0,0,0)');
    g.fillStyle = grd; g.beginPath(); g.arc(ox, oy, r, 0, Math.PI * 2); g.fill();
  }
  // front scutes: overlapping transverse plates
  const x0 = W * (0.5 - frontWidth / 2), x1 = W * (0.5 + frontWidth / 2);
  g.fillStyle = '#000'; g.fillRect(x0 - 4, 0, x1 - x0 + 8, H);
  const rowH = H / scuteRows;
  for (let i = 0; i < scuteRows; i++) {
    const y = i * rowH;
    const grd = g.createLinearGradient(0, y, 0, y + rowH);
    grd.addColorStop(0, 'rgb(40,40,40)'); grd.addColorStop(0.2, 'rgb(250,250,250)'); grd.addColorStop(0.8, 'rgb(180,180,180)'); grd.addColorStop(1, 'rgb(20,20,20)');
    g.fillStyle = grd;
    const r = 10;
    g.beginPath(); g.roundRect(x0 + (R() - 0.5) * 6, y + 1, x1 - x0, rowH - 2, r); g.fill();
  }
  const hd = g.getImageData(0, 0, W, H).data;
  const n = document.createElement('canvas'); n.width = W; n.height = H;
  const nx = n.getContext('2d'); const out = nx.createImageData(W, H);
  const a = document.createElement('canvas'); a.width = W; a.height = H;
  const ax = a.getContext('2d'); const ao = ax.createImageData(W, H);
  const at = (i, j) => hd[(((j + H) % H) * W + ((i + W) % W)) * 4] / 255;
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    const dx = at(i + 1, j) - at(i - 1, j), dy = at(i, j + 1) - at(i, j - 1);
    let vx = -dx * 3, vy = dy * 3, vz = 1; const l = Math.hypot(vx, vy, vz);
    const o = (j * W + i) * 4;
    out.data[o] = (vx / l * 0.5 + 0.5) * 255; out.data[o + 1] = (vy / l * 0.5 + 0.5) * 255; out.data[o + 2] = (vz / l * 0.5 + 0.5) * 255; out.data[o + 3] = 255;
    const hv = at(i, j);
    const c = 0.55 + 0.45 * Math.pow(hv, 0.5);   // creases darker
    ao.data[o] = c * 255; ao.data[o + 1] = c * 250; ao.data[o + 2] = c * 235; ao.data[o + 3] = 255;
  }
  nx.putImageData(out, 0, 0); ax.putImageData(ao, 0, 0);
  const nm = new THREE.CanvasTexture(n); nm.wrapS = nm.wrapT = THREE.RepeatWrapping; nm.anisotropy = 8;
  const am = new THREE.CanvasTexture(a); am.wrapS = am.wrapT = THREE.RepeatWrapping; am.colorSpace = THREE.SRGBColorSpace; am.anisotropy = 8;
  return { normalMap: nm, map: am };
}

// tube along a local axis with elliptical sections; profile(t) -> [rx, rz], axis: 'y-' or 'z+'
function limbTube(len, profile, axis, nR = 24, nS = 24, frontUp = false) {
  const rings = [];
  for (let i = 0; i < nR; i++) {
    const t = i / (nR - 1);
    const [rx, rz, off] = profile(t);
    const ring = [];
    for (let j = 0; j < nS; j++) {
      const a = (j / nS) * Math.PI * 2;
      const px = rx * Math.sin(a), pc = -rz * Math.cos(a) + (off || 0);
      if (axis === 'y-') ring.push(new THREE.Vector3(px, -t * len, pc));
      else ring.push(new THREE.Vector3(px, pc, t * len));   // front (u=0.5) on top
    }
    rings.push(ring);
  }
  return loftGeometry(rings, { capStart: true, capEnd: true });
}

export class Legs {
  constructor(rig, sys, skinned, body) {
    this.rig = rig; this.sys = sys;
    this.rigid = new RigidFeathers(sys);
    const tarsTex = scaleTextures(14, 0.34, 5);
    const toeTex = scaleTextures(6, 0.5, 9);
    const skin = (tex, rep) => {
      const m = new THREE.MeshPhysicalMaterial({ color: COLOR.feet, map: tex.map, normalMap: tex.normalMap, normalScale: new THREE.Vector2(0.9, 0.9), roughness: 0.55, clearcoat: 0.25, clearcoatRoughness: 0.5 });
      m.map = tex.map.clone(); m.map.repeat.set(1, rep); m.map.needsUpdate = true;
      m.normalMap = tex.normalMap.clone(); m.normalMap.repeat.set(1, rep); m.normalMap.needsUpdate = true;
      return m;
    };
    const tarsMat = skin(tarsTex, 1), toeMat = skin(toeTex, 1);
    const talonMat = new THREE.MeshPhysicalMaterial({ color: COLOR.talon, roughness: 0.28, clearcoat: 0.6, clearcoatRoughness: 0.2 });
    const thighMat = new THREE.MeshStandardMaterial({ color: COLOR.brown, roughness: 0.95 });
    this.meshes = [];
    for (const L of rig.legs) {
      // tibiotarsus ("drumstick"), hidden under trousers
      const tib = new THREE.Mesh(limbTube(SKEL.tibiotarsus + 0.01, (t) => [lerp(0.024, 0.012, t), lerp(0.027, 0.013, t), 0.004 * (1 - t)], 'y-'), thighMat);
      tib.castShadow = true; L.tibia.add(tib);
      // tarsometatarsus: stout, slightly flattened front-back; joint bulge at the top
      const tars = new THREE.Mesh(limbTube(SKEL.tarsometatarsus + 0.004, (t) => {
        const bul = Math.exp(-((t - 0.02) ** 2) / 0.004) * 0.003 + Math.exp(-((t - 1) ** 2) / 0.006) * 0.0025;
        return [0.0082 + bul, 0.0098 + bul, 0];
      }, 'y-', 30, 28), tarsMat);
      tars.castShadow = true; tars.receiveShadow = true; L.tarsus.add(tars);
      // foot pad (metatarsal pad) under the ball of the foot
      const pad = new THREE.Mesh(new THREE.SphereGeometry(1, 20, 14), toeMat);
      pad.scale.set(0.011, 0.007, 0.012); pad.position.set(0, -0.004, 0.002); L.foot.add(pad);
      // toes
      for (const T of L.toes) {
        const n = T.ph.length;
        for (let k = 0; k < n; k++) {
          const len = T.def.phal[k];
          const r0 = lerp(0.0072, 0.0052, k / Math.max(1, n - 1)) * (T.def.name === 'IV' ? 0.9 : 1);
          const g = limbTube(len + 0.004, (t) => {
            // ventral digital pad bulges downward mid-segment
            const pad = 0.0022 * Math.sin(Math.PI * t);
            return [r0 * (1 - 0.1 * t), r0 * (1 - 0.1 * t) + pad * 0.5, -pad * 0.5];
          }, 'z+', 14, 20, true);
          g.translate(0, 0, -0.002);
          const m = new THREE.Mesh(g, toeMat); m.castShadow = true; m.receiveShadow = true;
          T.ph[k].add(m);
        }
        // talon: curved, laterally compressed keratin claw
        const tm = new THREE.Mesh(this._talon(T.def.talon, lerp(0.0048, 0.0056, T.def.talon / 0.048)), talonMat);
        tm.castShadow = true; T.claw.add(tm);
      }
      this._trousers(L);
    }
  }

  _talon(len, base) {
    const psiMax = 1.75, Rr = len / psiMax;
    const nR = 26, nS = 16, rings = [];
    for (let i = 0; i < nR; i++) {
      const t = i / (nR - 1);
      const psi = psiMax * t;
      const cz = Rr * Math.sin(psi), cy = -Rr * (1 - Math.cos(psi)) + 0.0015;
      const tz = Math.cos(psi), ty = -Math.sin(psi);   // tangent
      const w = base * 0.75 * Math.pow(1 - t, 0.85) + 0.00015, hgt = base * 1.15 * Math.pow(1 - t, 0.8) + 0.0002;
      const ring = [];
      for (let j = 0; j < nS; j++) {
        const a = (j / nS) * Math.PI * 2;
        const lx = -Math.sin(a) * w;
        // keel on top (dorsal ridge), flatter underside (with the groove)
        const ly = Math.cos(a) * hgt * 0.5 * (Math.cos(a) > 0 ? 1 : 0.7);
        // normal direction in the sagittal plane: (0, tz, -ty)
        ring.push(new THREE.Vector3(lx, cy + ly * tz, cz - ly * ty));
      }
      rings.push(ring);
    }
    return loftGeometry(rings, { capStart: true, capEnd: true });
  }

  _trousers(L) {
    const brown = new THREE.Color(COLOR.brown);
    let s = L.s > 0 ? 3 : 5; const R = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
    const rows = 9;
    for (let row = 0; row < rows; row++) {
      const t = row / (rows - 1);
      const y = lerp(0.0, -0.13, t);
      const rad = lerp(0.024, 0.014, t);
      const n = Math.round(lerp(16, 11, t));
      for (let k = 0; k < n; k++) {
        const a = ((k + (row % 2) * 0.5) / n) * Math.PI * 2 + (R() - 0.5) * 0.3;
        const out = new THREE.Vector3(Math.sin(a), 0, -Math.cos(a) * 1.1 + 0.15).normalize();
        const p = new THREE.Vector3(out.x * rad * 0.8, y, out.z * rad * 0.8 + 0.003);
        const Z = new THREE.Vector3(0, -1, 0).addScaledVector(out, 0.28 + 0.1 * t).normalize();
        const Y = out.clone().sub(Z.clone().multiplyScalar(out.dot(Z))).normalize();
        const X = new THREE.Vector3().crossVectors(Y, Z);
        const len = lerp(0.075, 0.095, t) * (0.85 + R() * 0.3), wid = 0.05 * (0.85 + R() * 0.3);
        const m = new THREE.Matrix4().makeBasis(X, Y, Z).setPosition(p).scale(new THREE.Vector3(wid, len, len));
        const c = brown.clone().offsetHSL(0, (R() - 0.5) * 0.08, (R() - 0.5) * 0.04).multiplyScalar(1.12);
        const id = this.sys.add({ type: R() < 0.6 ? 'fluff' : 'contour', variant: Math.floor(R() * 9), color: c, bend: -0.1, camber: 0.08, flutter: 0.03, seed: R(), lift: 0.12, ruffle: 0.7, ao: 0.55 });
        this.rigid.add(id, L.tibia, m);
      }
    }
    // a few short feathers over the top of the tarsus (feathering reaches just below the intertarsal joint)
    for (let k = 0; k < 10; k++) {
      const a = (k / 10) * Math.PI * 2;
      const out = new THREE.Vector3(Math.sin(a), 0, -Math.cos(a)).normalize();
      const p = new THREE.Vector3(out.x * 0.009, -0.004, out.z * 0.01);
      const Z = new THREE.Vector3(0, -1, 0).addScaledVector(out, 0.35).normalize();
      const Y = out.clone().sub(Z.clone().multiplyScalar(out.dot(Z))).normalize();
      const X = new THREE.Vector3().crossVectors(Y, Z);
      const m = new THREE.Matrix4().makeBasis(X, Y, Z).setPosition(p).scale(new THREE.Vector3(0.026, 0.036, 0.036));
      const id = this.sys.add({ type: 'contour', variant: k, color: brown.clone().multiplyScalar(1.1), bend: -0.08, camber: 0.08, flutter: 0.02, seed: R(), lift: 0.08, ruffle: 0.5, ao: 0.6 });
      this.rigid.add(id, L.tarsus, m);
    }
  }

  update() { this.rigid.update(); }
}

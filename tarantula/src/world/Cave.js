import * as THREE from 'three';
import { mergeGeometries, mergeVertices } from 'three/examples/jsm/utils/BufferGeometryUtils.js';
import { createRockMaterial } from './rockMaterial.js';
import { Simplex3, mulberry32 } from '../core/noise.js';

export const SUN_DIR = new THREE.Vector3(-0.22, -0.95, 0.22).normalize(); // light travels down the shaft
export const SHAFT_TOP = new THREE.Vector3(30, 118, -30);

function runWorker(onProgress) {
  return new Promise((resolve, reject) => {
    const w = new Worker(new URL('./caveWorker.js', import.meta.url), { type: 'module' });
    w.onmessage = (e) => {
      if (e.data.type === 'progress') onProgress && onProgress(e.data.p);
      else if (e.data.type === 'done') { resolve(e.data); w.terminate(); }
    };
    w.onerror = (e) => { reject(e); w.terminate(); };
    w.postMessage({ seed: 7, voxel: 1.0 });
  });
}

function rockGeometry(seed, detail = 3) {
  const n = new Simplex3(seed);
  const ico = new THREE.IcosahedronGeometry(1, detail);
  ico.deleteAttribute('uv'); ico.deleteAttribute('normal');
  const g = mergeVertices(ico); // indexed -> smooth normals, no faceting
  const p = g.attributes.position;
  const v = new THREE.Vector3();
  const sx = 0.8 + (seed % 5) * 0.12, sy = 0.45 + (seed % 3) * 0.12;
  for (let i = 0; i < p.count; i++) {
    v.fromBufferAttribute(p, i);
    const d = 1 + 0.28 * n.fbm(v.x * 1.3, v.y * 1.3, v.z * 1.3, 3) + 0.06 * n.noise(v.x * 5, v.y * 5, v.z * 5);
    v.multiplyScalar(d);
    v.x *= sx; v.y *= sy;
    // flatten underside so rocks rest on the floor
    if (v.y < -0.15) v.y = -0.15 + (v.y + 0.15) * 0.3;
    p.setXYZ(i, v.x, v.y, v.z);
  }
  g.computeVertexNormals();
  return g;
}

export class Cave {
  constructor(scene, world) {
    this.scene = scene; this.world = world;
    this.group = new THREE.Group(); this.group.name = 'Cave';
    scene.add(this.group);
  }

  async build(onProgress) {
    const data = await runWorker((p) => onProgress && onProgress(p * 0.8));
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(data.positions, 3));
    g.setAttribute('normal', new THREE.BufferAttribute(data.normals, 3));
    g.setAttribute('aAO', new THREE.BufferAttribute(data.colors, 3));
    g.setIndex(new THREE.BufferAttribute(data.index, 1));
    g.computeBoundingSphere(); g.computeBoundingBox();
    this.material = createRockMaterial();
    const mesh = new THREE.Mesh(g, this.material);
    mesh.castShadow = true; mesh.receiveShadow = true; mesh.name = 'CaveRock';
    this.group.add(mesh);
    this.mesh = mesh;
    onProgress && onProgress(0.85, 'Building collision BVH');
    await new Promise((r) => setTimeout(r, 0));
    this.world.add(mesh);
    onProgress && onProgress(0.92, 'Scattering rocks');
    await new Promise((r) => setTimeout(r, 0));
    this._scatter();
    this._roots();
    onProgress && onProgress(1);
    return this;
  }

  _floorHit(x, z, minNy = 0.7) {
    const h = this.world.raycast(new THREE.Vector3(x, 85, z), new THREE.Vector3(0, -1, 0), 120);
    if (!h || h.normal.y < minNy) return null;
    return h;
  }

  _scatter() {
    const rnd = mulberry32(1234);
    const rockMat = createRockMaterial({ instanced: true });
    // --- larger stones: merged into one collision mesh the spider can climb over
    const stones = [];
    for (let i = 0; i < 70; i++) {
      const x = (rnd() * 2 - 1) * 70, z = (rnd() * 2 - 1) * 80;
      const h = this._floorHit(x, z); if (!h) continue;
      if (Math.hypot(x - 6, z + 2) < 9) continue; // keep spawn clear
      const s = 0.9 + Math.pow(rnd(), 2.2) * 4.2;
      const geo = rockGeometry(i + 11, 3);
      const m = new THREE.Matrix4().compose(
        h.point.clone().addScaledVector(h.normal, s * 0.12),
        new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), h.normal).multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), rnd() * 6.28)),
        new THREE.Vector3(s, s, s));
      geo.applyMatrix4(m);
      stones.push(geo);
    }
    if (stones.length) {
      const merged = mergeGeometries(stones);
      const cnt = merged.attributes.position.count;
      const ao = new Float32Array(cnt * 3);
      for (let i = 0; i < cnt; i++) { ao[i * 3] = 0.8; ao[i * 3 + 1] = 0.55; }
      merged.setAttribute('aAO', new THREE.BufferAttribute(ao, 3));
      const mesh = new THREE.Mesh(merged, this.material);
      mesh.castShadow = true; mesh.receiveShadow = true; mesh.name = 'Stones';
      this.group.add(mesh);
      this.world.add(mesh);
    }
    // --- gravel: tiny instanced pebbles (sub-centimetre, visual only)
    const variants = [0, 1, 2, 3].map((i) => rockGeometry(100 + i, 2));
    const per = 700;
    const dummy = new THREE.Object3D();
    variants.forEach((geo, vi) => {
      const im = new THREE.InstancedMesh(geo, rockMat, per);
      let k = 0;
      for (let i = 0; i < per * 3 && k < per; i++) {
        // clustered distribution
        const cx = (rnd() * 2 - 1) * 72, cz = (rnd() * 2 - 1) * 82;
        const h = this._floorHit(cx, cz, 0.8); if (!h) continue;
        const s = 0.08 + Math.pow(rnd(), 3) * 0.45;
        dummy.position.copy(h.point).addScaledVector(h.normal, s * 0.1);
        dummy.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), h.normal).multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), rnd() * 6.28));
        dummy.scale.setScalar(s);
        dummy.updateMatrix();
        im.setMatrixAt(k++, dummy.matrix);
      }
      im.count = k;
      im.castShadow = true; im.receiveShadow = true;
      this.group.add(im);
    });
  }

  // Thin roots dangling from the sunlit shaft opening
  _roots() {
    const rnd = mulberry32(77);
    const mat = new THREE.MeshStandardMaterial({ color: 0x3a2a1c, roughness: 0.8 });
    this.roots = [];
    for (let i = 0; i < 14; i++) {
      const a = rnd() * Math.PI * 2, r = 8 + rnd() * 3;
      const top = new THREE.Vector3(22 + Math.cos(a) * r, 74, -21 + Math.sin(a) * r);
      const hit = this.world.raycast(top.clone().add(new THREE.Vector3(0, -10, 0)), new THREE.Vector3(Math.cos(a), 0.25, Math.sin(a)).normalize(), 25);
      if (!hit) continue;
      const start = hit.point.clone().addScaledVector(hit.normal, -0.3);
      const len = 6 + rnd() * 16;
      const pts = [];
      for (let k = 0; k <= 8; k++) {
        const t = k / 8;
        pts.push(start.clone().add(new THREE.Vector3(-Math.cos(a) * t * 3 + Math.sin(t * 5 + i) * 0.8, -len * t, -Math.sin(a) * t * 3 + Math.cos(t * 4 + i) * 0.8)));
      }
      const curve = new THREE.CatmullRomCurve3(pts);
      const geo = new THREE.TubeGeometry(curve, 24, 0.12 + rnd() * 0.12, 6, false);
      // taper
      const p = geo.attributes.position; const c = new THREE.Vector3();
      for (let v = 0; v < p.count; v++) {
        const seg = Math.floor(v / 7) / 24;
        const cp = curve.getPointAt(Math.min(seg, 1));
        c.fromBufferAttribute(p, v).sub(cp).multiplyScalar(1 - seg * 0.85).add(cp);
        p.setXYZ(v, c.x, c.y, c.z);
      }
      geo.computeVertexNormals();
      const m = new THREE.Mesh(geo, mat);
      m.castShadow = true; m.receiveShadow = true;
      this.group.add(m);
    }
  }

  findSpawn() {
    const h = this._floorHit(6, -2, 0.6) || this._floorHit(0, 0, 0.3);
    return h;
  }
}

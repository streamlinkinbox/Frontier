import * as THREE from 'three';
import { Sky } from 'three/examples/jsm/objects/Sky.js';

// ---------------------------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------------------------
function hash2(x, y) { let h = (x * 374761393 + y * 668265263) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
function vnoise(x, y) {
  const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi;
  const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  const a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
export function fbm(x, y, oct = 5) { let s = 0, a = 0.5, f = 1; for (let i = 0; i < oct; i++) { s += a * vnoise(x * f, y * f); f *= 2.03; a *= 0.5; } return s; }
const sstep = (a, b, x) => { const t = Math.min(Math.max((x - a) / (b - a), 0), 1); return t * t * (3 - 2 * t); };

// River runs roughly along z at x = riverX(z); gravel bar on its east (+x) bank where the eagle is.
export const riverX = (z) => -14 + 6 * Math.sin(z * 0.012) + 3 * Math.sin(z * 0.031 + 1.3);
export function heightAt(x, z) {
  const d = x - riverX(z);                                   // signed distance across the valley
  // valley: flat gravel bar near the river, rising banks further out, mountains in the distance
  const bar = 0.35 * sstep(-6, 4, d) + 0.02 * d * sstep(0, 30, d);
  const channel = -1.3 * Math.exp(-(d * d) / 40);
  const bank = 6 * sstep(35, 80, Math.abs(d)) + 22 * sstep(80, 260, Math.abs(d));
  const hills = 40 * Math.pow(fbm(x * 0.004 + 3, z * 0.004 - 2, 5), 2) * sstep(60, 250, Math.abs(d));
  const micro = 0.06 * (fbm(x * 0.6, z * 0.6, 3) - 0.5) + 0.25 * (fbm(x * 0.05, z * 0.05, 3) - 0.5) * sstep(8, 30, Math.abs(d));
  return bar + channel + bank + hills + micro;
}

function gravelTexture() {
  const S = 1024, c = document.createElement('canvas'); c.width = c.height = S;
  const g = c.getContext('2d');
  g.fillStyle = '#6f685c'; g.fillRect(0, 0, S, S);
  let s = 3; const R = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
  const h = document.createElement('canvas'); h.width = h.height = S; const hg = h.getContext('2d');
  hg.fillStyle = '#000'; hg.fillRect(0, 0, S, S);
  // sand grain
  for (let i = 0; i < 60000; i++) { const t = 80 + R() * 90; g.fillStyle = `rgba(${t + 10 | 0},${t | 0},${t - 12 | 0},0.35)`; g.fillRect(R() * S, R() * S, 1.5, 1.5); }
  // pebbles, several size classes, wrapped
  const pebble = (x, y, r, col) => {
    for (const ox of [-S, 0, S]) for (const oy of [-S, 0, S]) {
      const px = x + ox, py = y + oy;
      if (px < -r || py < -r || px > S + r || py > S + r) continue;
      const rot = R() * Math.PI, e = 0.6 + R() * 0.4;
      g.save(); g.translate(px, py); g.rotate(rot); g.scale(1, e);
      const gr = g.createRadialGradient(-r * 0.3, -r * 0.3, 0, 0, 0, r);
      gr.addColorStop(0, col[0]); gr.addColorStop(0.8, col[1]); gr.addColorStop(1, 'rgba(30,26,22,0.9)');
      g.fillStyle = gr; g.beginPath(); g.arc(0, 0, r, 0, Math.PI * 2); g.fill(); g.restore();
      hg.save(); hg.translate(px, py); hg.rotate(rot); hg.scale(1, e);
      const hr = hg.createRadialGradient(0, 0, 0, 0, 0, r); hr.addColorStop(0, '#fff'); hr.addColorStop(0.7, '#aaa'); hr.addColorStop(1, '#000');
      hg.fillStyle = hr; hg.beginPath(); hg.arc(0, 0, r, 0, Math.PI * 2); hg.fill(); hg.restore();
    }
  };
  const palette = [['#a59d90', '#7c7468'], ['#8d857a', '#5f584f'], ['#b3a58e', '#857760'], ['#77736d', '#4e4b46'], ['#9b8f7f', '#6b6052'], ['#c2bcb0', '#8f887c']];
  for (let i = 0; i < 2600; i++) { const r = 3 + Math.pow(R(), 3) * 26; pebble(R() * S, R() * S, r, palette[Math.floor(R() * palette.length)]); }
  const hd = hg.getImageData(0, 0, S, S).data;
  const n = document.createElement('canvas'); n.width = n.height = S; const ng = n.getContext('2d'); const out = ng.createImageData(S, S);
  const at = (i, j) => hd[(((j + S) % S) * S + ((i + S) % S)) * 4] / 255;
  for (let j = 0; j < S; j++) for (let i = 0; i < S; i++) {
    const dx = at(i + 1, j) - at(i - 1, j), dy = at(i, j + 1) - at(i, j - 1);
    let x = -dx * 4, y = dy * 4, z = 1; const l = Math.hypot(x, y, z); const o = (j * S + i) * 4;
    out.data[o] = (x / l * 0.5 + 0.5) * 255; out.data[o + 1] = (y / l * 0.5 + 0.5) * 255; out.data[o + 2] = (z / l * 0.5 + 0.5) * 255; out.data[o + 3] = 255;
  }
  ng.putImageData(out, 0, 0);
  const map = new THREE.CanvasTexture(c); map.colorSpace = THREE.SRGBColorSpace; map.wrapS = map.wrapT = THREE.RepeatWrapping; map.anisotropy = 16;
  const nm = new THREE.CanvasTexture(n); nm.wrapS = nm.wrapT = THREE.RepeatWrapping; nm.anisotropy = 16;
  return { map, nm };
}

export class World {
  constructor(renderer, scene) {
    this.scene = scene;
    this.heightAt = heightAt;
    // --- sky ---
    const sky = new Sky(); sky.scale.setScalar(20000);
    const u = sky.material.uniforms;
    u.turbidity.value = 3.2; u.rayleigh.value = 1.3; u.mieCoefficient.value = 0.004; u.mieDirectionalG.value = 0.85;
    this.sunDir = new THREE.Vector3().setFromSphericalCoords(1, THREE.MathUtils.degToRad(90 - 38), THREE.MathUtils.degToRad(215));
    u.sunPosition.value.copy(this.sunDir);
    scene.add(sky);
    this.sky = sky;
    // environment lighting from the sky
    const pm = new THREE.PMREMGenerator(renderer);
    const envScene = new THREE.Scene(); const sky2 = new Sky(); sky2.scale.setScalar(1000);
    Object.assign(sky2.material.uniforms.turbidity, { value: u.turbidity.value });
    sky2.material.uniforms.rayleigh.value = u.rayleigh.value; sky2.material.uniforms.mieCoefficient.value = u.mieCoefficient.value;
    sky2.material.uniforms.mieDirectionalG.value = u.mieDirectionalG.value; sky2.material.uniforms.sunPosition.value.copy(this.sunDir);
    envScene.add(sky2);
    // ground bounce: a large warm-grey disc below
    const gd = new THREE.Mesh(new THREE.CircleGeometry(900, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0x4a4638 }));
    gd.position.y = -50; envScene.add(gd);
    const env = pm.fromScene(envScene, 0.02, 1, 2000, { position: new THREE.Vector3(0, 0, 0) });
    scene.environment = env.texture;
    scene.environmentIntensity = 0.55;
    scene.fog = new THREE.FogExp2(0xa9bfd3, 0.0022);

    // --- sun ---
    const sun = new THREE.DirectionalLight(0xfff1dc, 3.3);
    sun.castShadow = true;
    sun.shadow.mapSize.set(4096, 4096);
    const sc = sun.shadow.camera; sc.left = -3; sc.right = 3; sc.top = 3; sc.bottom = -3; sc.near = 0.5; sc.far = 80;
    sun.shadow.bias = -0.00015; sun.shadow.normalBias = 0.012; sun.shadow.radius = 2;
    scene.add(sun); scene.add(sun.target);
    this.sun = sun;
    const hemi = new THREE.HemisphereLight(0xbfd6ee, 0x5a5242, 0.35); scene.add(hemi);

    this._terrain();
    this._water();
    this._props();
    this._forest();
  }

  // keep the high-resolution shadow frustum around the focus point (the eagle)
  follow(p, radius = 3) {
    const sc = this.sun.shadow.camera;
    if (sc.right !== radius) { sc.left = -radius; sc.right = radius; sc.top = radius; sc.bottom = -radius; sc.updateProjectionMatrix(); }
    // snap to shadow texels to avoid shimmering
    const texel = (2 * radius) / this.sun.shadow.mapSize.x;
    const q = p.clone(); q.x = Math.round(q.x / texel) * texel; q.z = Math.round(q.z / texel) * texel;
    this.sun.target.position.copy(q);
    this.sun.position.copy(q).addScaledVector(this.sunDir, 40);
  }

  _terrain() {
    const { map, nm } = gravelTexture();
    this.gravel = { map, nm };
    const size = 1400, seg = 360;
    const g = new THREE.PlaneGeometry(size, size, seg, seg).rotateX(-Math.PI / 2);
    // concentrate resolution near the origin
    const pos = g.attributes.position;
    const col = new Float32Array(pos.count * 3);
    for (let i = 0; i < pos.count; i++) {
      let x = pos.getX(i), z = pos.getZ(i);
      const r = Math.hypot(x, z), rn = r / (size / 2);
      const k = rn > 0 ? Math.pow(rn, 1.9) / rn : 0; x *= k; z *= k;
      const y = heightAt(x, z);
      pos.setXYZ(i, x, y, z);
      // colour: gravel near the river, grass/moss further, darker forest floor, rock on hills
      const d = Math.abs(x - riverX(z));
      const grass = sstep(18, 34, d + 6 * (fbm(x * 0.05, z * 0.05, 3) - 0.5));
      const rock = sstep(0.4, 0.9, y / 60);
      const c = new THREE.Color(0xffffff).lerp(new THREE.Color(0x6f7a45), grass).lerp(new THREE.Color(0x7a746a), rock);
      col[i * 3] = c.r; col[i * 3 + 1] = c.g; col[i * 3 + 2] = c.b;
    }
    g.setAttribute('color', new THREE.BufferAttribute(col, 3));
    g.computeVertexNormals();
    const mat = new THREE.MeshStandardMaterial({ map, normalMap: nm, normalScale: new THREE.Vector2(1.1, 1.1), vertexColors: true, roughness: 0.95 });
    // two-scale texturing to hide tiling
    mat.onBeforeCompile = (sh) => {
      sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nvarying vec3 vWp;').replace('#include <worldpos_vertex>', '#include <worldpos_vertex>\nvWp = (modelMatrix * vec4(transformed,1.0)).xyz;');
      sh.fragmentShader = sh.fragmentShader.replace('#include <common>', '#include <common>\nvarying vec3 vWp;')
        .replace('#include <map_fragment>', `
          vec4 t1 = texture2D(map, vWp.xz * 0.55);
          vec4 t2 = texture2D(map, vWp.xz * 0.11 + 0.37);
          float mixk = smoothstep(2.0, 14.0, length(vWp.xz - cameraPosition.xz));
          vec4 texelColor = mix(t1, t2, mixk);
          diffuseColor *= texelColor;`)
        .replace('#include <normal_fragment_maps>', `
          vec3 mapN = texture2D(normalMap, vWp.xz * 0.55).xyz * 2.0 - 1.0;
          mapN.xy *= normalScale * (1.0 - 0.7 * smoothstep(2.0, 14.0, length(vWp.xz - cameraPosition.xz)));
          normal = normalize( tbn * mapN );`);
    };
    // use a world-aligned TBN: plane rotated so that uv-derivative TBN works on xz
    const mesh = new THREE.Mesh(g, mat); mesh.receiveShadow = true;
    this.scene.add(mesh);
    this.terrain = mesh;
  }

  _water() {
    const g = new THREE.PlaneGeometry(1600, 1600, 1, 1).rotateX(-Math.PI / 2);
    const m = new THREE.MeshPhysicalMaterial({ color: 0x1e2e30, roughness: 0.06, metalness: 0, transmission: 0, ior: 1.333, specularIntensity: 1, envMapIntensity: 1.2 });
    const w = new THREE.Mesh(g, m); w.position.y = -0.35; w.receiveShadow = true;
    this.scene.add(w);
    this.water = w;
  }

  _props() {
    let s = 17; const R = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
    // scattered 3D pebbles near the eagle (instanced, displaced icosahedra)
    const pg = new THREE.IcosahedronGeometry(1, 2);
    const pp = pg.attributes.position;
    for (let i = 0; i < pp.count; i++) { const v = new THREE.Vector3().fromBufferAttribute(pp, i); v.multiplyScalar(1 + 0.12 * (fbm(v.x * 2 + 5, v.y * 2 + v.z * 3, 2) - 0.5)); pp.setXYZ(i, v.x, v.y, v.z); }
    pg.computeVertexNormals();
    const pm = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.8 });
    const N = 2200;
    const inst = new THREE.InstancedMesh(pg, pm, N);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), e = new THREE.Euler(), c = new THREE.Color();
    const cols = [0x9a9286, 0x7e776c, 0xb0a48e, 0x6c6962, 0x8c8172, 0xbdb6aa];
    for (let i = 0; i < N; i++) {
      const r = 0.3 + Math.pow(R(), 0.6) * 14, a = R() * Math.PI * 2;
      const x = Math.cos(a) * r, z = Math.sin(a) * r;
      const sz = 0.01 + Math.pow(R(), 4) * 0.07;
      const y = heightAt(x, z) + sz * 0.2;
      q.setFromEuler(e.set(R() * 3, R() * 3, R() * 3));
      m.compose(new THREE.Vector3(x, y, z), q, new THREE.Vector3(sz * (1 + R() * 0.4), sz * (0.45 + R() * 0.3), sz * (0.8 + R() * 0.4)));
      inst.setMatrixAt(i, m); inst.setColorAt(i, c.set(cols[Math.floor(R() * cols.length)]).multiplyScalar(0.85 + R() * 0.3));
    }
    inst.castShadow = true; inst.receiveShadow = true;
    this.scene.add(inst);

    // weathered driftwood log
    this.log = this._driftwood(new THREE.Vector3(2.6, 0, -1.6), 0.6, 3.2, 0.16);
    this._driftwood(new THREE.Vector3(-3.5, 0, 2.5), 2.2, 2.1, 0.1);
    // large boulder
    const bg = new THREE.IcosahedronGeometry(1, 5);
    const bp = bg.attributes.position;
    for (let i = 0; i < bp.count; i++) { const v = new THREE.Vector3().fromBufferAttribute(bp, i); const n = fbm(v.x * 1.4 + 9, v.y * 1.4 + v.z * 1.7, 5); v.multiplyScalar(0.8 + 0.45 * n); v.y *= 0.62; bp.setXYZ(i, v.x, v.y, v.z); }
    bg.computeVertexNormals();
    const boulder = new THREE.Mesh(bg, new THREE.MeshStandardMaterial({ color: 0x8a847a, roughness: 0.9, map: this.gravel.map, normalMap: this.gravel.nm }));
    boulder.scale.setScalar(0.9); boulder.position.set(-2.8, heightAt(-2.8, -2.2) + 0.1, -2.2);
    boulder.castShadow = true; boulder.receiveShadow = true;
    this.scene.add(boulder);
    // dead snag (classic eagle perch)
    this._snag(new THREE.Vector3(9, 0, -12));
  }

  _driftwood(p, yaw, len, rad) {
    const pts = []; for (let i = 0; i <= 8; i++) pts.push(new THREE.Vector3((i / 8 - 0.5) * len, Math.sin(i * 0.9) * 0.03, Math.sin(i * 0.6) * 0.08));
    const curve = new THREE.CatmullRomCurve3(pts);
    const g = new THREE.TubeGeometry(curve, 64, rad, 14, false);
    const pos = g.attributes.position;
    for (let i = 0; i < pos.count; i++) { const v = new THREE.Vector3().fromBufferAttribute(pos, i); const n = fbm(v.x * 25, (v.y + v.z) * 6, 3); v.y += (n - 0.5) * 0.02; pos.setXYZ(i, v.x, v.y, v.z); }
    g.computeVertexNormals();
    const m = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ color: 0xa79d8c, roughness: 0.92 }));
    m.rotation.y = yaw; m.position.set(p.x, heightAt(p.x, p.z) + rad * 0.6, p.z);
    m.castShadow = true; m.receiveShadow = true; this.scene.add(m);
    return m;
  }

  _snag(base) {
    const grp = new THREE.Group();
    const mat = new THREE.MeshStandardMaterial({ color: 0x9c948a, roughness: 0.9 });
    const branch = (p0, dir, len, r0, depth) => {
      const pts = []; const d = dir.clone().normalize(); let p = p0.clone();
      for (let i = 0; i <= 6; i++) { pts.push(p.clone()); p.addScaledVector(d, len / 6); d.x += (Math.random() - 0.5) * 0.15; d.z += (Math.random() - 0.5) * 0.15; d.normalize(); }
      const c = new THREE.CatmullRomCurve3(pts);
      const g = new THREE.TubeGeometry(c, 24, r0, 10, false);
      const pos = g.attributes.position; const uv = g.attributes.uv;
      for (let i = 0; i < pos.count; i++) {
        const t = uv.getX(i); const v = new THREE.Vector3().fromBufferAttribute(pos, i);
        const cp = c.getPointAt(t); v.sub(cp).multiplyScalar(1 - 0.85 * t).add(cp); pos.setXYZ(i, v.x, v.y, v.z);
      }
      g.computeVertexNormals();
      const m = new THREE.Mesh(g, mat); m.castShadow = true; grp.add(m);
      if (depth > 0) for (let k = 0; k < 3; k++) {
        const t = 0.35 + k * 0.2; const q = c.getPointAt(t);
        const nd = new THREE.Vector3(Math.random() - 0.5, 0.5 + Math.random() * 0.4, Math.random() - 0.5);
        branch(q, nd, len * 0.35, r0 * 0.35, depth - 1);
      }
    };
    branch(new THREE.Vector3(0, -0.5, 0), new THREE.Vector3(0.05, 1, 0.02), 14, 0.4, 2);
    grp.position.set(base.x, heightAt(base.x, base.z), base.z);
    this.scene.add(grp);
  }

  _forest() {
    // conifers (western red cedar / Douglas fir silhouettes) on the banks
    const trunk = new THREE.CylinderGeometry(0.15, 0.35, 1, 6).translate(0, 0.5, 0);
    const crown = new THREE.ConeGeometry(1, 1, 7, 4).translate(0, 0.5, 0);
    const cp = crown.attributes.position;
    for (let i = 0; i < cp.count; i++) { const v = new THREE.Vector3().fromBufferAttribute(cp, i); const n = fbm(v.x * 4 + 1, v.y * 6 + v.z * 4, 3); const k = 0.8 + 0.5 * n; cp.setXYZ(i, v.x * k, v.y, v.z * k); }
    crown.computeVertexNormals();
    const N = 2600;
    const tr = new THREE.InstancedMesh(trunk, new THREE.MeshStandardMaterial({ color: 0x3b2d22, roughness: 0.95 }), N);
    const cr = new THREE.InstancedMesh(crown, new THREE.MeshStandardMaterial({ color: 0x1f3322, roughness: 0.92 }), N);
    let s = 99; const R = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
    const m = new THREE.Matrix4(), c = new THREE.Color(); let n = 0;
    for (let tries = 0; tries < 30000 && n < N; tries++) {
      const x = (R() - 0.5) * 1100, z = (R() - 0.5) * 1100;
      const d = Math.abs(x - riverX(z));
      if (d < 30 + 20 * fbm(x * 0.02, z * 0.02, 2)) continue;
      if (Math.hypot(x, z) < 25) continue;
      if (fbm(x * 0.01 + 7, z * 0.01, 3) < 0.38) continue;
      const hgt = 18 + R() * 26, y = heightAt(x, z);
      m.compose(new THREE.Vector3(x, y - 0.5, z), new THREE.Quaternion(), new THREE.Vector3(1, hgt * 0.35, 1)); tr.setMatrixAt(n, m);
      m.compose(new THREE.Vector3(x, y + hgt * 0.22, z), new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), R() * 6), new THREE.Vector3(hgt * 0.17, hgt * 0.8, hgt * 0.17)); cr.setMatrixAt(n, m);
      cr.setColorAt(n, c.setHSL(0.36 + R() * 0.05, 0.25 + R() * 0.15, 0.55 + R() * 0.4));
      n++;
    }
    tr.count = cr.count = n;
    tr.castShadow = cr.castShadow = true; cr.receiveShadow = true;
    this.scene.add(tr, cr);
  }
}

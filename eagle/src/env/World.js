import * as THREE from 'three';
import { Sky } from 'three/examples/jsm/objects/Sky.js';

// Lakeshore habitat (bald eagles live near large bodies of open water with tall trees / cliffs).
// Everything is procedural: physically based sky (Preetham), fBm terrain, water, boulder, gravel.

function hash(x, y) { const s = Math.sin(x * 127.1 + y * 311.7) * 43758.5453; return s - Math.floor(s); }
function vnoise(x, y) {
  const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi;
  const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  const a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
export function fbm(x, y, oct = 5) { let s = 0, a = 0.5, f = 1; for (let i = 0; i < oct; i++) { s += a * vnoise(x * f, y * f); f *= 2.03; a *= 0.5; } return s; }
const ss = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };

export const LAKE = { center: new THREE.Vector2(0, -95), radius: 70, level: -1.2 };

// Terrain height (metres). Flat shore near the origin, lake basin to the north, forested hills,
// mountains on the horizon.
export function terrainHeight(x, z) {
  const r = Math.hypot(x, z);
  let h = (fbm(x * 0.004, z * 0.004, 5) - 0.5) * 60 * ss(40, 500, r);
  h += Math.pow(fbm(x * 0.0012 + 7, z * 0.0012 + 3, 4), 2.2) * 420 * ss(350, 1200, r);
  h += (fbm(x * 0.05, z * 0.05, 3) - 0.5) * 1.2 * ss(4, 25, r);
  h += (fbm(x * 0.4, z * 0.4, 2) - 0.5) * 0.08;
  // lake basin
  const dl = Math.hypot(x - LAKE.center.x, (z - LAKE.center.y) * 1.3);
  h = h * ss(LAKE.radius * 0.7, LAKE.radius * 1.9, dl) + (LAKE.level - 4) * (1 - ss(LAKE.radius * 0.7, LAKE.radius * 1.4, dl));
  // gentle shore slope toward the water
  h += -0.6 * ss(60, 5, dl - LAKE.radius) * 0;
  return h;
}

export class World {
  constructor(scene, renderer) {
    this.scene = scene;
    this.renderer = renderer;
    this.sunDir = new THREE.Vector3();
    this.time = 0;
    this.buildSky();
    this.buildTerrain();
    this.buildWater();
    this.buildBoulder();
    this.buildGravel();
    this.buildForest();
    this.buildLights();
  }

  buildSky() {
    const sky = this.sky = new Sky();
    sky.scale.setScalar(20000);
    const u = sky.material.uniforms;
    u.turbidity.value = 3.2; u.rayleigh.value = 1.3; u.mieCoefficient.value = 0.004; u.mieDirectionalG.value = 0.82;
    const elev = 24, azim = 205;
    const phi = THREE.MathUtils.degToRad(90 - elev), theta = THREE.MathUtils.degToRad(azim);
    this.sunDir.setFromSphericalCoords(1, phi, theta);
    u.sunPosition.value.copy(this.sunDir);
    this.scene.add(sky);
  }

  buildTerrain() {
    const size = 5000, seg = 320;
    const g = new THREE.PlaneGeometry(size, size, seg, seg);
    g.rotateX(-Math.PI / 2);
    // concentrate vertices near the centre (radial warp) so the shore is detailed
    const p = g.attributes.position;
    for (let i = 0; i < p.count; i++) {
      let x = p.getX(i), z = p.getZ(i);
      const r = Math.hypot(x, z) / (size / 2);
      const k = Math.pow(r, 2.2) / Math.max(r, 1e-6);
      x *= k; z *= k;
      p.setXYZ(i, x, terrainHeight(x, z), z);
    }
    g.computeVertexNormals();
    const mat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.95, metalness: 0 });
    mat.onBeforeCompile = (sh) => {
      sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nvarying vec3 vW; varying vec3 vNw;')
        .replace('#include <worldpos_vertex>', '#include <worldpos_vertex>\nvW = (modelMatrix * vec4(transformed,1.0)).xyz; vNw = normalize(mat3(modelMatrix) * objectNormal);');
      sh.fragmentShader = sh.fragmentShader.replace('#include <common>', `#include <common>
        varying vec3 vW; varying vec3 vNw;
        float h2(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }
        float n2(vec2 p){ vec2 i=floor(p), f=fract(p); vec2 u=f*f*(3.0-2.0*f);
          return mix(mix(h2(i),h2(i+vec2(1,0)),u.x), mix(h2(i+vec2(0,1)),h2(i+vec2(1,1)),u.x), u.y); }
        float fb(vec2 p){ float s=0.0,a=0.5; for(int i=0;i<5;i++){ s+=a*n2(p); p*=2.03; a*=0.5;} return s; }`)
        .replace('#include <color_fragment>', `#include <color_fragment>
        {
          float slope = 1.0 - clamp(vNw.y, 0.0, 1.0);
          float d = length(vW.xz);
          float n = fb(vW.xz * 0.35), n2v = fb(vW.xz * 3.1), nb = fb(vW.xz * 0.02);
          vec3 gravel = mix(vec3(0.13,0.12,0.10), vec3(0.22,0.20,0.17), n2v) * (0.85 + 0.3*n);
          vec3 sand   = vec3(0.30,0.26,0.19) * (0.9 + 0.2*n2v);
          vec3 grass  = mix(vec3(0.07,0.10,0.03), vec3(0.14,0.13,0.05), n) * (0.8 + 0.3*n2v);
          vec3 forest = mix(vec3(0.025,0.04,0.022), vec3(0.05,0.06,0.03), nb);
          vec3 rock   = mix(vec3(0.16,0.155,0.15), vec3(0.26,0.25,0.23), n2v);
          vec3 snow   = vec3(0.9,0.92,0.95);
          vec2 lc = vec2(${LAKE.center.x.toFixed(1)}, ${LAKE.center.y.toFixed(1)});
          float dl = length(vec2(vW.x - lc.x, (vW.z - lc.y) * 1.3));
          vec3 col = mix(gravel, grass, smoothstep(8.0, 22.0, d + n*10.0));
          col = mix(col, sand, (1.0 - smoothstep(${(LAKE.radius * 1.02).toFixed(1)}, ${(LAKE.radius * 1.25).toFixed(1)}, dl + n*6.0)) * step(${LAKE.radius.toFixed(1)} * 0.6, dl));
          col = mix(col, forest, smoothstep(90.0, 220.0, d + nb*80.0));
          col = mix(col, rock, smoothstep(0.35, 0.6, slope + n*0.1));
          col = mix(col, snow, smoothstep(230.0, 300.0, vW.y + n*40.0) * (1.0 - smoothstep(0.55,0.8,slope)));
          diffuseColor.rgb = col;
        }`);
    };
    const mesh = this.terrain = new THREE.Mesh(g, mat);
    mesh.receiveShadow = true;
    this.scene.add(mesh);
  }

  buildWater() {
    // animated normal map
    const S = 256, c = document.createElement('canvas'); c.width = c.height = S;
    const gx = c.getContext('2d'), img = gx.createImageData(S, S);
    const h = new Float32Array(S * S);
    for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) {
      let v = 0, a = 1, f = 1 / 32;
      for (let o = 0; o < 5; o++) {
        const px = x * f, py = y * f, per = S * f;
        const xi = Math.floor(px), yi = Math.floor(py), xf = px - xi, yf = py - yi;
        const hh = (i, j) => hash(((i % per) + per) % per, ((j % per) + per) % per);
        const u = xf * xf * (3 - 2 * xf), w = yf * yf * (3 - 2 * yf);
        v += a * (hh(xi, yi) * (1 - u) * (1 - w) + hh(xi + 1, yi) * u * (1 - w) + hh(xi, yi + 1) * (1 - u) * w + hh(xi + 1, yi + 1) * u * w);
        a *= 0.5; f *= 2;
      }
      h[y * S + x] = v;
    }
    for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) {
      const dx = h[y * S + (x + 1) % S] - h[y * S + (x - 1 + S) % S], dy = h[((y + 1) % S) * S + x] - h[((y - 1 + S) % S) * S + x];
      const n = new THREE.Vector3(-dx * 3, -dy * 3, 1).normalize(), o = (y * S + x) * 4;
      img.data[o] = (n.x * 0.5 + 0.5) * 255; img.data[o + 1] = (n.y * 0.5 + 0.5) * 255; img.data[o + 2] = (n.z * 0.5 + 0.5) * 255; img.data[o + 3] = 255;
    }
    gx.putImageData(img, 0, 0);
    const nt = new THREE.CanvasTexture(c); nt.wrapS = nt.wrapT = THREE.RepeatWrapping; nt.repeat.set(40, 40);
    this.waterNormal = nt;
    const mat = new THREE.MeshPhysicalMaterial({ color: 0x0c1c22, roughness: 0.06, metalness: 0, normalMap: nt, normalScale: new THREE.Vector2(0.35, 0.35), ior: 1.333, specularIntensity: 1 });
    const g = new THREE.CircleGeometry(LAKE.radius * 2.2, 96); g.rotateX(-Math.PI / 2);
    g.scale(1, 1, 1 / 1.3);
    const water = this.water = new THREE.Mesh(g, mat);
    water.position.set(LAKE.center.x, LAKE.level, LAKE.center.y);
    water.receiveShadow = true;
    this.scene.add(water);
  }

  buildBoulder() {
    // granite boulder next to the eagle (perch / backdrop)
    const g = new THREE.IcosahedronGeometry(1, 6);
    const p = g.attributes.position;
    for (let i = 0; i < p.count; i++) {
      const v = new THREE.Vector3().fromBufferAttribute(p, i);
      const n = fbm(v.x * 1.6 + 3, v.y * 1.6 + v.z * 1.3, 5);
      const n2 = fbm(v.z * 5 + 1, v.x * 5 + v.y * 4, 3);
      v.multiplyScalar(1 + (n - 0.5) * 0.55 + (n2 - 0.5) * 0.08);
      v.y *= 0.62;
      p.setXYZ(i, v.x * 1.35, v.y * 1.1, v.z * 1.0);
    }
    g.computeVertexNormals();
    const mat = new THREE.MeshStandardMaterial({ color: 0x77726a, roughness: 0.88 });
    mat.onBeforeCompile = (sh) => {
      sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nvarying vec3 vP; varying vec3 vN2;')
        .replace('#include <begin_vertex>', '#include <begin_vertex>\nvP = position; vN2 = normal;');
      sh.fragmentShader = sh.fragmentShader.replace('#include <common>', `#include <common>
        varying vec3 vP; varying vec3 vN2;
        float h3(vec3 p){ return fract(sin(dot(p, vec3(127.1,311.7,74.7))) * 43758.5453); }
        float n3(vec3 p){ vec3 i=floor(p), f=fract(p); vec3 u=f*f*(3.0-2.0*f);
          return mix(mix(mix(h3(i),h3(i+vec3(1,0,0)),u.x), mix(h3(i+vec3(0,1,0)),h3(i+vec3(1,1,0)),u.x),u.y),
                     mix(mix(h3(i+vec3(0,0,1)),h3(i+vec3(1,0,1)),u.x), mix(h3(i+vec3(0,1,1)),h3(i+vec3(1,1,1)),u.x),u.y), u.z); }`)
        .replace('#include <color_fragment>', `#include <color_fragment>
        {
          float g1 = n3(vP * 38.0), g2 = n3(vP * 9.0), g3 = n3(vP * 2.5);
          vec3 base = mix(vec3(0.17,0.16,0.15), vec3(0.30,0.29,0.27), g2);
          base = mix(base, vec3(0.05,0.05,0.045), step(0.78, g1) * 0.7);           // dark mica/hornblende grains
          base = mix(base, vec3(0.45,0.43,0.40), step(0.86, n3(vP*55.0+3.0))*0.6); // feldspar
          base = mix(base, vec3(0.12,0.14,0.07), smoothstep(0.55, 0.8, vN2.y + g3*0.3) * 0.55); // lichen/moss on top
          diffuseColor.rgb = base;
        }`)
        .replace('#include <roughnessmap_fragment>', '#include <roughnessmap_fragment>\nroughnessFactor = 0.8 + 0.15 * n3(vP*20.0);');
    };
    const m = this.boulder = new THREE.Mesh(g, mat);
    m.position.set(-1.6, -0.25, -1.4);
    m.rotation.y = 0.6;
    m.castShadow = true; m.receiveShadow = true;
    this.scene.add(m);
  }

  buildGravel() {
    const base = new THREE.IcosahedronGeometry(1, 1);
    const p = base.attributes.position;
    for (let i = 0; i < p.count; i++) { const v = new THREE.Vector3().fromBufferAttribute(p, i); v.multiplyScalar(0.8 + 0.4 * hash(v.x * 9, v.y * 7 + v.z * 3)); p.setXYZ(i, v.x, v.y * 0.55, v.z); }
    base.computeVertexNormals();
    const mat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.85 });
    const N = 2600;
    const inst = new THREE.InstancedMesh(base, mat, N);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), pos = new THREE.Vector3(), c = new THREE.Color();
    let seed = 17; const R = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
    for (let i = 0; i < N; i++) {
      const r = Math.sqrt(R()) * 14, a = R() * Math.PI * 2;
      const x = Math.cos(a) * r, z = Math.sin(a) * r;
      const sc = 0.012 + Math.pow(R(), 3) * 0.07;
      pos.set(x, terrainHeight(x, z) - sc * 0.2, z);
      q.setFromEuler(new THREE.Euler(R() * 0.4, R() * 6.28, R() * 0.4));
      s.set(sc * (0.8 + 0.5 * R()), sc, sc * (0.8 + 0.5 * R()));
      m.compose(pos, q, s);
      inst.setMatrixAt(i, m);
      const g = 0.08 + R() * 0.16;
      c.setRGB(g * (1 + (R() - 0.5) * 0.15), g * 0.96, g * 0.9);
      inst.setColorAt(i, c);
    }
    inst.castShadow = true; inst.receiveShadow = true;
    this.scene.add(inst);
  }

  buildForest() {
    // conifers (spruce/fir) around the lake — layered skirts of drooping branches
    const tree = new THREE.BufferGeometry();
    const parts = [];
    const tiers = 9;
    for (let t = 0; t < tiers; t++) {
      const f = t / tiers;
      const cone = new THREE.ConeGeometry(0.42 * (1 - f) + 0.06, 0.28, 9, 1, true);
      cone.translate(0, 0.12 + f * 0.86, 0);
      const cp = cone.attributes.position;
      for (let i = 0; i < cp.count; i++) { const y = cp.getY(i); const r = Math.hypot(cp.getX(i), cp.getZ(i)); cp.setY(i, y - r * 0.25 + (hash(i, t) - 0.5) * 0.04); }
      parts.push(cone);
    }
    const trunk = new THREE.CylinderGeometry(0.025, 0.04, 0.3, 6); trunk.translate(0, 0.1, 0); parts.push(trunk);
    let total = 0; for (const g of parts) total += g.attributes.position.count;
    const pos = new Float32Array(total * 3), col = new Float32Array(total * 3); const idx = [];
    let o = 0;
    parts.forEach((g, gi) => {
      const pa = g.attributes.position; const ni = g.index.array;
      for (let i = 0; i < ni.length; i++) idx.push(ni[i] + o);
      for (let i = 0; i < pa.count; i++) {
        pos.set([pa.getX(i), pa.getY(i), pa.getZ(i)], (o + i) * 3);
        const trunkPart = gi === parts.length - 1;
        const k = trunkPart ? [0.08, 0.05, 0.03] : [0.015 + 0.012 * hash(i, gi), 0.03 + 0.018 * hash(gi, i), 0.018];
        col.set(k, (o + i) * 3);
      }
      o += pa.count;
    });
    tree.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    tree.setAttribute('color', new THREE.BufferAttribute(col, 3));
    tree.setIndex(idx); tree.computeVertexNormals();
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.95, side: THREE.DoubleSide });
    const N = 5000;
    const inst = new THREE.InstancedMesh(tree, mat, N);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3();
    let seed = 9; const R = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
    let n = 0;
    for (let tries = 0; tries < 40000 && n < N; tries++) {
      const r = 70 + Math.pow(R(), 0.8) * 900, a = R() * Math.PI * 2;
      const x = Math.cos(a) * r, z = Math.sin(a) * r;
      const dl = Math.hypot(x - LAKE.center.x, (z - LAKE.center.y) * 1.3);
      if (dl < LAKE.radius * 1.3) continue;
      const h = terrainHeight(x, z);
      if (h > 230) continue;
      if (fbm(x * 0.01, z * 0.01, 3) < 0.42 && r > 80) continue; // clearings
      const height = 14 + R() * 20;
      p.set(x, h - 0.5, z);
      q.setFromEuler(new THREE.Euler((R() - 0.5) * 0.06, R() * 6.28, (R() - 0.5) * 0.06));
      s.set(height * (0.75 + 0.3 * R()), height, height * (0.75 + 0.3 * R()));
      m.compose(p, q, s);
      inst.setMatrixAt(n++, m);
    }
    inst.count = n;
    inst.castShadow = false; inst.receiveShadow = true;
    this.scene.add(inst);
  }

  buildLights() {
    const sun = this.sun = new THREE.DirectionalLight(0xfff1dc, 3.4);
    sun.castShadow = true;
    sun.shadow.mapSize.set(4096, 4096);
    const S = 4;
    Object.assign(sun.shadow.camera, { left: -S, right: S, top: S, bottom: -S, near: 1, far: 80 });
    sun.shadow.bias = -0.00015; sun.shadow.normalBias = 0.012; sun.shadow.radius = 1.5;
    this.scene.add(sun, sun.target);
    const hemi = this.hemi = new THREE.HemisphereLight(0xbfd4ff, 0x4a4032, 0.25);
    this.scene.add(hemi);
  }

  // keep the shadow frustum centred on the subject (tight → crisp feather shadows)
  follow(p) {
    this.sun.target.position.copy(p);
    this.sun.position.copy(p).addScaledVector(this.sunDir, 40);
    this.sun.target.updateMatrixWorld();
  }

  update(dt) {
    this.time += dt;
    this.waterNormal.offset.set(this.time * 0.004, this.time * 0.0025);
  }

  async buildEnvironment(renderer) {
    // IBL from the sky only (fast, stable)
    const pmrem = new THREE.PMREMGenerator(renderer);
    const skyScene = new THREE.Scene();
    const sky2 = new Sky(); sky2.scale.setScalar(10000);
    for (const k of Object.keys(this.sky.material.uniforms)) sky2.material.uniforms[k].value = this.sky.material.uniforms[k].value;
    skyScene.add(sky2);
    // ground bounce: a large dark-olive disc below the horizon
    const gd = new THREE.Mesh(new THREE.CircleGeometry(5000, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0x2c2a20 }));
    gd.position.y = -10; skyScene.add(gd);
    const rt = pmrem.fromScene(skyScene, 0, 0.1, 20000, { size: 256, position: new THREE.Vector3(0, 0, 0) });
    this.scene.environment = rt.texture;
    this.scene.environmentIntensity = 0.55;
    pmrem.dispose();
  }
}

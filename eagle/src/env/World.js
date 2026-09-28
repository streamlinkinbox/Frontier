import * as THREE from 'three';
import { Sky } from 'three/examples/jsm/objects/Sky.js';
import { mergeVertices } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

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
        float fb(vec2 p){ float s=0.0,a=0.5; for(int i=0;i<5;i++){ s+=a*n2(p); p*=2.03; a*=0.5;} return s; }
        vec2 h22(vec2 p){ p = vec2(dot(p,vec2(127.1,311.7)), dot(p,vec2(269.5,183.3))); return fract(sin(p)*43758.5453); }
        // Voronoi pebbles: returns (F1, F2-F1, cell id) and the offset from the pebble centre
        vec3 peb(vec2 x, out vec2 off){ vec2 n=floor(x), f=fract(x); float d1=8.0, d2=8.0; vec2 id=vec2(0); off=vec2(0);
          for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){ vec2 g=vec2(i,j); vec2 o=h22(n+g); o = 0.5+0.42*sin(6.2831*o);
            vec2 r=g+o-f; float d=dot(r,r); if(d<d1){ d2=d1; d1=d; id=n+g; off=-r; } else if(d<d2){ d2=d; } }
          return vec3(sqrt(d1), sqrt(d2)-sqrt(d1), h22(id).x); }
        vec3 vPebN = vec3(0.0);`)
        .replace('#include <normal_fragment_maps>', `#include <normal_fragment_maps>
        normal = normalize(normal + (viewMatrix * vec4(vPebN, 0.0)).xyz);`)
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
          // close range shore: individual rounded pebbles / cobbles with dark crevices and grit between them
          vPebN = vec3(0.0);
          float near = (1.0 - smoothstep(10.0, 26.0, d + n*6.0)) * (1.0 - smoothstep(0.3, 0.5, slope));
          if (near > 0.0) {
            vec2 o1, o2;
            vec3 a = peb(vW.xz * 26.0, o1);          // ~4 cm gravel
            vec3 b = peb(vW.xz * 8.0 + 17.0, o2);    // ~12 cm cobbles (sparse)
            // only some cells hold a stone; the rest is sand/grit between them (a real shingle beach)
            float hasA = step(0.45, fract(a.z * 7.31));
            float cob = step(0.85, b.z) * smoothstep(0.04, 0.2, b.y);
            float pa = smoothstep(0.03, 0.22, a.y) * hasA * (1.0 - smoothstep(0.30, 0.46, a.x));
            vec3 tintA = mix(vec3(0.12,0.115,0.105), vec3(0.24,0.225,0.20), fract(a.z * 91.7));
            tintA *= mix(vec3(1.0), vec3(1.1,0.97,0.84), step(0.82, fract(a.z * 13.0)));
            vec3 tintB = mix(vec3(0.15,0.145,0.135), vec3(0.27,0.26,0.24), fract(b.z * 37.0)) * (0.92 + 0.16*n2v);
            float gn = n2(vW.xz * 140.0) * 0.6 + n2(vW.xz * 45.0) * 0.4;
            vec3 grit = mix(vec3(0.105,0.095,0.078), vec3(0.19,0.17,0.135), gn) * (0.85 + 0.3 * n);
            vec3 stones = mix(grit, tintA * (0.92 + 0.16 * n2(vW.xz * 70.0)), pa);
            stones = mix(stones, tintB, cob);
            float k = near * (1.0 - smoothstep(8.0, 22.0, d + n*10.0));
            col = mix(col, stones, k);
            // gently domed stones (world-space normal perturbation), subtle so they do not read as tiles
            vec2 gA = o1 * 0.55 * pa, gB = o2 * 0.45 * cob;
            vPebN = vec3(gA.x + gB.x, 0.0, gA.y + gB.y) * k;
          }
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
    // indexed, finely tessellated sphere (shared vertices → smooth normals), then weathered fBm displacement
    let g = new THREE.IcosahedronGeometry(1, 56);
    g.deleteAttribute('normal'); g.deleteAttribute('uv');
    g = mergeVertices(g, 1e-5);
    const p = g.attributes.position;
    for (let i = 0; i < p.count; i++) {
      const v = new THREE.Vector3().fromBufferAttribute(p, i);
      const n = fbm(v.x * 1.6 + 3, v.y * 1.6 + v.z * 1.3, 5);
      const n2 = fbm(v.z * 5 + 1, v.x * 5 + v.y * 4, 4);
      const n3 = fbm(v.x * 14 + v.z * 3 + 5, v.y * 14 - v.z * 6, 3);
      // exfoliation: gently flattened facets + fine grain relief
      const ridge = 1 - Math.abs(fbm(v.x * 3.1 + 11, v.z * 3.1 + v.y * 2.2, 3) * 2 - 1);
      v.multiplyScalar(1 + (n - 0.5) * 0.55 + (n2 - 0.5) * 0.10 + (n3 - 0.5) * 0.018 - ridge * ridge * 0.035);
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

  // Spruce branch texture: a twig with side twiglets densely clothed in short needles (alpha-cut).
  needleTexture() {
    const W = 256, H = 128, c = document.createElement('canvas'); c.width = W; c.height = H;
    const g = c.getContext('2d');
    let seed = 5; const R = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
    g.clearRect(0, 0, W, H);
    g.lineCap = 'round';
    const twig = (x0, y0, ang, len, w, depth) => {
      const x1 = x0 + Math.cos(ang) * len, y1 = y0 + Math.sin(ang) * len;
      // needles along the twig, pointing forward and outward on both sides
      const n = Math.floor(len / 1.1);
      for (let i = 0; i < n; i++) {
        const t = i / n, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
        for (const sd of [-1, 1]) {
          const a = ang + sd * (0.7 + R() * 0.5), l = (5 + R() * 4) * (1 - 0.35 * t) * (depth ? 0.85 : 1);
          const k = 40 + R() * 45;
          g.strokeStyle = `rgb(${k * 0.55 | 0},${k | 0},${k * 0.5 | 0})`; g.lineWidth = 2.1;
          g.beginPath(); g.moveTo(x, y); g.lineTo(x + Math.cos(a) * l, y + Math.sin(a) * l); g.stroke();
        }
      }
      g.strokeStyle = 'rgb(60,42,28)'; g.lineWidth = w; g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke();
      if (depth > 0) {
        const m = 13;
        for (let i = 1; i < m; i++) {
          const t = i / m, sd = i % 2 ? 1 : -1;
          const env = Math.sin(Math.PI * Math.min(1, t * 1.15)) ;
          twig(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, ang + sd * (0.75 + R() * 0.25), len * 0.5 * env + 8, w * 0.55, depth - 1);
        }
      }
    };
    twig(2, H / 2, 0, W - 10, 3, 1);
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8;
    return t;
  }

  // Unit-height spruce: tapering trunk + whorls of drooping branch cards; canopy normals (radial + up)
  // give the soft, volumetric light response of a real conifer crown.
  spruceGeometry({ whorls, perWhorl, segs, cards, seed }) {
    const pos = [], nrm = [], uv = [], col = [], idx = [];
    let s0 = seed; const R = () => ((s0 = (s0 * 1664525 + 1013904223) >>> 0) / 4294967296);
    const vtx = (x, y, z, nx, ny, nz, u, v, c) => { pos.push(x, y, z); nrm.push(nx, ny, nz); uv.push(u, v); col.push(c[0], c[1], c[2]); return pos.length / 3 - 1; };
    // trunk
    const TS = 7;
    for (let j = 0; j <= 1; j++) for (let i = 0; i <= TS; i++) {
      const a = i / TS * Math.PI * 2, y = j ? 0.9 : -0.02, r = j ? 0.002 : 0.016;
      vtx(Math.cos(a) * r, y, Math.sin(a) * r, Math.cos(a), 0, Math.sin(a), 0.99, 0.5, [0.09, 0.06, 0.04]);
    }
    for (let i = 0; i < TS; i++) idx.push(i, i + TS + 1, i + 1, i + 1, i + TS + 1, i + TS + 2);
    for (let w = 0; w < whorls; w++) {
      const f = (w + R() * 0.6) / whorls;
      const y0 = 0.1 + f * 0.86;
      const L = (0.34 * Math.pow(1 - f, 0.9) + 0.035) * (0.85 + 0.3 * R());
      const n = Math.max(3, Math.round(perWhorl * (1 - 0.4 * f)));
      const az0 = R() * 6.28;
      for (let b = 0; b < n; b++) {
        const az = az0 + b / n * Math.PI * 2 + (R() - 0.5) * 0.5;
        const dir = [Math.cos(az), 0, Math.sin(az)], side = [-Math.sin(az), 0, Math.cos(az)];
        const droop = 0.25 + 0.35 * (1 - f) + R() * 0.15; // lower branches droop more
        for (let cI = 0; cI < cards; cI++) {
          const roll = cards > 1 ? (cI ? 0.5 : -0.5) : 0;
          const width = L * (0.5 + 0.12 * R());
          const base = idx.length;
          const first = pos.length / 3;
          for (let k = 0; k <= segs; k++) {
            const t = k / segs;
            // branch sags then turns up slightly at the tip (spruce)
            const drop = L * (droop * t * t * 0.9 - 0.12 * t * t * t * t) + L * 0.12 * t;
            const cx = dir[0] * L * t, cz = dir[2] * L * t, cy = y0 - drop;
            const hw = width * 0.5 * (0.45 + 0.55 * Math.sin(Math.PI * Math.min(1, 0.15 + t * 0.9)));
            const up = Math.sin(roll) * hw, lat = Math.cos(roll) * hw;
            const ao = 0.35 + 0.65 * Math.min(1, t * 1.3 + 0.1); // dark inner crown
            const c = [0.36 * ao, 0.46 * ao, 0.40 * ao];
            // canopy normal: outward from the trunk axis and up
            const rx = dir[0] * (0.25 + t) , rz = dir[2] * (0.25 + t), ry = 0.55;
            const l = Math.hypot(rx, ry, rz);
            for (const sd of [-1, 1]) vtx(cx + side[0] * lat * sd, cy + up * sd, cz + side[2] * lat * sd, rx / l, ry / l, rz / l, t * 0.97, 0.5 + 0.5 * sd, c);
          }
          for (let k = 0; k < segs; k++) { const i0 = first + k * 2; idx.push(i0, i0 + 2, i0 + 1, i0 + 1, i0 + 2, i0 + 3); }
          void base;
        }
      }
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(nrm, 3));
    g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
    g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    g.setIndex(idx);
    return g;
  }

  buildForest() {
    // spruce / fir around the lake (bald eagles nest and perch in tall conifers near water)
    const map = this.needleTexture();
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, map, roughness: 0.9, side: THREE.DoubleSide, alphaTest: 0.3, alphaToCoverage: true });
    // keep foliage coverage at distance: sharpen the mip-averaged alpha before the alpha test
    mat.onBeforeCompile = (sh) => { sh.fragmentShader = sh.fragmentShader.replace('#include <alphatest_fragment>', 'diffuseColor.a = clamp((diffuseColor.a - 0.3) / max(fwidth(diffuseColor.a), 1e-4) + 0.5, 0.0, 1.0);\n#include <alphatest_fragment>'); };
    const near = this.spruceGeometry({ whorls: 26, perWhorl: 7, segs: 3, cards: 2, seed: 11 });
    const far = this.spruceGeometry({ whorls: 13, perWhorl: 5, segs: 1, cards: 1, seed: 12 });
    const NN = 1800, NF = 4200;
    const instN = new THREE.InstancedMesh(near, mat, NN), instF = new THREE.InstancedMesh(far, mat, NF);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color();
    let seed = 9; const R = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
    let nn = 0, nf = 0;
    for (let tries = 0; tries < 60000 && (nn < NN || nf < NF); tries++) {
      const r = 60 + Math.pow(R(), 0.8) * 900, a = R() * Math.PI * 2;
      const x = Math.cos(a) * r, z = Math.sin(a) * r;
      const dl = Math.hypot(x - LAKE.center.x, (z - LAKE.center.y) * 1.3);
      if (dl < LAKE.radius * 1.3) continue;
      const h = terrainHeight(x, z);
      if (h > 230) continue;
      if (fbm(x * 0.01, z * 0.01, 3) < 0.42 && r > 80) continue; // clearings
      const isNear = r < 320;
      if (isNear ? nn >= NN : nf >= NF) continue;
      const height = 14 + R() * 22;
      p.set(x, h - 0.4, z);
      q.setFromEuler(new THREE.Euler((R() - 0.5) * 0.05, R() * 6.28, (R() - 0.5) * 0.05));
      s.set(height * (0.8 + 0.3 * R()), height, height * (0.8 + 0.3 * R()));
      m.compose(p, q, s);
      c.setRGB(0.8 + 0.3 * R(), 0.85 + 0.25 * R(), 0.75 + 0.3 * R());
      if (isNear) { instN.setMatrixAt(nn, m); instN.setColorAt(nn++, c); } else { instF.setMatrixAt(nf, m); instF.setColorAt(nf++, c); }
    }
    instN.count = nn; instF.count = nf;
    for (const inst of [instN, instF]) { inst.castShadow = false; inst.receiveShadow = true; this.scene.add(inst); }
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
    // ground bounce: sunlit shore/forest (albedo ≈ 0.15–0.2 under a 3.4 sun) seen below the horizon —
    // this is what lights the underside of the wings and body in flight
    const gd = new THREE.Mesh(new THREE.CircleGeometry(5000, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: new THREE.Color().setRGB(0.13, 0.12, 0.09) }));
    gd.position.y = -10; skyScene.add(gd);
    const rt = pmrem.fromScene(skyScene, 0, 0.1, 20000, { size: 256, position: new THREE.Vector3(0, 0, 0) });
    this.scene.environment = rt.texture;
    this.scene.environmentIntensity = 0.55;
    pmrem.dispose();
  }
}

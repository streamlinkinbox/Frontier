import * as THREE from 'three';

// ------------------------------------------------------------------------------------------------
// Procedural feather atlas.
// Every feather is drawn barb by barb: rachis (shaft), vanes made of individual barbs meeting the shaft at
// an acute angle, random vane splits (where barbules unzip), a downy afterfeather at the base and a
// slightly frayed margin. A height field of the same strokes is converted into a tangent-space normal map
// so that barbs catch light.
// Layout convention in the atlas: base of the feather at the bottom of its cell (v = 0), tip at the top.
// ------------------------------------------------------------------------------------------------

function rng(seed) {
  let s = seed >>> 0;
  return () => { s = (s + 0x6d2b79f5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
const sstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };

// Vane profiles: returns [outerHalfWidth, innerHalfWidth] as a fraction of the cell width at v (0 base..1 tip).
// "outer" is drawn on the left of the cell (towards the leading edge for wing feathers).
const TYPES = {
  // body contour feather: rounded, broad, with a large fluffy base (hidden under the overlap)
  contour: {
    w: 192, h: 320, rachis: 0.5, barb: 0.9, down: 0.34, splits: 5, fray: 0.5,
    prof: (v) => { const r = v < 0.5 ? 0.3 + 0.16 * sstep(0, 0.5, v) : 0.46 * Math.sqrt(Math.max(0, 1 - ((v - 0.5) / 0.5) ** 2)); return [r, r]; },
  },
  // head / neck feathers of the bald eagle: narrow, lanceolate, pointed ("hackles")
  lance: {
    w: 128, h: 288, rachis: 0.5, barb: 0.8, down: 0.22, splits: 3, fray: 0.7,
    prof: (v) => { const r = 0.44 * Math.pow(Math.sin(Math.PI * clamp(v * 0.95 + 0.05, 0, 1)), 0.85) * (1 - 0.45 * v); return [r, r]; },
  },
  // wing coverts: rounded, solid, little down
  covert: {
    w: 192, h: 448, rachis: 0.46, barb: 0.7, down: 0.12, splits: 3, fray: 0.25,
    prof: (v) => { const t = v < 0.6 ? 1 : Math.sqrt(Math.max(0, 1 - ((v - 0.6) / 0.4) ** 2)); return [0.36 * (0.75 + 0.25 * sstep(0, 0.3, v)) * t, 0.5 * (0.75 + 0.25 * sstep(0, 0.3, v)) * t]; },
  },
  // secondaries & tertials: broad, blunt tip, narrower outer vane
  secondary: {
    w: 224, h: 768, rachis: 0.36, barb: 0.55, down: 0.05, splits: 4, fray: 0.2,
    prof: (v) => { const t = v < 0.88 ? 1 : Math.sqrt(Math.max(0, 1 - ((v - 0.88) / 0.12) ** 2)); const b = 0.8 + 0.2 * sstep(0, 0.15, v); return [0.3 * b * t, 0.6 * b * (v < 0.9 ? 1 : Math.sqrt(Math.max(0, 1 - ((v - 0.9) / 0.1) ** 2)))]; },
  },
  // inner primaries: long, asymmetric, no emargination
  primaryInner: {
    w: 192, h: 1024, rachis: 0.3, barb: 0.45, down: 0.04, splits: 4, fray: 0.2,
    prof: (v) => { const t = v < 0.84 ? 1 : Math.sqrt(Math.max(0, 1 - ((v - 0.84) / 0.16) ** 2)); return [0.2 * t * (0.8 + 0.2 * sstep(0, 0.2, v)), 0.66 * t * (0.75 + 0.25 * sstep(0, 0.2, v))]; },
  },
  // outer primaries (p10-p6): emarginated outer vane and notched inner vane -> narrow "finger"
  primaryOuter: {
    w: 192, h: 1024, rachis: 0.36, barb: 0.4, down: 0.04, splits: 3, fray: 0.2,
    prof: (v) => {
      const tip = v < 0.9 ? 1 : Math.sqrt(Math.max(0, 1 - ((v - 0.9) / 0.1) ** 2));
      const outer = (0.2 - 0.12 * sstep(0.5, 0.6, v)) * tip * (0.8 + 0.2 * sstep(0, 0.2, v));
      const inner = (0.6 - 0.36 * sstep(0.56, 0.68, v)) * tip * (0.75 + 0.25 * sstep(0, 0.2, v));
      return [outer, inner];
    },
  },
  // tail feathers (rectrices): broad, symmetric, rounded tip
  rectrix: {
    w: 224, h: 896, rachis: 0.48, barb: 0.55, down: 0.04, splits: 4, fray: 0.25,
    prof: (v) => { const t = v < 0.84 ? 1 : Math.sqrt(Math.max(0, 1 - ((v - 0.84) / 0.16) ** 2)); const b = 0.82 + 0.18 * sstep(0, 0.2, v); return [0.45 * b * t, 0.47 * b * t]; },
  },
  // loose "trouser" feathers on the tibiotarsus and belly
  fluff: {
    w: 192, h: 320, rachis: 0.5, barb: 1.0, down: 0.55, splits: 9, fray: 1.0,
    prof: (v) => { const r = 0.44 * Math.pow(Math.sin(Math.PI * clamp(v, 0, 1)), 0.6); return [r, r]; },
  },
};

export function createFeatherAtlas(size = 2048) {
  const W = size, H = size;
  const cA = document.createElement('canvas'); cA.width = W; cA.height = H;
  const cH = document.createElement('canvas'); cH.width = W; cH.height = H;
  const A = cA.getContext('2d'), Hc = cH.getContext('2d');
  A.clearRect(0, 0, W, H);
  Hc.fillStyle = '#000'; Hc.fillRect(0, 0, W, H);
  const scale = size / 2048;

  // variants per type (different split / barb patterns)
  const variants = { contour: 4, lance: 4, covert: 3, secondary: 2, primaryInner: 2, primaryOuter: 2, rectrix: 2, fluff: 2 };
  // shelf packing
  const items = [];
  for (const [k, n] of Object.entries(variants)) for (let i = 0; i < n; i++) items.push({ k, i, w: Math.round(TYPES[k].w * scale), h: Math.round(TYPES[k].h * scale) });
  items.sort((a, b) => b.h - a.h);
  let x = 0, y = 0, shelf = 0;
  const pad = Math.max(2, Math.round(4 * scale));
  const rects = {};
  for (const it of items) {
    if (x + it.w > W) { x = 0; y += shelf + pad; shelf = 0; }
    it.x = x; it.y = y; x += it.w + pad; shelf = Math.max(shelf, it.h);
    (rects[it.k] || (rects[it.k] = [])).push([it.x / W, 1 - (it.y + it.h) / H, it.w / W, it.h / H]);
    drawFeather(A, Hc, it, TYPES[it.k], rng(1337 + items.indexOf(it) * 7919), scale);
  }
  if (y + shelf > H) console.warn('feather atlas overflow');

  // normal map from height field (Sobel), alpha-weighted
  const hd = Hc.getImageData(0, 0, W, H).data;
  const nC = document.createElement('canvas'); nC.width = W; nC.height = H;
  const nX = nC.getContext('2d');
  const out = nX.createImageData(W, H), od = out.data;
  const hAt = (i, j) => hd[((clamp(j, 0, H - 1) * W) + clamp(i, 0, W - 1)) * 4] / 255;
  const k = 2.2;
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    const dx = (hAt(i + 1, j - 1) + 2 * hAt(i + 1, j) + hAt(i + 1, j + 1)) - (hAt(i - 1, j - 1) + 2 * hAt(i - 1, j) + hAt(i - 1, j + 1));
    const dy = (hAt(i - 1, j + 1) + 2 * hAt(i, j + 1) + hAt(i + 1, j + 1)) - (hAt(i - 1, j - 1) + 2 * hAt(i, j - 1) + hAt(i + 1, j - 1));
    let nx = -dx * k, ny = dy * k, nz = 1; const l = Math.hypot(nx, ny, nz); nx /= l; ny /= l; nz /= l;
    const o = (j * W + i) * 4;
    od[o] = (nx * 0.5 + 0.5) * 255; od[o + 1] = (ny * 0.5 + 0.5) * 255; od[o + 2] = (nz * 0.5 + 0.5) * 255; od[o + 3] = 255;
  }
  nX.putImageData(out, 0, 0);

  const map = new THREE.CanvasTexture(cA);
  map.colorSpace = THREE.SRGBColorSpace; map.anisotropy = 8; map.generateMipmaps = true;
  map.minFilter = THREE.LinearMipmapLinearFilter;
  const normalMap = new THREE.CanvasTexture(nC);
  normalMap.anisotropy = 8;
  return { map, normalMap, rects, types: TYPES, canvas: cA };
}

function drawFeather(A, Hc, rect, T, R, scale) {
  const { x: rx0, y: ry0, w, h } = rect;
  const rachX = rx0 + T.rachis * w;
  const Y = (v) => ry0 + (1 - v) * h;               // v -> canvas y
  const prof = T.prof;
  const vTip = 0.995;
  // --- vane silhouette (solid part from v=down) ---
  const v0 = T.down;
  const outline = [];
  const N = 90;
  for (let i = 0; i <= N; i++) { const v = v0 + (vTip - v0) * i / N; outline.push([rachX - prof(v)[0] * w, Y(v)]); }
  for (let i = N; i >= 0; i--) { const v = v0 + (vTip - v0) * i / N; outline.push([rachX + prof(v)[1] * w, Y(v)]); }
  const fillPath = (ctx) => { ctx.beginPath(); outline.forEach(([px, py], i) => (i ? ctx.lineTo(px, py) : ctx.moveTo(px, py))); ctx.closePath(); };
  A.save(); fillPath(A); A.fillStyle = 'rgb(196,196,196)'; A.fill(); A.restore();
  Hc.save(); fillPath(Hc); Hc.fillStyle = 'rgb(90,90,90)'; Hc.fill(); Hc.restore();

  // --- barbs ---
  const lw = Math.max(1, 1.1 * scale);
  const spacing = Math.max(1.6, 2.2 * scale);
  A.save(); fillPath(A); A.clip();
  Hc.save(); fillPath(Hc); Hc.clip();
  for (const side of [-1, 1]) {
    const nb = Math.floor(h * (vTip - v0 * 0.9) / spacing);
    for (let b = 0; b < nb; b++) {
      const v = v0 * 0.9 + (vTip - v0 * 0.9) * (b + R() * 0.6) / nb;
      const half = prof(Math.min(v + 0.02, 1))[side < 0 ? 0 : 1] * w;
      if (half < 0.5) continue;
      // barbs leave the rachis at an acute angle towards the tip and curve to be more parallel near the margin
      const ang = T.barb * (0.9 + 0.2 * R());
      const L = half / Math.sin(ang) * 1.1;
      const sx = rachX, sy = Y(v);
      const ex = sx + side * half * 1.12, ey = sy - Math.cos(ang) * L * 0.85;
      const cx = sx + side * half * 0.45, cy = sy - Math.cos(ang) * L * 0.25;
      const tone = 172 + R() * 44;
      A.strokeStyle = `rgba(${tone | 0},${tone | 0},${tone | 0},0.4)`; A.lineWidth = lw;
      A.beginPath(); A.moveTo(sx, sy); A.quadraticCurveTo(cx, cy, ex, ey); A.stroke();
      const ht = 140 + R() * 90;
      Hc.strokeStyle = `rgb(${ht | 0},${ht | 0},${ht | 0})`; Hc.lineWidth = lw;
      Hc.beginPath(); Hc.moveTo(sx, sy); Hc.quadraticCurveTo(cx, cy, ex, ey); Hc.stroke();
    }
  }
  // margin lighter (fringe), subtle darker inner vane near the rachis
  const g = A.createLinearGradient(rx0, 0, rx0 + w, 0);
  g.addColorStop(0, 'rgba(255,255,255,0.22)'); g.addColorStop(T.rachis, 'rgba(0,0,0,0.12)'); g.addColorStop(1, 'rgba(255,255,255,0.22)');
  A.fillStyle = g; A.fillRect(rx0, ry0, w, h);
  // slight darkening toward the base (overlapped part) is done in the shader (per-instance AO)
  A.restore(); Hc.restore();

  // --- splits: gaps where barbs separated ---
  A.save(); A.globalCompositeOperation = 'destination-out';
  Hc.save();
  const nSplit = Math.round(T.splits * (0.6 + R() * 0.8));
  for (let s = 0; s < nSplit; s++) {
    const side = R() < 0.5 ? -1 : 1;
    const v = v0 + (vTip - v0) * (0.2 + R() * 0.75);
    const half = prof(v)[side < 0 ? 0 : 1] * w;
    const ang = T.barb;
    const start = 0.35 + R() * 0.45;
    const sx = rachX + side * half * start, sy = Y(v) - Math.cos(ang) * half * start * 0.9;
    const ex = rachX + side * half * 1.2, ey = Y(v) - Math.cos(ang) * half * 1.25;
    const wdt = (1.2 + R() * 2.2) * scale;
    A.lineWidth = wdt; A.strokeStyle = 'rgba(0,0,0,1)';
    A.beginPath(); A.moveTo(sx, sy); A.lineTo(ex, ey); A.stroke();
    Hc.lineWidth = wdt; Hc.strokeStyle = '#000'; Hc.beginPath(); Hc.moveTo(sx, sy); Hc.lineTo(ex, ey); Hc.stroke();
  }
  // frayed margin: small bites along the edge
  const nFray = Math.round(80 * T.fray * (h / 400));
  for (let s = 0; s < nFray; s++) {
    const side = R() < 0.5 ? -1 : 1;
    const v = v0 + (vTip - v0) * R();
    const half = prof(v)[side < 0 ? 0 : 1] * w;
    const r = (0.8 + R() * 1.8) * scale;
    A.beginPath(); A.arc(rachX + side * half, Y(v), r, 0, Math.PI * 2); A.fill();
  }
  A.restore(); Hc.restore();

  // --- downy base (afterfeather): fine curly wisps ---
  const nDown = Math.round(260 * T.down * (w / 192));
  for (let s = 0; s < nDown; s++) {
    const v = R() * (v0 + 0.06);
    const sx = rachX + (R() - 0.5) * 6 * scale, sy = Y(v * 0.7);
    const len = (0.2 + R() * 0.3) * w;
    const a = (R() < 0.5 ? -1 : 1) * (0.4 + R() * 1.0);
    const ex = sx + Math.sin(a) * len, ey = sy - Math.cos(a) * len * 0.8;
    const cx = sx + Math.sin(a) * len * 0.3 + (R() - 0.5) * 20 * scale, cy = sy - len * 0.5;
    const t = 170 + R() * 70;
    A.strokeStyle = `rgba(${t | 0},${t | 0},${t | 0},${0.35 + R() * 0.35})`; A.lineWidth = lw * 0.9;
    A.beginPath(); A.moveTo(sx, sy); A.quadraticCurveTo(cx, cy, ex, ey); A.stroke();
    Hc.strokeStyle = 'rgb(120,120,120)'; Hc.lineWidth = lw * 0.9;
    Hc.beginPath(); Hc.moveTo(sx, sy); Hc.quadraticCurveTo(cx, cy, ex, ey); Hc.stroke();
  }

  // --- rachis (shaft) ---
  const segs = 40;
  for (let i = 0; i < segs; i++) {
    const va = (i / segs) * 0.97, vb = ((i + 1) / segs) * 0.97;
    const wd = Math.max(1, (0.05 * (1 - va) + 0.008) * w * (T.rachis < 0.45 ? 0.8 : 0.6));
    A.strokeStyle = 'rgb(228,224,214)'; A.lineWidth = wd; A.lineCap = 'round';
    A.beginPath(); A.moveTo(rachX, Y(va)); A.lineTo(rachX, Y(vb)); A.stroke();
    Hc.strokeStyle = 'rgb(255,255,255)'; Hc.lineWidth = wd * 1.4; Hc.lineCap = 'round';
    Hc.beginPath(); Hc.moveTo(rachX, Y(va)); Hc.lineTo(rachX, Y(vb)); Hc.stroke();
  }
}

// ------------------------------------------------------------------------------------------------
// Instanced feather cards
// Card space: rachis along +z (0 base .. 1 tip), vane across x (-0.5..0.5 in atlas cell units), outer
// (dorsal) surface normal +y. Instance matrix scale = (width, length, length) so bends scale with length.
// Per instance:
//   aAtlas  = atlas rect (u0, v0, du, dv)
//   aShape  = (bend, camber, flutterAmp, seed)    bend: tip displacement along +y in units of length
//   aExtra  = (rachisU, ruffleWeight, lift, baseAO)
// ------------------------------------------------------------------------------------------------

const VERT_HEAD = /* glsl */`
attribute vec4 aAtlas;
attribute vec4 aShape;
attribute vec4 aExtra;
uniform float uTime;
uniform float uFlutter;
uniform float uRuffle;
varying float vFAO;
vec3 fDeform(vec3 p, out vec3 nrm) {
  float x = p.x - (aExtra.x - 0.5);
  float z = p.z;
  float ph = uTime * (7.0 + 6.0 * aShape.w) + aShape.w * 40.0;
  float fl = aShape.z * uFlutter * (sin(ph + z * 3.0) + 0.5 * sin(2.3 * ph + 1.7)) * 0.5;
  float bend = aShape.x + fl;
  float cam = aShape.y;
  float y = bend * z * z + cam * (0.25 - x * x);
  float fx = -2.0 * cam * x;
  float fz = 2.0 * bend * z;
  vec3 n = normalize(vec3(-fx, 1.0, -fz));
  float lift = aExtra.z + aExtra.y * uRuffle * (0.6 + 0.4 * sin(aShape.w * 17.0));
  float c = cos(lift), s = sin(lift);
  vec3 q = vec3(x, y * c + z * s, -y * s + z * c);
  nrm = vec3(n.x, n.y * c + n.z * s, -n.y * s + n.z * c);
  return q;
}
`;

function patchShader(shader, uniforms, depthOnly) {
  Object.assign(shader.uniforms, uniforms);
  shader.vertexShader = VERT_HEAD + shader.vertexShader;
  if (!depthOnly) {
    shader.vertexShader = shader.vertexShader.replace('#include <beginnormal_vertex>', `
      vec3 fN; vec3 fP = fDeform(position, fN);
      vec3 objectNormal = fN;
      #ifdef USE_TANGENT
      vec3 objectTangent = vec3( tangent.xyz );
      #endif`);
    shader.vertexShader = shader.vertexShader.replace('#include <begin_vertex>', `vec3 transformed = fP;`);
  } else {
    shader.vertexShader = shader.vertexShader.replace('#include <begin_vertex>', `vec3 fN; vec3 transformed = fDeform(position, fN);`);
  }
  shader.vertexShader = shader.vertexShader.replace('#include <uv_vertex>', `#include <uv_vertex>
    vec2 fUv = aAtlas.xy + uv * aAtlas.zw;
    #ifdef USE_MAP
      vMapUv = fUv;
    #endif
    #ifdef USE_NORMALMAP
      vNormalMapUv = fUv;
    #endif
    #ifdef USE_ROUGHNESSMAP
      vRoughnessMapUv = fUv;
    #endif
    vFAO = mix(aExtra.w, 1.0, smoothstep(0.0, 0.75, position.z));`);
  shader.fragmentShader = 'varying float vFAO;\n' + shader.fragmentShader;
  if (!depthOnly) {
    shader.fragmentShader = shader.fragmentShader.replace('#include <color_fragment>', `#include <color_fragment>
      diffuseColor.rgb *= vFAO;`);
  }
}

export class FeatherSystem {
  constructor(atlas, capacity) {
    this.atlas = atlas;
    this.capacity = capacity;
    const geo = new THREE.PlaneGeometry(1, 1, 4, 12);
    // plane XY -> card XZ (normal +y), z from 0..1 base->tip; uv.y = z
    geo.rotateX(-Math.PI / 2); // (x, y, z) -> (x, 0, -y)
    const pos = geo.attributes.position;
    for (let i = 0; i < pos.count; i++) pos.setZ(i, -pos.getZ(i) + 0.5);
    // the z flip mirrored the card: restore counter-clockwise winding seen from +y (dorsal = front face)
    const idx = geo.index.array;
    for (let i = 0; i < idx.length; i += 3) { const t = idx[i + 1]; idx[i + 1] = idx[i + 2]; idx[i + 2] = t; }
    geo.computeVertexNormals();
    this.aAtlas = new THREE.InstancedBufferAttribute(new Float32Array(capacity * 4), 4);
    this.aShape = new THREE.InstancedBufferAttribute(new Float32Array(capacity * 4), 4);
    this.aExtra = new THREE.InstancedBufferAttribute(new Float32Array(capacity * 4), 4);
    this.aShape.setUsage(THREE.DynamicDrawUsage);
    this.aExtra.setUsage(THREE.DynamicDrawUsage);
    geo.setAttribute('aAtlas', this.aAtlas);
    geo.setAttribute('aShape', this.aShape);
    geo.setAttribute('aExtra', this.aExtra);

    this.uniforms = { uTime: { value: 0 }, uFlutter: { value: 0 }, uRuffle: { value: 0 } };
    const mat = new THREE.MeshPhysicalMaterial({
      map: atlas.map, normalMap: atlas.normalMap, normalScale: new THREE.Vector2(0.35, 0.35),
      roughness: 0.82, metalness: 0, side: THREE.DoubleSide, alphaTest: 0.42, alphaToCoverage: true,
      sheen: 0.2, sheenRoughness: 0.6, sheenColor: new THREE.Color(0x6a6258),
      specularIntensity: 0.22, envMapIntensity: 0.75,
    });
    mat.onBeforeCompile = (sh) => patchShader(sh, this.uniforms, false);
    mat.customProgramCacheKey = () => 'feather-v1';
    const depth = new THREE.MeshDepthMaterial({ depthPacking: THREE.RGBADepthPacking, map: atlas.map, alphaTest: 0.42, side: THREE.DoubleSide });
    depth.onBeforeCompile = (sh) => patchShader(sh, this.uniforms, true);
    depth.customProgramCacheKey = () => 'feather-depth-v1';

    this.mesh = new THREE.InstancedMesh(geo, mat, capacity);
    this.mesh.customDepthMaterial = depth;
    this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    this.mesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(capacity * 3), 3);
    this.mesh.castShadow = true; this.mesh.receiveShadow = true;
    this.mesh.frustumCulled = false;
    this.mesh.count = 0;
    this.count = 0;
    this._c = new THREE.Color();
  }

  // spec: { type, variant, width, length, color (THREE.Color / hex), bend, camber, flutter, lift, ruffle, ao }
  add(spec) {
    const i = this.count++;
    if (i >= this.capacity) throw new Error('feather capacity exceeded');
    const list = this.atlas.rects[spec.type];
    const r = list[(spec.variant ?? i) % list.length];
    // mirror: flip the vane asymmetry through the UVs (keeps a proper rotation in the instance matrix so
    // front-face / normal handling stays correct)
    if (spec.mirror) this.aAtlas.setXYZW(i, r[0] + r[2], r[1], -r[2], r[3]);
    else this.aAtlas.setXYZW(i, r[0], r[1], r[2], r[3]);
    const rach = this.atlas.types[spec.type].rachis;
    this.aShape.setXYZW(i, spec.bend ?? 0, spec.camber ?? 0, spec.flutter ?? 0, spec.seed ?? Math.random());
    this.aExtra.setXYZW(i, spec.mirror ? 1 - rach : rach, spec.ruffle ?? 0, spec.lift ?? 0, spec.ao ?? 0.7);
    this.mesh.instanceColor.setXYZ(i, ...this._c.set(spec.color ?? 0xffffff).toArray());
    this.mesh.count = this.count;
    return i;
  }
  setBend(i, bend) { this.aShape.setX(i, bend); }
  setLift(i, lift) { this.aExtra.setZ(i, lift); }
  setMatrix(i, m) { this.mesh.setMatrixAt(i, m); }
  commit() {
    this.mesh.instanceMatrix.needsUpdate = true;
    this.aShape.needsUpdate = true;
    this.aExtra.needsUpdate = true;
  }
}

// Feathers rigidly attached to one bone (tail, legs): world = bone.matrixWorld * local
const _rm = new THREE.Matrix4();
export class RigidFeathers {
  constructor(sys) { this.sys = sys; this.list = []; }
  add(id, bone, local) { const f = { id, bone, local }; this.list.push(f); return f; }
  update() {
    for (const f of this.list) { _rm.multiplyMatrices(f.bone.matrixWorld, f.local); this.sys.setMatrix(f.id, _rm); }
  }
}

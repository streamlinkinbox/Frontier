import * as THREE from 'three';
import { STRIKE, sampleStrike } from './strike-motion.js';
import { WALK, sampleWalkingFoot, walkingBodyShift } from './walk-motion.js';
import { STANCE, sampleStance } from './stance-motion.js';
import { createHindwingMap, unfoldedHindwing, installHindwingMorph } from './hindwing.js';

/**
 * Mantis religiosa — an original, parameterised anatomical mesh and skeletal rig.
 * Construction coordinates are centimetres; the exported root converts to metres.
 * No image planes, downloaded animal mesh, or AI-generated photographs are used.
 */
const V = (x = 0, y = 0, z = 0) => new THREE.Vector3(x, y, z);
const TAU = Math.PI * 2;
const clamp = THREE.MathUtils.clamp;
const lerp = THREE.MathUtils.lerp;
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
let seed = 84129;
const random = () => { seed = (Math.imul(1664525, seed) + 1013904223) | 0; return (seed >>> 0) / 4294967296; };
const col = (hex) => new THREE.Color(hex);

function canvasTexture(canvas, color = true) {
  const t = new THREE.CanvasTexture(canvas);
  t.colorSpace = color ? THREE.SRGBColorSpace : THREE.NoColorSpace;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.anisotropy = 8;
  return t;
}
function canvas(size = 1024, height = size) {
  const c = document.createElement('canvas'); c.width = size; c.height = height; return c;
}
function makeTextures() {
  const size = 1024, skin = canvas(size), rough = canvas(512), normal = canvas(512);
  const cx = skin.getContext('2d'), pixels = cx.createImageData(size, size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const i = (y * size + x) * 4;
    const broad = Math.sin(x * .026 + Math.sin(y * .019) * 2) * Math.cos(y * .034) * 2.6;
    const small = (random() - .5) * 7;
    pixels.data[i] = 93 + broad * 1.8 + small;
    pixels.data[i + 1] = 134 + broad * 1.8 + small;
    pixels.data[i + 2] = 41 + broad * .65 + small;
    pixels.data[i + 3] = 255;
  }
  cx.putImageData(pixels, 0, 0);
  // Sparse pores and tiny pigment flecks, not a uniformly noisy rock surface.
  for (let i = 0; i < 13000; i++) {
    cx.fillStyle = `rgba(48,64,20,${.025 + random() * .09})`;
    const r = .25 + random() * .65; cx.beginPath(); cx.ellipse(random() * size, random() * size, r, r * .6, random() * 3, 0, TAU); cx.fill();
  }
  const rn = normal.getContext('2d'), ni = rn.createImageData(512, 512);
  const rr = rough.getContext('2d'), ri = rr.createImageData(512, 512);
  for (let i = 0; i < ni.data.length; i += 4) {
    ni.data[i] = 128 + (random() - .5) * 15; ni.data[i + 1] = 128 + (random() - .5) * 15;
    ni.data[i + 2] = 254; ni.data[i + 3] = 255;
    const r = 130 + random() * 35;
    ri.data[i] = ri.data[i + 1] = ri.data[i + 2] = r; ri.data[i + 3] = 255;
  }
  rn.putImageData(ni, 0, 0); rr.putImageData(ri, 0, 0);

  const eye = canvas(512), en = canvas(512), ec = eye.getContext('2d'), enc = en.getContext('2d');
  ec.fillStyle = '#92a25d'; ec.fillRect(0, 0, 512, 512);
  enc.fillStyle = '#8080ff'; enc.fillRect(0, 0, 512, 512);
  const step = 8;
  for (let j = -1; j < 76; j++) for (let i = -1; i < 66; i++) {
    const x = i * step + (j % 2) * step / 2, y = j * step * .866;
    const light = 121 + random() * 4;
    ec.fillStyle = `rgb(${light + 13},${light + 24},${light - 50})`;
    ec.beginPath();
    for (let k = 0; k < 6; k++) { const a = k * TAU / 6 + Math.PI / 6; ec.lineTo(x + Math.cos(a) * 4.25, y + Math.sin(a) * 4.25); }
    ec.closePath(); ec.fill();
    ec.strokeStyle = 'rgba(64,82,29,.07)'; ec.lineWidth = .38; ec.stroke();
    const grad = enc.createRadialGradient(x - .3, y - .4, 0, x, y, 4.5);
    grad.addColorStop(0, '#858afa'); grad.addColorStop(.65, '#8080ff'); grad.addColorStop(1, '#7573f9');
    enc.fillStyle = grad; enc.beginPath(); enc.arc(x, y, 4.4, 0, TAU); enc.fill();
  }

  // Tegminal venation follows the longitudinal wing axis (V). All maps export.
  const wing = canvas(1024, 2048), wc = wing.getContext('2d');
  const wg = wc.createLinearGradient(0, 0, 1024, 0);
  wg.addColorStop(0, '#748f38'); wg.addColorStop(.4, '#7e9844'); wg.addColorStop(.82, '#6f8a32'); wg.addColorStop(1, '#a1a65b');
  wc.fillStyle = wg; wc.fillRect(0, 0, 1024, 2048);
  const image = wc.getImageData(0, 0, 1024, 2048);
  for (let i = 0; i < image.data.length; i += 4) { const r = (random() - .5) * 10; image.data[i] += r; image.data[i + 1] += r; image.data[i + 2] += r * .7; }
  wc.putImageData(image, 0, 0);
  const paths = [];
  for (let n = 0; n < 12; n++) {
    const path = [];
    for (let j = 0; j <= 80; j++) {
      const t = j / 80, x = 30 + n * 85 + Math.sin(t * 3.5 + n * .5) * 19 + t * (n - 5) * 2;
      path.push([x, t * 2048]);
    }
    paths.push(path); wc.beginPath(); path.forEach(([x, y]) => wc.lineTo(x, y));
    wc.strokeStyle = n % 3 === 0 ? 'rgba(185,191,106,.65)' : 'rgba(157,171,80,.65)'; wc.lineWidth = n % 3 === 0 ? 3.1 : 1.7; wc.stroke();
  }
  for (let i = 0; i < 11; i++) for (let j = 1; j < 63; j++) {
    const t = (j + random() * .45) / 64, h = 2048 * t;
    const x1 = 30 + i * 85 + Math.sin(t * 3.5 + i * .5) * 19 + t * (i - 5) * 2;
    const x2 = 30 + (i + 1) * 85 + Math.sin(t * 3.5 + (i + 1) * .5) * 19 + t * (i - 4) * 2;
    wc.beginPath(); wc.moveTo(x1, h); wc.lineTo(lerp(x1, x2, .45), h + 7 + random() * 12); wc.lineTo(x2, h + (random() - .5) * 16);
    wc.strokeStyle = `rgba(180,186,100,${.23 + random() * .21})`; wc.lineWidth = .85 + random() * .65; wc.stroke();
  }
  return { skin: canvasTexture(skin), normal: canvasTexture(normal, false), rough: canvasTexture(rough, false), eye: canvasTexture(eye), eyeNormal: canvasTexture(en, false), wing: canvasTexture(wing) };
}

class SurfaceBuilder {
  constructor(materials) { this.materials = materials; this.parts = new Map(); }
  part(mat) {
    if (!this.parts.has(mat)) this.parts.set(mat, { p: [], uv: [], c: [], si: [], sw: [], idx: [] });
    return this.parts.get(mat);
  }
  vertex(part, point, uv, color, bone) {
    const i = part.p.length / 3; part.p.push(point.x, point.y, point.z); part.uv.push(...uv);
    part.c.push(color.r, color.g, color.b);
    if (Array.isArray(bone)) { part.si.push(bone[2] < 1 ? bone[0] : 0, bone[2] > 0 ? bone[1] : 0, 0, 0); part.sw.push(1 - bone[2], bone[2], 0, 0); }
    else { part.si.push(bone, 0, 0, 0); part.sw.push(1, 0, 0, 0); }
    return i;
  }
  grid(mat, nu, nv, sample, color, bone, reverse = false) {
    const p = this.part(mat), start = p.p.length / 3;
    for (let i = 0; i <= nu; i++) for (let j = 0; j <= nv; j++) {
      const u = i / nu, v = j / nv;
      this.vertex(p, sample(u, v), [v, u], typeof color === 'function' ? color(u, v) : color, typeof bone === 'function' ? bone(u, v) : bone);
    }
    for (let i = 0; i < nu; i++) for (let j = 0; j < nv; j++) {
      const a = start + i * (nv + 1) + j, b = a + 1, c = a + nv + 1, d = c + 1;
      if (!reverse) p.idx.push(a, b, c, b, d, c); else p.idx.push(a, c, b, b, c, d);
    }
  }
  tube(mat, points, radius, color, bone, segments = 30, sides = 12, ratio = 1) {
    const curve = points.length === 2 ? new THREE.LineCurve3(points[0], points[1]) : new THREE.CatmullRomCurve3(points, false, 'centripetal');
    const sample = (t, v) => {
      const p = curve.getPoint(t), tangent = curve.getTangent(t).normalize();
      let n = V(1, 0, 0); if (Math.abs(tangent.x) > .94) n = V(0, 0, 1);
      n.addScaledVector(tangent, -n.dot(tangent)).normalize();
      const b = tangent.clone().cross(n).normalize();
      const r = typeof radius === 'function' ? radius(t) : radius;
      const rx = Array.isArray(r) ? r[0] : r, ry = Array.isArray(r) ? r[1] : r * ratio;
      return p.addScaledVector(n, Math.cos(TAU * v) * rx).addScaledVector(b, Math.sin(TAU * v) * ry);
    };
    this.grid(mat, segments, sides, sample, color, bone);
  }
  ellipsoid(mat, center, scale, color, bone, segments = 48, rings = 28, tilt = null) {
    this.grid(mat, rings, segments, (u, v) => {
      const a = Math.PI * u, b = TAU * v;
      const p = V(Math.sin(a) * Math.cos(b) * scale.x, -Math.cos(a) * scale.y, -Math.sin(a) * Math.sin(b) * scale.z);
      if (tilt) p.applyQuaternion(tilt);
      return p.add(center);
    }, color, bone);
  }
  build(root, skeleton) {
    const objects = [];
    for (const [name, p] of this.parts) {
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute(p.p, 3));
      g.setAttribute('uv', new THREE.Float32BufferAttribute(p.uv, 2));
      g.setAttribute('color', new THREE.Float32BufferAttribute(p.c, 3));
      g.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(p.si, 4));
      g.setAttribute('skinWeight', new THREE.Float32BufferAttribute(p.sw, 4));
      g.setIndex(p.idx); g.computeVertexNormals(); g.computeBoundingSphere();
      if (this.materials[name].normalMap) {
        g.computeTangents();
        const t = g.getAttribute('tangent'), n = g.getAttribute('normal');
        for (let i = 0; i < t.count; i++) {
          const v = V(t.getX(i), t.getY(i), t.getZ(i));
          if (!Number.isFinite(v.lengthSq()) || v.lengthSq() < .9) {
            const normal = V(n.getX(i), n.getY(i), n.getZ(i)).normalize();
            v.copy(Math.abs(normal.y) < .9 ? V(0,1,0) : V(1,0,0)).cross(normal).normalize();
            if(v.lengthSq() < .01) v.set(1,0,0);
            t.setXYZW(i,v.x,v.y,v.z,1);
          }
        }
      } else if (!this.materials[name].map && !this.materials[name].roughnessMap) g.deleteAttribute('uv');
      const mesh = new THREE.SkinnedMesh(g, this.materials[name]); mesh.name = name;
      mesh.castShadow = mesh.receiveShadow = true; mesh.frustumCulled = false;
      root.add(mesh); mesh.bind(skeleton); objects.push(mesh);
    }
    return objects;
  }
}

export function createMantis() {
  seed = 84129;
  const maps = makeTextures();
  maps.eye.repeat.set(2, 2); maps.eyeNormal.repeat.set(2, 2);
  const root = new THREE.Group(); root.name = 'Mantis_religiosa';
  const materials = {};
  function mat(name, props) { const m = new THREE.MeshPhysicalMaterial({ vertexColors: true, roughness: .53, metalness: 0, ...props }); m.name = name; materials[name] = m; }
  mat('Chitin · chlorophyll', { map: maps.skin, normalMap: maps.normal, normalScale: new THREE.Vector2(.22, .22), roughnessMap: maps.rough, roughness: .82, clearcoat: .21, clearcoatRoughness: .4, sheen: .06, sheenColor: col('#d6dc88') });
  mat('Ventral cuticle', { map: maps.skin, normalMap: maps.normal, normalScale: new THREE.Vector2(.16, .16), roughness: .59, clearcoat: .06 });
  mat('Compound eye facets', { map: maps.eye, normalMap: maps.eyeNormal, normalScale: new THREE.Vector2(.075, .075), roughness: .34, clearcoat: .22, clearcoatRoughness: .27 });
  mat('Head cuticle', { normalMap: maps.normal, normalScale: new THREE.Vector2(.19, .19), roughness: .43, clearcoat: .18, clearcoatRoughness: .4 });
  mat('Tegmina · reticulate veins', { color: '#c8d4ac', map: maps.wing, roughness: .65, side: THREE.DoubleSide, clearcoat: .08, sheen: .13, sheenColor: col('#a8b663') });
  mat('Abdominal sternites', { normalMap: maps.normal, normalScale: new THREE.Vector2(.24, .24), roughness: .61, clearcoat: .09 });
  mat('Hindwing membrane', { color: '#eef0d5', map: createHindwingMap(), roughness: .57, side: THREE.DoubleSide, transparent: true, opacity: .96, depthWrite: false });
  mat('Vein ridges', { color: '#879b46', roughness: .62 });
  mat('Arthrodial membrane', { color: '#6b783f', roughness: .76 });
  mat('Sclerotised spines', { color: '#ffffff', roughness: .43, clearcoat: .06 });
  mat('Antenna flagellum', { color: '#706b38', roughness: .6 });
  mat('Coxal ocellus', { color: '#1c251a', roughness: .57 });
  mat('Coxal ocellus centre', { color: '#d3d3ac', roughness: .65 });
  mat('Setae', { color: '#a5a875', roughness: .74 });
  mat('Ocelli and mouthparts', { color: '#4b482a', roughness: .36, clearcoat: .22 });

  const builder = new SurfaceBuilder(materials), bones = [], index = {}, rest = {};
  function bone(name, pos, parent = null) {
    const b = new THREE.Bone(); b.name = name;
    b.position.copy(parent ? pos.clone().sub(rest[parent].world) : pos);
    if (parent) bones[index[parent]].add(b); else root.add(b);
    index[name] = bones.length; bones.push(b); rest[name] = { world: pos.clone(), local: b.position.clone() };
    return index[name];
  }
  const white = col('#ffffff'), dark = col('#cbd49e'), golden = col('#e4deaa');
  const body = bone('Root', V());
  const thorax = bone('Thorax', V(0, 1.72, .04), 'Root');
  const pro = bone('Prothorax', V(0, 1.84, .25), 'Thorax');
  const headOrigin = V(0, 3.16, 2.45), head = bone('Head', headOrigin, 'Prothorax');
  const ab = bone('Abdomen_01', V(0, 1.62, -.35), 'Thorax');
  const ab2 = bone('Abdomen_02', V(0, 1.53, -1.5), 'Abdomen_01');
  const ab3 = bone('Abdomen_03', V(0, 1.4, -2.65), 'Abdomen_02');
  const abdomenSkin = t => t < .44 ? [ab, ab2, smooth(.15, .44, t)] : [ab2, ab3, smooth(.55, .9, t)];

  // Segmented abdomen with a continuous elliptical envelope and overlapping sclerites.
  const widths = [.27, .36, .47, .53, .54, .5, .43, .34, .24, .12, .02];
  function width(t) { const x = clamp(t, 0, .99999) * (widths.length - 1); return lerp(widths[Math.floor(x)], widths[Math.ceil(x)], x % 1); }
  const abdomenCenter = t => V(0, 1.64 - .29 * t, .05 - 3.56 * t);
  builder.grid('Abdominal sternites', 120, 64, (t, v) => {
    const a = v * TAU, p = abdomenCenter(t), ring = (t * 8) % 1;
    const seam = 1 - .038 * Math.exp(-Math.pow((ring - .02) / .04, 2));
    return p.add(V(Math.cos(a) * width(t) * seam, Math.sin(a) * width(t) * .50 * seam, 0));
  }, (t, v) => {
    const stripe = 1 - .12 * Math.exp(-Math.pow(((t * 8) % 1) / .07, 2));
    const ventral = Math.sin(v * TAU) < 0;
    return col(ventral ? '#806b3f' : '#718735').multiplyScalar(stripe);
  }, abdomenSkin, true);
  for (let j = 0; j < 8; j++) for (const s of [-1, 1]) {
    const t = .09 + j * .108, p = abdomenCenter(t); p.x = s * width(t) * .987; p.y -= .024;
    builder.ellipsoid('Arthrodial membrane', p, V(.005, .013, .031), white, abdomenSkin(t), 16, 10);
  }
  // Meso/metathoracic plates, compact and partly hidden below the wings.
  builder.ellipsoid('Chitin · chlorophyll', V(0, 1.72, .08), V(.31, .20, .47), col('#dce1ad'), thorax, 48, 28);
  builder.ellipsoid('Ventral cuticle', V(0, 1.57, -.25), V(.30, .15, .31), golden, thorax);

  const neckPath = [V(0, 1.84, .25), V(0, 2.12, .77), V(0, 2.58, 1.56), V(0, 3.04, 2.32)];
  builder.tube('Chitin · chlorophyll', neckPath, t => {
    const w = .102 + .075 * Math.exp(-Math.pow((t - .78) / .12, 2)) + .065 * Math.exp(-Math.pow(t / .09, 2));
    return [w, .071 + .027 * (1 - t)];
  }, (t, v) => col('#e2e5b5').multiplyScalar(1 - .07 * Math.sin(v * TAU)), pro, 76, 36);
  // Pronotal margins and subtle median carina.
  for (const s of [-1, 1]) builder.tube('Vein ridges', neckPath.map((p, i) => p.clone().add(V(s * [.155, .106, .121, .103][i], .013, -.012))), t => .006 * Math.sin(Math.PI * t) + .0015, col('#cacb8d'), pro, 50, 6);
  builder.tube('Chitin · chlorophyll', neckPath.map(p => p.clone().add(V(0, .065, -.035))), t => .006 + Math.sin(t * Math.PI) * .007, col('#e9e5be'), pro, 50, 8);
  builder.ellipsoid('Arthrodial membrane', V(0, 3.10, 2.38), V(.10, .11, .105), golden, head);
  builder.ellipsoid('Chitin · chlorophyll', V(0, 3.115, 2.38), V(.114, .07, .103), col('#e8e7bf'), head);

  // Head capsule: rounded triangular sclerite with anatomical, non-spherical plates.
  const outline = [V(0, -.292, 0), V(-.16, -.244, 0), V(-.30, -.085, 0), V(-.41, .09, 0), V(-.41, .258, 0), V(-.28, .291, 0), V(0, .276, 0), V(.28, .291, 0), V(.41, .258, 0), V(.41, .09, 0), V(.30, -.085, 0), V(.16, -.244, 0)];
  const shape = new THREE.CatmullRomCurve3(outline, true, 'catmullrom', .35);
  for (const side of [-1, 1]) {
    builder.grid('Head cuticle', 32, 112, (u, v) => {
      const edge = shape.getPoint(v % 1), r = u;
      return headOrigin.clone().add(V(edge.x * r, edge.y * r, side * (.095 + .085 * (1 - r * r)) + .042 * Math.max(0, -edge.y * r)));
    }, (u, v) => col('#789842').multiplyScalar(1 - .045 * Math.cos(v * TAU)), head, side === -1);
  }
  builder.grid('Head cuticle', 12, 112, (u, v) => {
    const edge = shape.getPoint(v % 1), bulge = 1 + .023 * Math.sin(u * Math.PI);
    return headOrigin.clone().add(V(edge.x * bulge, edge.y * bulge, lerp(-.095, .095, u) + .042 * Math.max(0, -edge.y)));
  }, col('#779449'), head, true);
  function facePlate(points, z, bulge, tint) {
    const outline = new THREE.CatmullRomCurve3(points.map(p=>V(p[0],p[1],0)),true,'catmullrom',.2);
    const center=points.reduce((p,v)=>p.add(V(v[0],v[1],0)),V()).divideScalar(points.length);
    builder.grid('Head cuticle',16,56,(u,v)=>{
      const p=center.clone().lerp(outline.getPoint(v % 1),u);
      p.z=z+bulge*(1-u*u);return p.add(headOrigin);
    },col(tint),head,true);
  }
  // Frons and clypeus are flattened, overlapping shields; no cartoon smile/bulb nose.
  facePlate([[-.115,.118],[-.144,.027],[-.096,-.040],[0,-.052],[.096,-.040],[.144,.027],[.115,.118],[0,.154]],.183,.019,'#81a24a');
  facePlate([[-.105,-.055],[-.090,-.155],[-.042,-.214],[.042,-.214],[.090,-.155],[.105,-.055]],.184,.029,'#89a951');
  builder.ellipsoid('Head cuticle',headOrigin.clone().add(V(0,-.243,.182)),V(.068,.038,.026),col('#80a64e'),head,40,24);
  builder.tube('Vein ridges',[headOrigin.clone().add(V(-.09,-.20,.18)),headOrigin.clone().add(V(-.06,-.282,.173)),headOrigin.clone().add(V(0,-.298,.181)),headOrigin.clone().add(V(.06,-.282,.173)),headOrigin.clone().add(V(.09,-.20,.18))],.004,col('#e0d7ae'),head,42,7);
  for (const s of [-1, 1]) {
    const e = headOrigin.clone().add(V(s * .344, .146, .033));
    const eq = new THREE.Quaternion().setFromAxisAngle(V(0, 0, 1), s * .29);
    builder.ellipsoid('Compound eye facets', e, V(.181, .159, .158), (u, v) => {
      const pole = Math.sin(u * Math.PI); return col('#d4dfc2').multiplyScalar(.96 + pole * .06);
    }, head, 96, 60, eq);
    const rimPoints=Array.from({length:49},(_,i)=>V(Math.cos(i/48*TAU)*.174,Math.sin(i/48*TAU)*.153,-.015).applyQuaternion(eq).add(e));
    builder.tube('Head cuticle',rimPoints,.006,col('#acb670'),head,96,7);
    const jaw = bone(`Mandible_${s < 0 ? 'L' : 'R'}`, headOrigin.clone().add(V(s * .058, -.25, .145)), 'Head');
    builder.ellipsoid('Ocelli and mouthparts', headOrigin.clone().add(V(s * .046, -.279, .163)), V(.033, .025, .025), col('#bdac80'), jaw, 28, 18);
    const p = headOrigin.clone().add(V(s * .086, -.239, .164));
    builder.tube('Sclerotised spines', [p, p.clone().add(V(s * .034, -.039, .041)), p.clone().add(V(s * .019, -.085, .053))], t => .010 * (1 - t * .5), col('#a39a67'), jaw, 24, 9);
    builder.ellipsoid('Ocelli and mouthparts', headOrigin.clone().add(V(s * .037, .198, .187)), V(.010, .009, .007), col('#d4b572'), head, 24, 16);
  }
  builder.ellipsoid('Ocelli and mouthparts', headOrigin.clone().add(V(0, .227, .18)), V(.011, .010, .008), col('#d4b572'), head, 24, 16);

  // Filiform antennae, 80 annuli each, blended over four deforming bones.
  const antennas = [];
  for (const s of [-1, 1]) {
    const side = s < 0 ? 'L' : 'R', base = headOrigin.clone().add(V(s * .147, .209, .178));
    const pts = [base, base.clone().add(V(s * .16, .30, .26)), base.clone().add(V(s * .47, .74, .59)), base.clone().add(V(s * .69, 1.06, 1.01)), base.clone().add(V(s * .70, 1.25, 1.39))];
    const curve = new THREE.CatmullRomCurve3(pts), ids = [];
    for (let k = 0; k < 4; k++) ids.push(bone(`Antenna_${side}_${k + 1}`, curve.getPoint(k / 4), k ? `Antenna_${side}_${k}` : 'Head'));
    antennas.push(ids);
    builder.ellipsoid('Ventral cuticle', base, V(.031, .030, .030), golden, head, 24, 16);
    builder.tube('Antenna flagellum', pts, t => (.0107 * Math.pow(1 - t, .65) + .0014) * (1 - .16 * Math.pow(.5 + .5 * Math.cos(t * TAU * 80), 9)), white, t => {
      const b = clamp(t * 4 - .25, 0, 2.99999), i = Math.floor(b); return [ids[i], ids[i + 1], b % 1];
    }, 320, 9);
  }

  // Folded outer tegmina and a separate pair of folded hindwings.
  const hindwingFanPoints = [];
  function wingPoint(s, t, u, under = false) {
    const w = .17 * (1 - t) + .49 * Math.pow(Math.sin(Math.PI * t), .64);
    const x = s * (.035 + w * (u - .16) + .105 * t);
    const y = 1.922 - .47 * t + Math.sin(u * Math.PI) * .105 * Math.sin(Math.PI * t) + (s < 0 ? .019 : 0) - (under ? .032 : 0);
    return V(x, y, .26 - t * (under ? 3.83 : 4.07));
  }
  for (const s of [-1, 1]) {
    const side = s < 0 ? 'L' : 'R';
    const w = bone(`Tegmen_${side}`, V(s * .18, 1.89, .23), 'Thorax');
    const h = bone(`Hindwing_${side}`, V(s * .15, 1.85, .12), 'Thorax');
    builder.grid('Hindwing membrane', 86, 26, (t, u) => wingPoint(s, t, u * .9, true), col('#f5f3e1'), h, s < 0);
    for (let i = 0; i <= 86; i++) for (let j = 0; j <= 26; j++) unfoldedHindwing(s, i / 86, j / 26).toArray(hindwingFanPoints, hindwingFanPoints.length);
    builder.grid('Tegmina · reticulate veins', 128, 32, (t, u) => wingPoint(s, t, u), white, w, s < 0);
    for (const edge of [0, 1]) {
      const pts = Array.from({ length: 45 }, (_, i) => wingPoint(s, i / 44, edge).add(V(0, .002, 0)));
      builder.tube('Vein ridges', pts, t => .003 + .004 * Math.sin(Math.PI * t), col('#d0cc9a'), w, 110, 6);
    }
    for (const u of [.17, .36, .62, .85]) {
      const pts = Array.from({ length: 40 }, (_, i) => wingPoint(s, i / 39, u + Math.sin(i / 39 * 3) * .015).add(V(0, .004, 0)));
      builder.tube('Vein ridges', pts, t => .0014 + .0019 * Math.sin(Math.PI * t), col('#bcc48c'), w, 90, 5);
    }
    // Short annulated terminal cerci.
    const p = V(s * .087, 1.338, -3.46), c = bone(`Cercus_${side}`, p, 'Abdomen_03');
    builder.tube('Ventral cuticle', [p, p.clone().add(V(s * .05, .015, -.17)), p.clone().add(V(s * .04, .041, -.34))], t => .025 * (1 - t) * (1 - .15 * Math.cos(t * TAU * 9)) + .002, golden, c, 45, 10);
  }

  function joint(p, r, b) {
    builder.ellipsoid('Arthrodial membrane', p, V(r * .72, r * .68, r * .72), col('#d4cb94'), b, 24, 16);
  }
  function segment(a, b, r, bn, tint = white, flat = 1) {
    builder.tube('Chitin · chlorophyll', [a, b], t => {
      const profile = .43 + .57 * Math.pow(Math.sin(Math.PI * (.035 + t * .93)), .5);
      return [r * profile, r * profile * flat];
    }, tint, bn, 28, 18);
  }
  function spine(base, dir, length, radius, b, tint = white) {
    const end = base.clone().addScaledVector(dir, length);
    const mid = base.clone().lerp(end, .6).add(V(0, 0, -.014));
    builder.tube('Sclerotised spines', [base, mid, end], t => radius * Math.pow(1 - t, .7) + .0003, t => col('#7f933a').lerp(col('#443323'), smooth(.35, .95, t)), b, 7, 7);
  }
  function setae(a, b, radius, bn, count = 10) {
    const dir = b.clone().sub(a).normalize(), u = V(1, 0, 0).cross(dir).normalize(), v = dir.clone().cross(u);
    for (let i = 0; i < count; i++) {
      const t = .1 + random() * .8, angle = random() * TAU;
      const outward = u.clone().multiplyScalar(Math.cos(angle)).addScaledVector(v, Math.sin(angle));
      const p = a.clone().lerp(b, t).addScaledVector(outward, radius * .8);
      builder.tube('Setae', [p, p.clone().addScaledVector(outward, .035 + random() * .027).addScaledVector(dir, .026)], t => .0013 * (1 - t) + .0002, white, bn, 3, 4);
    }
  }
  function foot(ankle, toe, bn) {
    const dir = toe.clone().sub(ankle), total = dir.length(), d = dir.clone().normalize();
    const lengths = [.43, .18, .145, .125, .12]; let t = 0;
    for (let j = 0; j < 5; j++) {
      const a = ankle.clone().addScaledVector(dir, t), end = t + lengths[j] * .95, b = ankle.clone().addScaledVector(dir, end);
      const radius = .025 * (1 - j * .10);
      builder.tube(j < 3 ? 'Ventral cuticle' : 'Sclerotised spines', [a, b], u => radius * (.64 + .36 * Math.sin(u * Math.PI)), j < 3 ? col('#dace9e') : col('#c4ba8a'), bn, 9, 10);
      joint(b, radius * .72, bn); setae(a, b, radius, bn, 7); t += lengths[j];
    }
    const sideways = V(d.z, 0, -d.x).normalize();
    for (const s of [-1, 1]) {
      const a = toe.clone().addScaledVector(sideways, s * .019);
      builder.tube('Sclerotised spines', [a, a.clone().addScaledVector(d, .045).addScaledVector(sideways, s * .015).add(V(0, .003, 0)), a.clone().addScaledVector(d, .071).add(V(0, -.014, 0))], t => .010 * (1 - t) + .0008, col('#a2a079'), bn, 12, 8);
    }
  }
  const legs = [];
  for (const [pair, hz, kz, az, tz, ky] of [['Middle', .10, .76, 1.03, 1.31, 1.43], ['Hind', -.43, -.92, -2.23, -2.55, 1.38]]) for (const s of [-1, 1]) {
    const side = s < 0 ? 'L' : 'R', prefix = `${pair}_${side}`;
    const hip = V(s * .285, pair === 'Middle' ? 1.73 : 1.60, hz), knee = V(s * 1.13, ky, kz), ankle = V(s * 1.69, .17, az), toe = V(s * 1.94, .042, tz);
    const fem = bone(`${prefix}_Femur`, hip, 'Thorax'), tib = bone(`${prefix}_Tibia`, knee, `${prefix}_Femur`), tar = bone(`${prefix}_Tarsus`, ankle, `${prefix}_Tibia`);
    joint(hip, .067, fem); segment(hip, knee, .060, fem, col('#e5dfb0'), .9); joint(knee, .057, tib);
    segment(knee, ankle, .034, tib, col('#d5d5a4'), .9); joint(ankle, .027, tar); foot(ankle, toe, tar);
    setae(hip, knee, .054, fem, 24); setae(knee, ankle, .030, tib, 28);
    for (let j = 0; j < 7; j++) { const p = knee.clone().lerp(ankle, .18 + j * .1); p.x += s * .025; spine(p, V(s * .65, -.7, .14).normalize(), .043, .006, tib, col('#d7d2a4')); }
    legs.push({ prefix, fem, tib, tar, hip, knee, ankle, toe });
  }
  const arms = [];
  for (const s of [-1, 1]) {
    const side = s < 0 ? 'L' : 'R', base = V(s * .17, 2.90, 2.08), elbow = V(s * .30, 1.82, 1.48), wrist = V(s * .39, 2.12, 2.99), end = V(s * .34, 1.66, 1.94);
    const co = bone(`Fore_${side}_Coxa`, base, 'Prothorax'), fe = bone(`Fore_${side}_Femur`, elbow, `Fore_${side}_Coxa`), ti = bone(`Fore_${side}_Tibia`, wrist, `Fore_${side}_Femur`), ta = bone(`Fore_${side}_Tarsus`, end, `Fore_${side}_Tibia`);
    joint(base, .072, co);
    builder.tube('Chitin · chlorophyll', [base, base.clone().lerp(elbow, .55).add(V(0, -.01, .018)), elbow], t => [.049 + Math.sin(t * Math.PI) * .017, .054 + Math.sin(t * Math.PI) * .023], col('#e8e1b3'), co, 48, 24);
    joint(elbow, .077, fe);
    builder.tube('Chitin · chlorophyll', [elbow, elbow.clone().lerp(wrist, .48).add(V(0, .035, 0)), wrist], t => [.041 + .029 * Math.pow(Math.sin(t * Math.PI), .7), .049 + .052 * Math.pow(Math.sin(t * Math.PI), .65)], col('#e4e3b0'), fe, 56, 28);
    joint(wrist, .071, ti);
    builder.tube('Chitin · chlorophyll', [wrist, wrist.clone().lerp(end, .45).add(V(0, -.013, 0)), end], t => [.035 - .018 * t, .047 - .025 * t], col('#ded49e'), ti, 44, 20);
    // Femoral and tibial teeth oppose each other like a folding pocket knife.
    const fd = wrist.clone().sub(elbow).normalize(), fdown = V(0, -fd.z, fd.y).normalize();
    const td = end.clone().sub(wrist).normalize(), tup = V(0, -td.z, td.y).normalize();
    for (const row of [-1, 1]) for (let j = 0; j < (row === -1 ? 13 : 5); j++) {
      const t = .15 + j * (row === -1 ? .059 : .155), p = elbow.clone().lerp(wrist, t).addScaledVector(fdown, .080).add(V(row * .037, 0, 0));
      spine(p, fdown.clone().addScaledVector(fd, -.28).normalize(), j % 2 ? .053 : .086, j % 2 ? .008 : .011, fe, col('#d4ba85'));
    }
    for (const row of [-1, 1]) for (let j = 0; j < 12; j++) {
      const t = .12 + j * .066, p = wrist.clone().lerp(end, t).addScaledVector(tup, .030).add(V(row * .023, 0, 0));
      spine(p, tup.clone().addScaledVector(td, -.22).normalize(), .042 + (j % 3 === 0 ? .017 : 0), .007, ti, col('#c6ae7a'));
    }
    // A heavier apical tibial hook; distal tarsus remains an actual five-part foot.
    const hookBase = end.clone().add(V(0, .015, 0));
    builder.tube('Sclerotised spines', [hookBase, hookBase.clone().add(V(0, .085, -.045)), hookBase.clone().add(V(0, .145, -.024))], t => .026 * Math.pow(1 - t, .6) + .0008, col('#c5ab73'), ti, 16, 10);
    foot(end, end.clone().add(V(s * .035, -.29, -.22)), ta);
    setae(base, elbow, .057, co, 18); setae(elbow, wrist, .075, fe, 23); setae(wrist, end, .032, ti, 18);
    // The characteristic black-and-ivory patch on the inner proximal coxa.
    const spot = base.clone().lerp(elbow, .235).add(V(-s * .051, 0, .020));
    const spotQ = new THREE.Quaternion().setFromAxisAngle(V(0, 0, 1), s * .21);
    builder.ellipsoid('Coxal ocellus', spot, V(.005, .089, .058), white, co, 32, 22, spotQ);
    builder.ellipsoid('Coxal ocellus centre', spot.clone().add(V(-s * .006, .016, .006)), V(.005, .039, .029), white, co, 28, 18, spotQ);
    const elevation = v => THREE.MathUtils.radToDeg(Math.atan2(v.y, v.z));
    const restCoxa = elevation(elbow.clone().sub(base)), restFemur = elevation(wrist.clone().sub(elbow));
    const restTibia = elevation(end.clone().sub(wrist)), restTarsus = elevation(V(s * .035, -.29, -.22));
    arms.push({ s, side, base, elbow, wrist, end, co, fe, ti, ta,
      restAngles: { coxa: restCoxa, femur: restFemur, tibia: restTibia, gape: 180 + restTibia - restFemur, tarsus: restTarsus },
    });
  }
  root.updateMatrixWorld(true);
  const skeleton = new THREE.Skeleton(bones);
  const meshes = builder.build(root, skeleton);
  const hindwingMesh = meshes.find(mesh => mesh.material.name === 'Hindwing membrane');
  installHindwingMorph(hindwingMesh, hindwingFanPoints);
  root.updateMatrixWorld(true);
  const identity = new THREE.Quaternion();
  function reset() { for (const b of bones) { b.position.copy(rest[b.name].local); b.quaternion.identity(); b.scale.set(1, 1, 1); } }

  // Two-bone analytic IK, baked into portable skeletal keyframes.
  function poseLeg(leg, target, shift, toePitch = 0) {
    const hip = leg.hip.clone().add(shift), a = leg.hip.distanceTo(leg.knee), b = leg.knee.distanceTo(leg.ankle);
    const direction = target.clone().sub(hip), distance = clamp(direction.length(), Math.abs(a - b) + .001, a + b - .001); direction.normalize();
    let pole = leg.knee.clone().sub(leg.hip).addScaledVector(direction, -leg.knee.clone().sub(leg.hip).dot(direction)); pole.normalize();
    const along = (a * a - b * b + distance * distance) / (2 * distance), height = Math.sqrt(Math.max(0, a * a - along * along));
    const knee = hip.clone().addScaledVector(direction, along).addScaledVector(pole, height);
    const q1 = new THREE.Quaternion().setFromUnitVectors(leg.knee.clone().sub(leg.hip).normalize(), knee.clone().sub(hip).normalize());
    const q2 = new THREE.Quaternion().setFromUnitVectors(leg.ankle.clone().sub(leg.knee).normalize(), target.clone().sub(knee).normalize());
    bones[leg.fem].quaternion.copy(q1); bones[leg.tib].quaternion.copy(q1.clone().invert().multiply(q2)); bones[leg.tar].quaternion.copy(q2.clone().invert());
    if (toePitch) bones[leg.tar].quaternion.multiply(new THREE.Quaternion().setFromAxisAngle(V(1,0,0), toePitch));
  }
  function poseArm(arm, reach, open, grip = 0) {
    const coRest = arm.elbow.clone().sub(arm.base).normalize(), feRest = arm.wrist.clone().sub(arm.elbow).normalize(), tiRest = arm.end.clone().sub(arm.wrist).normalize();
    const coTarget = V(arm.s * .13, -.11, 1.20).normalize(), feTarget = V(arm.s * .06, -.06, 1.51).normalize();
    const qco = new THREE.Quaternion().setFromUnitVectors(coRest, coTarget);
    qco.copy(identity.clone().slerp(qco, reach));
    const qfe = new THREE.Quaternion().setFromUnitVectors(feRest, feTarget); qfe.copy(identity.clone().slerp(qfe, reach));
    const tiTarget = V(arm.s * .018, .40, .98).normalize();
    const qti = new THREE.Quaternion().setFromUnitVectors(tiRest, tiTarget); qti.copy(identity.clone().slerp(qti, open));
    if (grip > 0) qti.multiply(new THREE.Quaternion().setFromAxisAngle(V(1, 0, 0), -.055 * grip));
    bones[arm.co].quaternion.copy(qco);
    bones[arm.fe].quaternion.copy(qco.clone().invert().multiply(qfe));
    bones[arm.ti].quaternion.copy(qfe.clone().invert().multiply(qti));
    bones[arm.ta].quaternion.setFromAxisAngle(V(1, 0, 0), -.12 * reach);
  }
  // Attack has anatomical hinge coordinates, separate from the small idle offsets.
  // Global segment frames are converted to parent-local bone rotations. The
  // femur, tibia and tarsus stay in one plane: no quaternion-generated roll.
  function poseStrikeArm(arm, time) {
    const r = arm.restAngles, motion = sampleStrike(time, r), axis = V(1, 0, 0);
    const hinge = angle => new THREE.Quaternion().setFromAxisAngle(axis, THREE.MathUtils.degToRad(angle));
    const tibia = motion.femur - 180 + motion.gape;
    const tarsusRelative = lerp(r.tarsus - r.tibia, 158, motion.tarsusFold);
    const qc = hinge(r.coxa - motion.coxa);
    const qf = hinge(r.femur - motion.femur);
    const qt = hinge(r.tibia - tibia);
    const qFoot = hinge(r.tarsus - (tibia + tarsusRelative));
    bones[arm.co].quaternion.copy(qc);
    bones[arm.fe].quaternion.copy(qc.clone().invert().multiply(qf));
    bones[arm.ti].quaternion.copy(qf.clone().invert().multiply(qt));
    bones[arm.ta].quaternion.copy(qt.clone().invert().multiply(qFoot));
  }
  function poseStanceArm(arm, time) {
    const r = arm.restAngles, m = sampleStance(time, arm.s);
    // Rotate the entire anatomical hinge plane outwards at the coxa. This turns
    // the inner black/ivory patches towards the threat without twisting the tibia.
    const plane = new THREE.Quaternion().setFromAxisAngle(V(0,1,0), m.splay);
    const hinge = angle => plane.clone().multiply(new THREE.Quaternion().setFromAxisAngle(V(1,0,0), THREE.MathUtils.degToRad(angle)));
    const tibia = m.femur - 180 + m.gape;
    const tarsusRelative = lerp(r.tarsus-r.tibia,158,m.tarsusFold);
    const qc=hinge(r.coxa-m.coxa), qf=hinge(r.femur-m.femur), qt=hinge(r.tibia-tibia), qFoot=hinge(r.tarsus-(tibia+tarsusRelative));
    bones[arm.co].quaternion.copy(qc);
    bones[arm.fe].quaternion.copy(qc.clone().invert().multiply(qf));
    bones[arm.ti].quaternion.copy(qf.clone().invert().multiply(qt));
    bones[arm.ta].quaternion.copy(qt.clone().invert().multiply(qFoot));
  }
  function pose(name, time, duration) {
    reset(); hindwingMesh.morphTargetInfluences[0] = 0;
    const p = time / duration, cyc = p * TAU;
    const shift = V(.012 * Math.sin(cyc), .016 * Math.sin(cyc * 2), 0);
    if (name === 'Walk') {
      shift.fromArray(walkingBodyShift(time));
      legs.forEach(leg => {
        const step = sampleWalkingFoot(time, leg.prefix);
        const target = leg.ankle.clone().add(V(0, step.liftCm, step.offsetZCm));
        const toePitch = -Math.sign(leg.toe.z - leg.ankle.z) * step.toeLift;
        poseLeg(leg, target, shift, toePitch);
      });
      bones[head].rotation.set(.008*Math.sin(cyc*2),.018*Math.sin(cyc),-.004*Math.cos(cyc*2));
      // Folded carrying posture: the raptorial pair does not pump like human arms.
      arms.forEach(arm => poseArm(arm,.008,0));
    } else if (name === 'Attack') {
      const motion = sampleStrike(time, arms[0].restAngles);
      shift.fromArray(motion.shift);
      arms.forEach(arm => poseStrikeArm(arm, time));
      legs.forEach(leg => poseLeg(leg, leg.ankle.clone(), shift));
      bones[head].rotation.x = motion.headPitch;
      bones[ab2].rotation.x = motion.abdomenPitch;
    } else if (name === 'Stance') {
      const display=sampleStance(time);
      shift.fromArray(display.shift);
      legs.forEach(leg=>poseLeg(leg,leg.ankle.clone(),shift));
      bones[pro].rotation.set(display.prothoraxPitch,0,display.prothoraxRoll);
      bones[head].rotation.set(display.headPitch,display.headYaw,0);
      bones[ab2].rotation.x=display.abdomenPitch;
      arms.forEach(arm=>poseStanceArm(arm,time));
      for(const sign of [-1,1]) {
        const wing=sampleStance(time,sign), side=sign<0?'L':'R';
        bones[index[`Tegmen_${side}`]].quaternion.setFromEuler(new THREE.Euler(wing.forewingPitch,wing.forewingYaw,0,'YXZ'));
        bones[index[`Hindwing_${side}`]].quaternion.setFromEuler(new THREE.Euler(wing.hindwingPitch,wing.hindwingYaw,0,'YXZ'));
      }
      hindwingMesh.morphTargetInfluences[0]=display.membraneUnfold;
    } else {
      legs.forEach(l => poseLeg(l, l.ankle.clone(), shift));
      bones[head].rotation.set(.025 * Math.sin(cyc * 2), .13 * Math.sin(cyc), .025 * Math.sin(cyc + .4));
      arms.forEach(a => poseArm(a, .012 * (1 + Math.sin(cyc + a.s * .6)), .008 * (1 + Math.sin(cyc * 2))));
    }
    bones[body].position.copy(shift);
    bones[ab2].scale.y = 1 + .025 * Math.sin(cyc * 2); bones[ab3].rotation.x += .009 * Math.sin(cyc * 2 - .5);
    const sensingGain = name === 'Attack' ? sampleStrike(time, arms[0].restAngles).sensingGain : name === 'Walk' ? .55 : name === 'Stance' ? .6 : 1;
    antennas.forEach((ids, j) => ids.forEach((b, k) => {
      bones[b].rotation.x = sensingGain * .022 * Math.sin(cyc * (j ? 2 : 1) - k * .7 + j);
      bones[b].rotation.z = sensingGain * .026 * Math.sin(cyc * 2 - k * .55 + j * 1.8);
    }));
    bones[index.Mandible_L].rotation.y = .015 * (1 + Math.sin(cyc * 4)); bones[index.Mandible_R].rotation.y = -.015 * (1 + Math.sin(cyc * 4));
  }

  const clips = [];
  const metadata = {
    Idle: { duration: 6, description: 'Head tracking, antenna sensing and abdominal ventilation.', loop: true },
    Walk: { duration: WALK.duration, description: 'Slow four-leg wave gait; rear-to-middle steps. In-place, matched to 2.5 mm/s travel.', loop: true, sampleRate: WALK.sampleRate, revision: WALK.revision, footfallOrder: WALK.order, supportDutyFactor: 1-WALK.swingFraction },
    Attack: { duration: STRIKE.duration, description: 'Folded setup → coxal approach → overlapping sweep / clamp → pull to mouth → recover.', loop: false, sampleRate: STRIKE.sampleRate, revision: STRIKE.revision, phaseTimes: STRIKE },
    Stance: { duration: STANCE.duration, description: 'Held deimatic display: upright prothorax, spread forelegs, exposed coxal markings and raised wing fans.', loop: true, sampleRate: STANCE.sampleRate, revision: STANCE.revision, blendInSeconds: STANCE.blendSeconds },
  };
  for (const [name, data] of Object.entries(metadata)) {
    const sampleRate = data.sampleRate || 60;
    const frameCount = Math.round(data.duration * sampleRate), times = [], wingWeights = [], frames = bones.map(() => ({ q: [], p: [], s: [] }));
    for (let f = 0; f <= frameCount; f++) {
      const t = f / sampleRate; times.push(t); pose(name, t, data.duration); wingWeights.push(hindwingMesh.morphTargetInfluences[0]);
      bones.forEach((b, i) => { b.quaternion.toArray(frames[i].q, frames[i].q.length); b.position.toArray(frames[i].p, frames[i].p.length); b.scale.toArray(frames[i].s, frames[i].s.length); });
    }
    const tracks = [];
    bones.forEach((b, i) => {
      tracks.push(new THREE.QuaternionKeyframeTrack(`${b.name}.quaternion`, times, frames[i].q));
      if (b.name === 'Root') tracks.push(new THREE.VectorKeyframeTrack(`${b.name}.position`, times, frames[i].p));
      if (b.name === 'Abdomen_02') tracks.push(new THREE.VectorKeyframeTrack(`${b.name}.scale`, times, frames[i].s));
    });
    tracks.push(new THREE.NumberKeyframeTrack('Hindwing_membrane.morphTargetInfluences[DisplayFan]',times,wingWeights));
    const clip = new THREE.AnimationClip(name, data.duration, tracks); clip.optimize(); clips.push(clip);
  }
  reset(); hindwingMesh.morphTargetInfluences[0]=0; root.updateMatrixWorld(true);
  root.scale.setScalar(.01);
  const triangles = meshes.reduce((sum, m) => sum + m.geometry.index.count / 3, 0);
  root.userData = { species: 'Mantis religiosa', sex: 'female', lifeStage: 'adult', units: 'metres', anatomicalBodyLengthMm: 70, generator: 'Frontier / anatomical surface study', triangles, bones: bones.length, originalMesh: true, animationMetadata: metadata, locomotionSpeedMetresPerSecond: WALK.speedMetresPerSecond, license: 'Original project asset; reference photographs are separately attributed.' };
  return { root, clips, skeleton, materials, metadata, triangles, bones: bones.length };
}

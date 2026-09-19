/* ============================================================
   tools/preview.mjs — offline software rasteriser
   ------------------------------------------------------------
   The sandbox has no browser/WebGL, so this renders the generated
   car straight out of the geometry buffers into PNGs. Purely a dev
   tool for eyeballing the procedural output:

     node tools/preview.mjs --style coupe --seed GT-77 --open all \
          --windows 1 --out .preview/coupe

   Writes side.png, front34.png, rear34.png, top.png, front.png
   ============================================================ */
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import * as THREE from 'three';
import { buildCar, makeSpec, STYLES, STYLE_IDS, PAINTS } from '../app/js/lowpoly-car.js';

/* ---------------- PNG writer ---------------- */
const CRC_TABLE = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c; }
  return t;
})();
function crc32(buf) { let c = 0xffffffff; for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function writePNG(file, w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 3 + 1)] = 0;
    rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  fs.writeFileSync(file, Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))
  ]));
}

/* ---------------- renderer ---------------- */
const LIGHTS = [
  { d: new THREE.Vector3(0.45, 0.85, 0.55).normalize(), c: 1.00 },
  { d: new THREE.Vector3(-0.7, 0.35, -0.4).normalize(), c: 0.42 },
  { d: new THREE.Vector3(0.1, -0.6, 0.2).normalize(), c: 0.12 }
];

function collect(root) {
  const tris = [];
  root.updateMatrixWorld(true);
  const walk = (o, vis) => {
    const v = vis && o.visible;
    if (v && o.isMesh) emit(o);
    o.children.forEach(ch => walk(ch, v));
  };
  const emit = o => {
    const pos = o.geometry.attributes.position.array;
    const m = o.matrixWorld;
    const col = o.material.color ? o.material.color : new THREE.Color('#888');
    const em = o.material.emissive ? o.material.emissive.clone().multiplyScalar(o.material.emissiveIntensity || 1) : new THREE.Color(0, 0, 0);
    const alpha = o.material.transparent ? (o.material.opacity ?? 1) : 1;
    const v = [new THREE.Vector3(), new THREE.Vector3(), new THREE.Vector3()];
    for (let i = 0; i < pos.length; i += 9) {
      v[0].set(pos[i], pos[i + 1], pos[i + 2]).applyMatrix4(m);
      v[1].set(pos[i + 3], pos[i + 4], pos[i + 5]).applyMatrix4(m);
      v[2].set(pos[i + 6], pos[i + 7], pos[i + 8]).applyMatrix4(m);
      tris.push({ a: v[0].clone(), b: v[1].clone(), c: v[2].clone(), col, em, alpha });
    }
  };
  walk(root, true);
  return tris;
}

function render(tris, w, h, eye, target, opts = {}) {
  const up = new THREE.Vector3(0, 1, 0);
  if (Math.abs(eye.clone().sub(target).normalize().dot(up)) > 0.99) up.set(0, 0, -1);
  const f = target.clone().sub(eye).normalize();
  const r = new THREE.Vector3().crossVectors(f, up).normalize();
  const u = new THREE.Vector3().crossVectors(r, f).normalize();
  const dist = eye.distanceTo(target);
  const cx = eye.x, cy = eye.y, cz = eye.z;
  const proj = p => {
    const dx = p.x - cx, dy = p.y - cy, dz = p.z - cz;
    return { x: dx * r.x + dy * r.y + dz * r.z, y: dx * u.x + dy * u.y + dz * u.z, z: dx * f.x + dy * f.y + dz * f.z };
  };
  const sc = h / (opts.view || 3.2);
  const buf = Buffer.alloc(w * h * 3);
  const zbuf = new Float64Array(w * h).fill(Infinity);
  // backdrop: vertical gradient + floor glow
  for (let y = 0; y < h; y++) {
    const t = y / h;
    const g = 0.055 + 0.10 * (1 - t) + 0.06 * Math.exp(-Math.pow((t - 0.72) / 0.16, 2));
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 3;
      const vg = 1 - 0.45 * Math.pow(Math.hypot((x / w - 0.5) * 1.4, (y / h - 0.5) * 1.5), 2);
      buf[i] = 255 * g * vg * 0.72; buf[i + 1] = 255 * g * vg * 0.78; buf[i + 2] = 255 * g * vg * 1.0;
    }
  }
  const put = (x, y, col, alpha) => {
    const i = (y * w + x) * 3;
    buf[i] = buf[i] * (1 - alpha) + col[0] * alpha;
    buf[i + 1] = buf[i + 1] * (1 - alpha) + col[1] * alpha;
    buf[i + 2] = buf[i + 2] * (1 - alpha) + col[2] * alpha;
  };
  const passes = [tris.filter(t => t.alpha >= 0.99), tris.filter(t => t.alpha < 0.99)];
  passes[1].sort((A, B) => {
    const da = (A.a.z + A.b.z + A.c.z), db = (B.a.z + B.b.z + B.c.z);
    return da - db;
  });
  passes.forEach((list, pass) => {
    for (const t of list) {
      const A = proj(t.a), B = proj(t.b), C = proj(t.c);
      const n = new THREE.Vector3().crossVectors(B.a ?? t.b.clone().sub(t.a), t.c.clone().sub(t.a));
      // proper normal
      n.copy(new THREE.Vector3().crossVectors(t.b.clone().sub(t.a), t.c.clone().sub(t.a)));
      if (n.lengthSq() < 1e-14) continue;
      n.normalize();
      if (n.dot(f) > 0) n.multiplyScalar(-1);         // double sided
      let li = 0.20;
      for (const L of LIGHTS) li += L.c * Math.max(0, n.dot(L.d));
      li += 0.30 * Math.pow(Math.max(0, n.dot(f.clone().negate().add(LIGHTS[0].d).normalize())), 6);
      li = Math.min(1.45, li);
      const col = [
        Math.min(255, 255 * (t.col.r * li + t.em.r)),
        Math.min(255, 255 * (t.col.g * li + t.em.g)),
        Math.min(255, 255 * (t.col.b * li + t.em.b))
      ];
      // screen coords
      const P = [A, B, C].map(p => ({ x: w / 2 + p.x * sc, y: h / 2 - p.y * sc, z: p.z }));
      const minX = Math.max(0, Math.floor(Math.min(P[0].x, P[1].x, P[2].x)));
      const maxX = Math.min(w - 1, Math.ceil(Math.max(P[0].x, P[1].x, P[2].x)));
      const minY = Math.max(0, Math.floor(Math.min(P[0].y, P[1].y, P[2].y)));
      const maxY = Math.min(h - 1, Math.ceil(Math.max(P[0].y, P[1].y, P[2].y)));
      const d = (P[1].y - P[2].y) * (P[0].x - P[2].x) + (P[2].x - P[1].x) * (P[0].y - P[2].y);
      if (Math.abs(d) < 1e-9) continue;
      for (let y = minY; y <= maxY; y++) {
        for (let x = minX; x <= maxX; x++) {
          const px = x + 0.5, py = y + 0.5;
          let w0 = ((P[1].y - P[2].y) * (px - P[2].x) + (P[2].x - P[1].x) * (py - P[2].y)) / d;
          let w1 = ((P[2].y - P[0].y) * (px - P[2].x) + (P[0].x - P[2].x) * (py - P[2].y)) / d;
          let w2 = 1 - w0 - w1;
          if (w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) continue;
          const z = w0 * P[0].z + w1 * P[1].z + w2 * P[2].z;
          const zi = y * w + x;
          if (pass === 0) { if (z >= zbuf[zi]) continue; zbuf[zi] = z; put(x, y, col, t.alpha); }
          else { if (z > zbuf[zi] + 1e-3) continue; put(x, y, col, t.alpha * 0.85); }
        }
      }
    }
  });
  return buf;
}

/* ---------------- cli ---------------- */
const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 && argv[i + 1] ? argv[i + 1] : d; };
const style = arg('style', 'coupe');
const seed = arg('seed', 'FR-0001');
const out = arg('out', '.preview/' + style);
const openArg = arg('open', 'none');
const win = parseFloat(arg('windows', '0'));
const view = parseFloat(arg('view', '0'));

const paintName = arg('paint', '');
const paint = paintName ? (PAINTS.find(p => p.id === paintName) || PAINTS[2]) : undefined;
const spec = makeSpec({ style, seed, paint });
const car = buildCar(spec);
const t = parseFloat(arg('t', '1'));
if (openArg !== 'none') {
  const want = openArg === 'all' ? null : openArg.split(',');
  car.parts.forEach(p => {
    if (!want || want.includes(p.id) || (want.includes('doors') && p.id.startsWith('door'))) {
      p.group.rotation[p.axis] = p.dir * p.open * t;
      p.t = t;
    }
  });
}
if (win > 0) car.doors.forEach(d => { d.glass.position.y = d.pivot.y - win * d.glassTravel; d.drop = win; });
if (view) car.wheels.forEach(w2 => { if (w2.userData.wheel.front) w2.rotation.y = view * Math.PI / 180; });

const only = arg('only', '');
if (only) {
  const keep = only.split(',').map(Number);
  car.root.children.filter((c, i) => !keep.includes(i)).forEach(c => { c.visible = false; });
}
fs.mkdirSync(out, { recursive: true });
const box = new THREE.Box3().setFromObject(car.root);
const c = box.getCenter(new THREE.Vector3());
const size = box.getSize(new THREE.Vector3());
const tris = collect(car.root);

const W = parseInt(arg('w', '760'), 10), H = parseInt(arg('h', '440'), 10);
const span = Math.max(size.y * 1.55, size.z * 1.5);
const target = arg('target', '');
const tgt = target ? target.split(',').map(Number) : null;
const views = {
  side: tgt ? tgt : [c.x + 0.0, c.y + 0.15, c.z + 9],
  front34: [c.x + 4.6, c.y + 1.55, c.z + 4.6],
  rear34: [c.x - 4.7, c.y + 1.5, c.z + 4.5],
  top: [c.x + 0.9, c.y + 8, c.z + 1.4],
  front: [c.x + 9, c.y + 0.55, c.z + 0.6]
};
for (const [name, eye] of Object.entries(views)) {
  const v = name === 'top' ? span * 0.95 : span;
  const buf = render(tris, W, H, new THREE.Vector3(...eye), c.clone(), { view: v });
  writePNG(path.join(out, name + '.png'), W, H, buf);
}
// diagnostics
console.log(JSON.stringify({
  style, seed, tris: tris.length,
  dims: { L: +size.x.toFixed(3), W: +size.z.toFixed(3), H: +size.y.toFixed(3) },
  centre: [+c.x.toFixed(2), +c.y.toFixed(2), +c.z.toFixed(2)],
  stations: car.sp.stations.length,
  parts: car.parts.map(p => p.id + ':' + p.label),
  doors: car.doors.map(d => d.id),
  paint: spec.paint.name, rim: spec.rim.style + '/' + spec.rim.name,
  finite: tris.every(x => [x.a, x.b, x.c].every(v => Number.isFinite(v.x) && Number.isFinite(v.y) && Number.isFinite(v.z)))
}, null, 1));
console.log('wrote', out);

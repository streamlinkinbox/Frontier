/* RoadForge — math utilities (vectors, matrices, noise, RNG). Classic script, global namespace RF. */
(function (RF) {
  'use strict';
  const V = {
    add: (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]],
    sub: (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]],
    mul: (a, s) => [a[0] * s, a[1] * s, a[2] * s],
    madd: (a, b, s) => [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s],
    dot: (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2],
    cross: (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]],
    len: (a) => Math.hypot(a[0], a[1], a[2]),
    lenXZ: (a) => Math.hypot(a[0], a[2]),
    dist: (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]),
    distXZ: (a, b) => Math.hypot(a[0] - b[0], a[2] - b[2]),
    norm: (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; },
    normXZ: (a) => { const l = Math.hypot(a[0], a[2]) || 1; return [a[0] / l, 0, a[2] / l]; },
    lerp: (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t],
    /** +90 degree rotation of a heading in the XZ plane (left-hand side when looking along d from above with +Z down). */
    perp: (d) => [-d[2], 0, d[0]],
    heading: (d) => Math.atan2(d[2], d[0]),
    fromHeading: (a) => [Math.cos(a), 0, Math.sin(a)],
    eq: (a, b, e = 1e-9) => Math.abs(a[0] - b[0]) < e && Math.abs(a[1] - b[1]) < e && Math.abs(a[2] - b[2]) < e,
    clone: (a) => [a[0], a[1], a[2]],
  };
  const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
  const lerp = (a, b, t) => a + (b - a) * t;
  const smoothstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
  const TAU = Math.PI * 2;
  /** Signed angle difference wrapped to (-PI, PI]. */
  const angDiff = (a, b) => { let d = b - a; while (d <= -Math.PI) d += TAU; while (d > Math.PI) d -= TAU; return d; };
  /** CCW gap from a to b in [0, TAU). */
  const angGap = (a, b) => { let d = b - a; while (d < 0) d += TAU; while (d >= TAU) d -= TAU; return d; };

  /* ---------- Seeded RNG ---------- */
  function mulberry32(seed) {
    let a = seed >>> 0;
    return function () {
      a = (a + 0x6D2B79F5) >>> 0;
      let t = a;
      t = Math.imul(t ^ (t >>> 15), t | 1);
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  function hashStr(s) { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; }
  function rng(seed) {
    const r = mulberry32(typeof seed === 'string' ? hashStr(seed) : seed | 0);
    r.range = (a, b) => a + (b - a) * r();
    r.int = (a, b) => Math.floor(a + (b - a + 1) * r());
    r.pick = (arr) => arr[Math.floor(r() * arr.length)];
    r.chance = (p) => r() < p;
    return r;
  }
  /* ---------- Value noise (CPU) ---------- */
  function hash2(x, y) { let h = Math.imul(x | 0, 374761393) + Math.imul(y | 0, 668265263); h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
  function noise2(x, y) {
    const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi;
    const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    return lerp(lerp(hash2(xi, yi), hash2(xi + 1, yi), u), lerp(hash2(xi, yi + 1), hash2(xi + 1, yi + 1), u), v);
  }
  function fbm2(x, y, oct = 4) { let a = 0.5, f = 1, s = 0, n = 0; for (let i = 0; i < oct; i++) { s += a * noise2(x * f, y * f); n += a; a *= 0.5; f *= 2.03; } return s / n; }

  /* ---------- Matrices (column-major Float32Array(16)) ---------- */
  const M4 = {
    identity: () => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]),
    mul(a, b, out) {
      out = out || new Float32Array(16);
      const o = new Float32Array(16);
      for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
        o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
      }
      out.set(o); return out;
    },
    perspective(fovy, aspect, near, far) {
      const f = 1 / Math.tan(fovy / 2), nf = 1 / (near - far);
      // WebGPU clip z in [0,1]
      return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, far * nf, -1, 0, 0, far * near * nf, 0]);
    },
    ortho(l, r, b, t, n, f) {
      return new Float32Array([2 / (r - l), 0, 0, 0, 0, 2 / (t - b), 0, 0, 0, 0, 1 / (n - f), 0, (l + r) / (l - r), (t + b) / (b - t), n / (n - f), 1]);
    },
    lookAt(eye, target, up) {
      const z = V.norm(V.sub(eye, target));
      let x = V.cross(up, z); if (V.len(x) < 1e-6) x = V.cross([1, 0, 0], z); x = V.norm(x);
      const y = V.cross(z, x);
      return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -V.dot(x, eye), -V.dot(y, eye), -V.dot(z, eye), 1]);
    },
    invert(m) {
      const a = m, o = new Float32Array(16);
      const a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3], a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7], a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11], a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];
      const b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11, b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12, b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30, b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
      let det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06; if (!det) return o; det = 1 / det;
      o[0] = (a11 * b11 - a12 * b10 + a13 * b09) * det; o[1] = (a02 * b10 - a01 * b11 - a03 * b09) * det; o[2] = (a31 * b05 - a32 * b04 + a33 * b03) * det; o[3] = (a22 * b04 - a21 * b05 - a23 * b03) * det;
      o[4] = (a12 * b08 - a10 * b11 - a13 * b07) * det; o[5] = (a00 * b11 - a02 * b08 + a03 * b07) * det; o[6] = (a32 * b02 - a30 * b05 - a33 * b01) * det; o[7] = (a20 * b05 - a22 * b02 + a23 * b01) * det;
      o[8] = (a10 * b10 - a11 * b08 + a13 * b06) * det; o[9] = (a01 * b08 - a00 * b10 - a03 * b06) * det; o[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det; o[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
      o[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det; o[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det; o[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det; o[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
      return o;
    },
    transformPoint(m, p) {
      const x = p[0], y = p[1], z = p[2];
      const w = m[3] * x + m[7] * y + m[11] * z + m[15] || 1;
      return [(m[0] * x + m[4] * y + m[8] * z + m[12]) / w, (m[1] * x + m[5] * y + m[9] * z + m[13]) / w, (m[2] * x + m[6] * y + m[10] * z + m[14]) / w];
    },
    transformDir(m, d) { return [m[0] * d[0] + m[4] * d[1] + m[8] * d[2], m[1] * d[0] + m[5] * d[1] + m[9] * d[2], m[2] * d[0] + m[6] * d[1] + m[10] * d[2]]; },
    /** Build TRS from position, heading (rotation about Y, radians), optional scale & pitch/roll. */
    trs(p, yaw = 0, s = 1, pitch = 0, roll = 0) {
      const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch), cr = Math.cos(roll), sr = Math.sin(roll);
      const sx = Array.isArray(s) ? s[0] : s, sy2 = Array.isArray(s) ? s[1] : s, sz = Array.isArray(s) ? s[2] : s;
      // R = Ry(yaw) * Rx(pitch) * Rz(roll) ; columns = transformed basis vectors
      const r00 = cy * cr + sy * sp * sr, r01 = cp * sr, r02 = -sy * cr + cy * sp * sr;
      const r10 = -cy * sr + sy * sp * cr, r11 = cp * cr, r12 = sy * sr + cy * sp * cr;
      const r20 = sy * cp, r21 = -sp, r22 = cy * cp;
      return new Float32Array([r00 * sx, r01 * sx, r02 * sx, 0, r10 * sy2, r11 * sy2, r12 * sy2, 0, r20 * sz, r21 * sz, r22 * sz, 0, p[0], p[1], p[2], 1]);
    },
    translation(x, y, z) { return new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]); },
    /** Matrix whose columns are the given basis vectors (x, y, z) and origin. */
    fromBasis(x, y, z, o) { return new Float32Array([x[0], x[1], x[2], 0, y[0], y[1], y[2], 0, z[0], z[1], z[2], 0, o[0], o[1], o[2], 1]); },
    /** Frame matrix from position, forward (unit, XZ-ish) and up vectors; x = right. */
    frame(p, fwd, up = [0, 1, 0], s = 1) {
      const z = V.norm(fwd); let x = V.norm(V.cross(up, z)); const y = V.cross(z, x);
      return new Float32Array([x[0] * s, x[1] * s, x[2] * s, 0, y[0] * s, y[1] * s, y[2] * s, 0, z[0] * s, z[1] * s, z[2] * s, 0, p[0], p[1], p[2], 1]);
    },
  };

  RF.V = V; RF.M4 = M4; RF.clamp = clamp; RF.lerp = lerp; RF.smoothstep = smoothstep; RF.TAU = TAU;
  RF.angDiff = angDiff; RF.angGap = angGap; RF.rng = rng; RF.hashStr = hashStr; RF.noise2 = noise2; RF.fbm2 = fbm2; RF.hash2 = hash2;
})(globalThis.RF || (globalThis.RF = {}));

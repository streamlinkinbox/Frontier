import * as THREE from 'three';

// Parametric surface toolkit used to build every body part of the tarantula.
// A surface function fn(u, v, out) writes:
//   out.p     : THREE.Vector3 position (object space)
//   out.hair  : THREE.Color   hair colour (linear)
//   out.fur   : THREE.Vector3 (pile length cm, strands/cm, stiffness)
//   out.comb  : THREE.Vector3 combing direction (need not be tangent; it is projected)
//   out.furUV : THREE.Vector2 fur-space coordinates in centimetres
// u is periodic in [0,1]; v in [0,1].

export function makeSample() {
  return {
    p: new THREE.Vector3(), n: new THREE.Vector3(), hair: new THREE.Color(), fur: new THREE.Vector3(0.06, 50, 0.7),
    comb: new THREE.Vector3(0, 0, 1), furUV: new THREE.Vector2(), region: 0,
  };
}

const _a = makeSample(), _b = makeSample(), _pu = new THREE.Vector3(), _pv = new THREE.Vector3();

export class Surface {
  constructor(fn, { center = new THREE.Vector3(), wrapU = true, vRange = [0, 1] } = {}) {
    this.fn = fn; this.center = center; this.wrapU = wrapU; this.vRange = vRange; this.flip = false;
    // orientation test at a mid sample
    const s = makeSample();
    this._rawSample(0.37, 0.5, s);
    const d = new THREE.Vector3().subVectors(s.p, center);
    this.flip = s.n.dot(d) < 0;
  }

  _rawSample(u, v, s) {
    const fn = this.fn;
    fn(u, v, s);
    const e = 1e-3;
    const vc = Math.min(Math.max(v, 0.004), 0.996);
    fn(u + e, vc, _a); fn(u - e, vc, _b); _pu.subVectors(_a.p, _b.p);
    fn(u, vc + e, _a); fn(u, vc - e, _b); _pv.subVectors(_a.p, _b.p);
    s.n.crossVectors(_pu, _pv);
    if (s.n.lengthSq() < 1e-14) s.n.subVectors(s.p, this.center);
    s.n.normalize();
    if (this.flip) s.n.negate();
    return s;
  }

  sample(u, v, s = makeSample()) {
    this._rawSample(u, v, s);
    // project comb onto tangent plane
    s.comb.addScaledVector(s.n, -s.comb.dot(s.n));
    if (s.comb.lengthSq() < 1e-8) s.comb.set(0, 0, 1).addScaledVector(s.n, -s.n.z);
    s.comb.normalize();
    return s;
  }

  // Base mesh geometry with all fur attributes
  build(nu, nv) {
    const count = (nu + 1) * (nv + 1);
    const pos = new Float32Array(count * 3), nor = new Float32Array(count * 3), col = new Float32Array(count * 3);
    const fur = new Float32Array(count * 3), comb = new Float32Array(count * 3), fuv = new Float32Array(count * 2), uv = new Float32Array(count * 2);
    const s = makeSample();
    const [v0, v1] = this.vRange;
    let k = 0;
    for (let j = 0; j <= nv; j++) {
      const v = v0 + (v1 - v0) * (j / nv);
      for (let i = 0; i <= nu; i++) {
        const u = i / nu;
        this.sample(u, v, s);
        pos.set([s.p.x, s.p.y, s.p.z], k * 3);
        nor.set([s.n.x, s.n.y, s.n.z], k * 3);
        col.set([s.hair.r, s.hair.g, s.hair.b], k * 3);
        fur.set([s.fur.x, s.fur.y, s.fur.z], k * 3);
        comb.set([s.comb.x, s.comb.y, s.comb.z], k * 3);
        fuv.set([s.furUV.x, s.furUV.y], k * 2);
        uv.set([u, v], k * 2);
        k++;
      }
    }
    const idx = [];
    for (let j = 0; j < nv; j++) {
      for (let i = 0; i < nu; i++) {
        const a = j * (nu + 1) + i, b = a + 1, c = a + nu + 2, d = a + nu + 1;
        if (!this.flip) idx.push(a, b, c, a, c, d); else idx.push(a, c, b, a, d, c);
      }
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
    g.setAttribute('color', new THREE.BufferAttribute(col, 3));
    g.setAttribute('aFur', new THREE.BufferAttribute(fur, 3));
    g.setAttribute('aComb', new THREE.BufferAttribute(comb, 3));
    g.setAttribute('aFurUV', new THREE.BufferAttribute(fuv, 2));
    g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    g.setIndex(idx);
    g.computeBoundingSphere();
    return g;
  }

  // Area-weighted random sampling table
  _areaTable(nu = 48, nv = 48) {
    if (this._table) return this._table;
    const s = makeSample(), q = makeSample(), r = makeSample();
    const areas = new Float32Array(nu * nv);
    const [v0, v1] = this.vRange;
    let total = 0;
    for (let j = 0; j < nv; j++) for (let i = 0; i < nu; i++) {
      const u = i / nu, v = v0 + (v1 - v0) * (j / nv), du = 1 / nu, dv = (v1 - v0) / nv;
      this.fn(u, v, s); this.fn(u + du, v, q); this.fn(u, v + dv, r);
      const a = _pu.subVectors(q.p, s.p).cross(_pv.subVectors(r.p, s.p)).length();
      total += a; areas[j * nu + i] = total;
    }
    this._table = { areas, total, nu, nv };
    return this._table;
  }

  randomUV(rnd) {
    const t = this._areaTable();
    const x = rnd() * t.total;
    let lo = 0, hi = t.areas.length - 1;
    while (lo < hi) { const mid = (lo + hi) >> 1; if (t.areas[mid] < x) lo = mid + 1; else hi = mid; }
    const i = lo % t.nu, j = (lo / t.nu) | 0;
    const [v0, v1] = this.vRange;
    return [(i + rnd()) / t.nu, v0 + (v1 - v0) * ((j + rnd()) / t.nv)];
  }

  get area() { return this._areaTable().total; }

  // Long guard setae. spec(u, v, sample, rnd) -> null | {len, width, tilt, curl, spread, c0, c1}
  buildStrands(count, rnd, spec, segments = 4) {
    const verts = [];
    const s = makeSample();
    const bin = new THREE.Vector3(), dir = new THREE.Vector3(), c = new THREE.Vector3(), p = new THREE.Vector3(), t = new THREE.Vector3();
    let made = 0, guard = 0;
    while (made < count && guard < count * 6) {
      guard++;
      const [u, v] = this.randomUV(rnd);
      this.sample(u, v, s);
      const h = spec(u, v, s, rnd);
      if (!h) continue;
      // comb direction with azimuthal jitter around the normal
      const az = (rnd() - 0.5) * 2 * (h.spread ?? 0.5);
      bin.crossVectors(s.n, s.comb).normalize();
      c.copy(s.comb).multiplyScalar(Math.cos(az)).addScaledVector(bin, Math.sin(az)).normalize();
      const tilt = h.tilt;
      dir.copy(s.n).multiplyScalar(Math.cos(tilt)).addScaledVector(c, Math.sin(tilt)).normalize();
      const curl = h.curl ?? 0.25;
      const root = s.p.clone().addScaledVector(s.n, -0.004);
      for (let k = 0; k <= segments; k++) {
        const tt = k / segments;
        // p(t) = root + L (dir t + (c*curl - n*0.25*curl) t^2)
        p.copy(root).addScaledVector(dir, h.len * tt).addScaledVector(c, h.len * curl * tt * tt).addScaledVector(s.n, -h.len * curl * 0.3 * tt * tt);
        t.copy(dir).addScaledVector(c, 2 * curl * tt).addScaledVector(s.n, -0.6 * curl * tt).normalize();
        const col = new THREE.Color().copy(h.c0).lerp(h.c1, Math.pow(tt, 0.7));
        for (let side = -1; side <= 1; side += 2) {
          verts.push({ p: p.clone(), t: t.clone(), n: s.n.clone(), side, w: h.width, tt, col });
        }
      }
      made++;
    }
    const n = verts.length;
    const pos = new Float32Array(n * 3), tan = new Float32Array(n * 3), nor = new Float32Array(n * 3), col = new Float32Array(n * 3);
    const side = new Float32Array(n), width = new Float32Array(n), at = new Float32Array(n);
    verts.forEach((q, i) => {
      pos.set([q.p.x, q.p.y, q.p.z], i * 3); tan.set([q.t.x, q.t.y, q.t.z], i * 3); nor.set([q.n.x, q.n.y, q.n.z], i * 3);
      col.set([q.col.r, q.col.g, q.col.b], i * 3); side[i] = q.side; width[i] = q.w; at[i] = q.tt;
    });
    const idx = [];
    const per = (segments + 1) * 2;
    for (let h = 0; h < made; h++) {
      const b = h * per;
      for (let k = 0; k < segments; k++) {
        const a0 = b + k * 2, a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
        idx.push(a0, a1, b1, a0, b1, b0);
      }
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
    g.setAttribute('color', new THREE.BufferAttribute(col, 3));
    g.setAttribute('aTan', new THREE.BufferAttribute(tan, 3));
    g.setAttribute('aSide', new THREE.BufferAttribute(side, 1));
    g.setAttribute('aWidth', new THREE.BufferAttribute(width, 1));
    g.setAttribute('aT', new THREE.BufferAttribute(at, 1));
    g.setIndex(idx);
    g.computeBoundingSphere();
    return g;
  }
}

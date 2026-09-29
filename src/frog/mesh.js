import * as THREE from 'three';
const V=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
const TAU=Math.PI*2;

export class SurfaceBuilder {
  constructor(materials) { this.materials = materials; this.parts = new Map(); }
  part(mat) {
    if (!this.parts.has(mat)) this.parts.set(mat, { p: [], uv: [], c: [], si: [], sw: [], idx: [] });
    return this.parts.get(mat);
  }
  vertex(part, point, uv, color, bone) {
    const i = part.p.length / 3; part.p.push(point.x, point.y, point.z); part.uv.push(...uv);
    part.c.push(color.r, color.g, color.b);
    if (Array.isArray(bone)) { const w=THREE.MathUtils.clamp(bone[2],0,1); part.si.push(w < 1 ? bone[0] : 0, w > 0 ? bone[1] : 0, 0, 0); part.sw.push(1-w,w,0,0); }
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
      g.setIndex(p.idx); g.computeVertexNormals(); if(p.n)g.attributes.normal.array.set(p.n); g.computeBoundingSphere();
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

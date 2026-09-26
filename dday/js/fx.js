// Particle effects: explosions, smoke, dust, debris, splashes. Instanced with per-instance colour+alpha.
import * as THREE from '../vendor/three.module.min.js';
import { rng, lerp, TAU } from './util.js';

const R = rng(99);

function makePool(scene, geo, count, additive, fog) {
  const mat = new THREE.ShaderMaterial({
    uniforms: { fogColor: { value: fog.color }, fogNear: { value: fog.near }, fogFar: { value: fog.far }, sun: { value: new THREE.Vector3(0.4, 0.8, -0.45).normalize() } },
    vertexShader: `
      attribute vec4 iColor; varying vec4 vC; varying float vFog; varying float vL; uniform vec3 sun;
      void main(){
        vC = iColor;
        vec3 n = normalize(mat3(instanceMatrix) * normal);
        vL = 0.62 + 0.38 * max(dot(n, sun), 0.0);
        vec4 mv = modelViewMatrix * instanceMatrix * vec4(position, 1.0);
        vFog = -mv.z;
        gl_Position = projectionMatrix * mv;
      }`,
    fragmentShader: `
      varying vec4 vC; varying float vFog; varying float vL; uniform vec3 fogColor; uniform float fogNear; uniform float fogFar;
      void main(){
        vec3 c = vC.rgb * ${additive ? '1.0' : 'vL'};
        float f = smoothstep(fogNear, fogFar, vFog);
        ${additive ? 'c *= (1.0 - f);' : 'c = mix(c, fogColor, f);'}
        gl_FragColor = vec4(c, vC.a);
      }`,
    transparent: true, depthWrite: false,
    blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
  });
  const im = new THREE.InstancedMesh(geo, mat, count);
  im.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  const col = new THREE.InstancedBufferAttribute(new Float32Array(count * 4), 4);
  col.setUsage(THREE.DynamicDrawUsage);
  im.geometry = geo.clone();
  im.geometry.setAttribute('iColor', col);
  im.frustumCulled = false;
  im.count = 0;
  scene.add(im);
  const P = [];
  for (let i = 0; i < count; i++) P.push({ alive: false });
  return { im, col, P, next: 0, count };
}

export class FX {
  constructor(scene, fog) {
    this.scene = scene;
    this.smoke = makePool(scene, new THREE.IcosahedronGeometry(1, 0), 1400, false, fog);
    this.fire = makePool(scene, new THREE.IcosahedronGeometry(1, 0), 500, true, fog);
    this.chunks = makePool(scene, new THREE.BoxGeometry(1, 1, 1), 400, false, fog);
    this.m = new THREE.Matrix4(); this.q = new THREE.Quaternion(); this.e = new THREE.Euler(); this.v = new THREE.Vector3(); this.s = new THREE.Vector3();
    // shock rings
    this.rings = [];
    const rg = new THREE.RingGeometry(0.85, 1, 24); rg.rotateX(-Math.PI / 2);
    for (let i = 0; i < 12; i++) {
      const m = new THREE.Mesh(rg, new THREE.MeshBasicMaterial({ color: 0xfff0d0, transparent: true, opacity: 0, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide }));
      m.visible = false; scene.add(m); this.rings.push({ m, t: 1, life: 0 });
    }
    // light flash
    this.flash = new THREE.PointLight(0xffa860, 0, 60, 2); scene.add(this.flash); this.flashT = 0;
  }
  spawn(pool, o) {
    const p = pool.P[pool.next]; pool.next = (pool.next + 1) % pool.count;
    Object.assign(p, {
      alive: true, t: 0, life: o.life ?? 1, x: o.x, y: o.y, z: o.z, vx: o.vx ?? 0, vy: o.vy ?? 0, vz: o.vz ?? 0,
      s0: o.s0 ?? 1, s1: o.s1 ?? o.s0 ?? 1, g: o.g ?? 0, drag: o.drag ?? 0, c0: o.c0, c1: o.c1 ?? o.c0,
      a0: o.a0 ?? 1, a1: o.a1 ?? 0, rx: R() * TAU, ry: R() * TAU, rz: R() * TAU, sp: o.spin ?? 0, floor: o.floor ?? -1e9, sx: o.sx ?? 1, sy: o.sy ?? 1,
    });
  }
  explosion(x, y, z, size = 1, opts = {}) {
    const water = opts.water;
    const dirt = opts.dirt || new THREE.Color('#6b5a44');
    // fireball
    if (!water) {
      for (let i = 0; i < 14 * size; i++) {
        const a = R() * TAU, u = R() * 0.9, sp = R.range(2, 7) * size;
        this.spawn(this.fire, { x, y: y + 0.6, z, vx: Math.cos(a) * sp * u, vy: R.range(2, 8) * size, vz: Math.sin(a) * sp * u, life: R.range(0.35, 0.7), s0: 0.8 * size, s1: R.range(2.2, 3.4) * size, drag: 3, c0: new THREE.Color(1.0, 0.85, 0.5), c1: new THREE.Color(0.6, 0.12, 0.02), a0: 1, a1: 0 });
      }
      this.spawn(this.fire, { x, y: y + 1, z, life: 0.18, s0: 2 * size, s1: 5.5 * size, c0: new THREE.Color(1, 1, 0.9), c1: new THREE.Color(1, 0.6, 0.2) });
    }
    // smoke column
    for (let i = 0; i < 16 * size; i++) {
      const a = R() * TAU, sp = R.range(0.5, 3.5) * size;
      const g = R.range(0.12, 0.26);
      this.spawn(this.smoke, { x: x + Math.cos(a), y: y + R.range(0.5, 2), z: z + Math.sin(a), vx: Math.cos(a) * sp, vy: R.range(2, 7) * size, vz: Math.sin(a) * sp, life: R.range(2.5, 5), s0: 1.0 * size, s1: R.range(3.5, 6) * size, drag: 1.4, c0: water ? new THREE.Color(0.85, 0.9, 0.92) : new THREE.Color(g, g * 0.95, g * 0.9), c1: water ? new THREE.Color(0.8, 0.86, 0.9) : new THREE.Color(0.42, 0.4, 0.37), a0: 0.9, a1: 0, g: -0.4 });
    }
    // dust ring
    for (let i = 0; i < 14 * size; i++) {
      const a = i / (14 * size) * TAU, sp = R.range(6, 11) * size;
      this.spawn(this.smoke, { x, y: y + 0.3, z, vx: Math.cos(a) * sp, vy: R.range(0.2, 1), vz: Math.sin(a) * sp, life: R.range(1.2, 2.2), s0: 0.8 * size, s1: 2.6 * size, drag: 2.5, c0: water ? new THREE.Color(0.9, 0.95, 1) : dirt.clone().multiplyScalar(1.25), a0: 0.75, a1: 0 });
    }
    // debris / water column
    for (let i = 0; i < (water ? 26 : 18) * size; i++) {
      const a = R() * TAU, sp = R.range(2, 9) * size;
      if (water) this.spawn(this.smoke, { x, y, z, vx: Math.cos(a) * sp * 0.4, vy: R.range(8, 18) * size, vz: Math.sin(a) * sp * 0.4, life: R.range(1, 1.8), s0: 0.5, s1: 1.4, g: 14, c0: new THREE.Color(0.92, 0.96, 1), a0: 0.9, a1: 0 });
      else this.spawn(this.chunks, { x, y: y + 0.5, z, vx: Math.cos(a) * sp, vy: R.range(5, 15) * size, vz: Math.sin(a) * sp, life: R.range(1.4, 2.6), s0: R.range(0.12, 0.4) * size, g: 18, c0: R() < 0.3 ? new THREE.Color(0.18, 0.17, 0.16) : dirt, a0: 1, a1: 1, spin: R.range(4, 12), floor: y - 0.5 });
    }
    const r = this.rings.find(r => r.t >= 1) || this.rings[0];
    r.t = 0; r.life = 0.45; r.size = 14 * size; r.m.position.set(x, y + 0.3, z); r.m.visible = !water;
    this.flash.position.set(x, y + 3, z); this.flash.intensity = 120 * size; this.flashT = 0.25;
  }
  dust(x, y, z, color, n = 3, size = 0.5, vy = 1) {
    for (let i = 0; i < n; i++) {
      const a = R() * TAU;
      this.spawn(this.smoke, { x: x + Math.cos(a) * 0.2, y, z: z + Math.sin(a) * 0.2, vx: Math.cos(a) * 1.5, vy: R.range(0.4, 1.8) * vy, vz: Math.sin(a) * 1.5, life: R.range(0.5, 1.1), s0: size * 0.4, s1: size * 1.6, drag: 2, c0: color, a0: 0.8, a1: 0 });
    }
  }
  spark(x, y, z, n = 4) {
    for (let i = 0; i < n; i++) {
      const a = R() * TAU;
      this.spawn(this.fire, { x, y, z, vx: Math.cos(a) * R.range(2, 6), vy: R.range(1, 5), vz: Math.sin(a) * R.range(2, 6), life: R.range(0.12, 0.3), s0: 0.08, s1: 0.02, g: 12, c0: new THREE.Color(1, 0.85, 0.45), c1: new THREE.Color(1, 0.4, 0.1) });
    }
  }
  smokePuff(x, y, z, dark = 0.2, size = 1, fire = false) {
    this.spawn(this.smoke, { x: x + R.range(-0.4, 0.4), y, z: z + R.range(-0.4, 0.4), vx: R.range(-0.4, 0.4) + 0.8, vy: R.range(1.5, 3), vz: R.range(-0.4, 0.4), life: R.range(3, 5), s0: 0.6 * size, s1: 3.2 * size, drag: 0.6, c0: new THREE.Color(dark, dark * 0.96, dark * 0.92), c1: new THREE.Color(0.45, 0.44, 0.42), a0: 0.75, a1: 0 });
    if (fire) this.spawn(this.fire, { x, y: y - 0.2, z, vx: R.range(-0.3, 0.3), vy: R.range(1, 2.5), vz: R.range(-0.3, 0.3), life: R.range(0.3, 0.6), s0: 0.5 * size, s1: 0.1, c0: new THREE.Color(1, 0.6, 0.2), c1: new THREE.Color(0.7, 0.15, 0.02) });
  }
  splash(x, y, z, n = 8) {
    for (let i = 0; i < n; i++) {
      const a = R() * TAU;
      this.spawn(this.smoke, { x, y, z, vx: Math.cos(a) * R.range(1, 3), vy: R.range(3, 6), vz: Math.sin(a) * R.range(1, 3), life: R.range(0.5, 0.9), s0: 0.25, s1: 0.8, g: 12, c0: new THREE.Color(0.9, 0.95, 1), a0: 0.85, a1: 0 });
    }
  }
  updatePool(pool, dt) {
    const { P, im, col } = pool;
    let n = 0;
    const m = this.m, q = this.q, e = this.e, v = this.v, s = this.s, c = new THREE.Color();
    for (let i = 0; i < P.length; i++) {
      const p = P[i]; if (!p.alive) continue;
      p.t += dt; if (p.t >= p.life) { p.alive = false; continue; }
      const k = p.t / p.life;
      const dr = Math.exp(-p.drag * dt);
      p.vx *= dr; p.vz *= dr; p.vy = p.vy * dr - p.g * dt;
      p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
      if (p.y < p.floor) { p.y = p.floor; p.vy *= -0.3; p.vx *= 0.5; p.vz *= 0.5; p.sp *= 0.5; }
      p.rx += p.sp * dt; p.rz += p.sp * 0.7 * dt;
      const sc = lerp(p.s0, p.s1, 1 - (1 - k) * (1 - k));
      e.set(p.rx, p.ry, p.rz); q.setFromEuler(e); v.set(p.x, p.y, p.z); s.set(sc * p.sx, sc * p.sy, sc);
      m.compose(v, q, s); im.setMatrixAt(n, m);
      c.copy(p.c0).lerp(p.c1, k);
      const a = lerp(p.a0, p.a1, k);
      col.setXYZW(n, c.r, c.g, c.b, a);
      n++;
    }
    im.count = n;
    im.instanceMatrix.needsUpdate = true; col.needsUpdate = true;
  }
  update(dt) {
    this.updatePool(this.smoke, dt); this.updatePool(this.fire, dt); this.updatePool(this.chunks, dt);
    for (const r of this.rings) {
      if (r.t >= 1) continue;
      r.t += dt / r.life; const k = Math.min(1, r.t);
      r.m.scale.setScalar(1 + r.size * k); r.m.material.opacity = (1 - k) * 0.55;
      if (r.t >= 1) r.m.visible = false;
    }
    if (this.flashT > 0) { this.flashT -= dt; this.flash.intensity *= Math.exp(-dt * 14); if (this.flashT <= 0) this.flash.intensity = 0; }
  }
}

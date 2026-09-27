// Particle & debris effects: dust / smoke sprites, sparks, rock chunks, glass.
import * as THREE from '../vendor/three.module.js';
import { makeSprite } from './textures.js';
import { terrainHeight, terrainNormal } from './terrain.js';
import { clamp } from './noise.js';

const G = 9.81;

class SpriteSystem {
  constructor(scene, { max = 2000, texture, additive = false, sizeMul = 1, color = 0xffffff, dark = false } = {}) {
    this.max = max; this.n = 0;
    this.pos = new Float32Array(max * 3); this.vel = new Float32Array(max * 3);
    this.life = new Float32Array(max); this.maxLife = new Float32Array(max); this.size = new Float32Array(max); this.grow = new Float32Array(max);
    this.col = new Float32Array(max * 3); this.alpha = new Float32Array(max); this.alpha0 = new Float32Array(max); this.rot = new Float32Array(max);
    this.drag = new Float32Array(max); this.grav = new Float32Array(max);
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('aSize', new THREE.BufferAttribute(this.size, 1).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('aColor', new THREE.BufferAttribute(this.col, 3).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('aAlpha', new THREE.BufferAttribute(this.alpha, 1).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('aRot', new THREE.BufferAttribute(this.rot, 1).setUsage(THREE.DynamicDrawUsage));
    geo.setDrawRange(0, 0);
    geo.boundingSphere = new THREE.Sphere(new THREE.Vector3(), 1e5);
    this.mat = new THREE.ShaderMaterial({
      uniforms: { map: { value: texture }, uScale: { value: 1 }, uSun: { value: new THREE.Vector3(0.4, 0.6, -0.7) }, uSunColor: { value: new THREE.Color(1, 0.9, 0.75) }, uAmbient: { value: new THREE.Color(0.55, 0.62, 0.75) }, uFogColor: { value: new THREE.Color(0.7, 0.78, 0.9) }, uFogDensity: { value: 0.0012 } },
      vertexShader: `
        attribute float aSize; attribute vec3 aColor; attribute float aAlpha; attribute float aRot;
        varying vec3 vColor; varying float vAlpha; varying float vRot; varying float vFog;
        uniform float uScale;
        void main(){
          vColor = aColor; vAlpha = aAlpha; vRot = aRot;
          vec4 mv = modelViewMatrix * vec4(position, 1.0);
          gl_PointSize = aSize * uScale / max(1.0, -mv.z);
          gl_Position = projectionMatrix * mv;
          float d = length(mv.xyz); vFog = 1.0 - exp(-d * ${'uFogDensity'} * 2.2);
        }`,
      fragmentShader: `
        uniform sampler2D map; uniform vec3 uFogColor; varying vec3 vColor; varying float vAlpha; varying float vRot; varying float vFog;
        void main(){
          vec2 c = gl_PointCoord - 0.5; float s = sin(vRot), co = cos(vRot);
          vec2 r = vec2(c.x * co - c.y * s, c.x * s + c.y * co) + 0.5;
          vec4 t = texture2D(map, r);
          float a = t.a * vAlpha; if (a < 0.003) discard;
          vec3 col = mix(vColor * t.rgb, uFogColor, vFog * ${additive ? '0.0' : '1.0'});
          gl_FragColor = vec4(col, a);
        }`,
      transparent: true, depthWrite: false, blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
    });
    this.mat.vertexShader = this.mat.vertexShader.replace('${\'uFogDensity\'}', 'uFogDensity');
    this.mat.vertexShader = 'uniform float uFogDensity;\n' + this.mat.vertexShader;
    this.points = new THREE.Points(geo, this.mat);
    this.points.frustumCulled = false;
    this.points.renderOrder = additive ? 3 : 2;
    scene.add(this.points);
    this.additive = additive;
  }
  emit(p, v, { life = 1.5, size = 2, grow = 1.5, color = [1, 1, 1], alpha = 0.5, drag = 1.5, grav = -0.5 } = {}) {
    let i;
    if (this.n < this.max) i = this.n++;
    else i = Math.floor(Math.random() * this.max); // recycle
    this.pos[i * 3] = p.x; this.pos[i * 3 + 1] = p.y; this.pos[i * 3 + 2] = p.z;
    this.vel[i * 3] = v.x; this.vel[i * 3 + 1] = v.y; this.vel[i * 3 + 2] = v.z;
    this.life[i] = life; this.maxLife[i] = life; this.size[i] = size; this.grow[i] = grow;
    this.col[i * 3] = color[0]; this.col[i * 3 + 1] = color[1]; this.col[i * 3 + 2] = color[2];
    this.alpha[i] = alpha; this.alpha0[i] = alpha; this.rot[i] = Math.random() * 6.28; this.drag[i] = drag; this.grav[i] = grav;
  }
  update(dt) {
    let n = this.n;
    for (let i = 0; i < n; i++) {
      this.life[i] -= dt;
      if (this.life[i] <= 0) { // swap-remove
        n--;
        if (i !== n) {
          for (let k = 0; k < 3; k++) { this.pos[i * 3 + k] = this.pos[n * 3 + k]; this.vel[i * 3 + k] = this.vel[n * 3 + k]; this.col[i * 3 + k] = this.col[n * 3 + k]; }
          this.life[i] = this.life[n]; this.maxLife[i] = this.maxLife[n]; this.size[i] = this.size[n]; this.grow[i] = this.grow[n];
          this.alpha[i] = this.alpha[n]; this.alpha0[i] = this.alpha0[n]; this.rot[i] = this.rot[n]; this.drag[i] = this.drag[n]; this.grav[i] = this.grav[n];
        }
        i--; continue;
      }
      const d = Math.max(0, 1 - this.drag[i] * dt);
      this.vel[i * 3] *= d; this.vel[i * 3 + 1] = this.vel[i * 3 + 1] * d + this.grav[i] * G * dt * 0.1; this.vel[i * 3 + 2] *= d;
      this.pos[i * 3] += this.vel[i * 3] * dt; this.pos[i * 3 + 1] += this.vel[i * 3 + 1] * dt; this.pos[i * 3 + 2] += this.vel[i * 3 + 2] * dt;
      const t = this.life[i] / this.maxLife[i];
      this.size[i] += this.grow[i] * dt;
      this.alpha[i] = this.alpha0[i] * (t < 0.7 ? t / 0.7 : 1) * (this.additive ? 1 : 1);
      this.rot[i] += dt * 0.3;
    }
    this.n = n;
    const g = this.points.geometry;
    g.setDrawRange(0, n);
    for (const a of ['position', 'aSize', 'aColor', 'aAlpha', 'aRot']) g.attributes[a].needsUpdate = true;
  }
}

class Debris {
  constructor(scene, geo, mat, max = 160) {
    this.mesh = new THREE.InstancedMesh(geo, mat, max);
    this.mesh.castShadow = true; this.mesh.frustumCulled = false;
    this.mesh.count = 0;
    this.max = max; this.items = [];
    scene.add(this.mesh);
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._s = new THREE.Vector3();
  }
  spawn(p, v, size, life = 8) {
    if (this.items.length >= this.max) this.items.shift();
    this.items.push({ p: p.clone(), v: v.clone(), r: new THREE.Euler(Math.random() * 6, Math.random() * 6, Math.random() * 6), w: new THREE.Vector3((Math.random() - 0.5) * 12, (Math.random() - 0.5) * 12, (Math.random() - 0.5) * 12), size, life, rest: false });
  }
  update(dt) {
    const it = this.items;
    for (let i = it.length - 1; i >= 0; i--) {
      const d = it[i];
      d.life -= dt; if (d.life <= 0) { it.splice(i, 1); continue; }
      if (!d.rest) {
        d.v.y -= G * dt; d.p.addScaledVector(d.v, dt);
        const h = terrainHeight(d.p.x, d.p.z) + d.size * 0.4;
        if (d.p.y < h) {
          d.p.y = h;
          const n = terrainNormal(d.p.x, d.p.z);
          const vn = d.v.dot(n);
          if (vn < 0) { d.v.addScaledVector(n, -vn * 1.35); d.v.multiplyScalar(0.7); }
          if (d.v.length() < 0.6 && n.y > 0.7) d.rest = true;
          d.w.multiplyScalar(0.6);
        }
        d.r.x += d.w.x * dt; d.r.y += d.w.y * dt; d.r.z += d.w.z * dt;
      }
      this._q.setFromEuler(d.r); this._s.setScalar(d.size);
      this._m.compose(d.p, this._q, this._s);
      this.mesh.setMatrixAt(i, this._m);
    }
    this.mesh.count = it.length;
    this.mesh.instanceMatrix.needsUpdate = true;
  }
}

export class FX {
  constructor(scene, rockTex) {
    const soft = makeSprite('soft', 128), hard = makeSprite('hard', 64);
    this.dust = new SpriteSystem(scene, { max: 3000, texture: soft });
    this.sparks = new SpriteSystem(scene, { max: 1200, texture: hard, additive: true });
    const rockGeo = new THREE.DodecahedronGeometry(1, 0);
    const rp = rockGeo.attributes.position; for (let i = 0; i < rp.count; i++) rp.setXYZ(i, rp.getX(i) * (0.7 + Math.random() * 0.5), rp.getY(i) * (0.7 + Math.random() * 0.5), rp.getZ(i) * (0.7 + Math.random() * 0.5));
    rockGeo.computeVertexNormals();
    this.rocks = new Debris(scene, rockGeo, new THREE.MeshStandardMaterial({ map: rockTex?.map, normalMap: rockTex?.normalMap, color: 0xb59c80, roughness: 0.95 }), 160);
    this.glass = new Debris(scene, new THREE.BoxGeometry(1, 0.05, 1), new THREE.MeshPhysicalMaterial({ color: 0x9fc3d8, roughness: 0.05, metalness: 0.3, transparent: true, opacity: 0.7 }), 120);
    this.metal = new Debris(scene, new THREE.BoxGeometry(1, 0.15, 0.6), new THREE.MeshStandardMaterial({ color: 0x444, roughness: 0.6, metalness: 0.8 }), 60);
    this.smokers = [];
  }
  setLighting(sun, fogColor, fogDensity) { for (const s of [this.dust, this.sparks]) { s.mat.uniforms.uFogColor.value.copy(fogColor); s.mat.uniforms.uFogDensity.value = fogDensity; } }
  setPixelScale(h) { const s = h * 0.9; this.dust.mat.uniforms.uScale.value = s; this.sparks.mat.uniforms.uScale.value = s; }

  /** Big impact against the cliff / ground. */
  crash(p, n, strength, vel) {
    const s = clamp(strength, 0.3, 1.5);
    const N = Math.floor(160 * s);
    const tmpV = new THREE.Vector3();
    for (let i = 0; i < N; i++) {
      tmpV.set((Math.random() - 0.5), Math.random() * 0.6, (Math.random() - 0.5)).normalize().multiplyScalar(4 + Math.random() * 14 * s).addScaledVector(n, 5 + Math.random() * 10);
      const shade = 0.55 + Math.random() * 0.3;
      this.dust.emit(p, tmpV, { life: 2.5 + Math.random() * 4 * s, size: 3 + Math.random() * 5, grow: 4 + Math.random() * 6, color: [0.74 * shade, 0.62 * shade, 0.48 * shade], alpha: 0.45, drag: 1.4, grav: -0.15 });
    }
    for (let i = 0; i < 120 * s; i++) {
      tmpV.set((Math.random() - 0.5), (Math.random() - 0.2), (Math.random() - 0.5)).normalize().multiplyScalar(8 + Math.random() * 30).addScaledVector(n, 8);
      this.sparks.emit(p, tmpV, { life: 0.3 + Math.random() * 0.8, size: 0.25 + Math.random() * 0.4, grow: -0.2, color: [1.0, 0.7 + Math.random() * 0.3, 0.3], alpha: 1, drag: 0.6, grav: -1.2 });
    }
    for (let i = 0; i < 26 * s; i++) {
      tmpV.set((Math.random() - 0.5), Math.random() * 0.5, (Math.random() - 0.5)).normalize().multiplyScalar(3 + Math.random() * 12).addScaledVector(n, 4 + Math.random() * 8);
      this.rocks.spawn(p, tmpV, 0.25 + Math.random() * Math.random() * 1.4, 12);
    }
    for (let i = 0; i < 40 * s; i++) {
      tmpV.set((Math.random() - 0.5), Math.random() * 0.8, (Math.random() - 0.5)).normalize().multiplyScalar(4 + Math.random() * 10).addScaledVector(vel || n, 0.3);
      this.glass.spawn(p, tmpV, 0.12 + Math.random() * 0.25, 6);
    }
    for (let i = 0; i < 10 * s; i++) {
      tmpV.set((Math.random() - 0.5), Math.random() * 0.8, (Math.random() - 0.5)).normalize().multiplyScalar(3 + Math.random() * 9).addScaledVector(n, 3);
      this.metal.spawn(p, tmpV, 0.3 + Math.random() * 0.5, 10);
    }
  }
  /** Continuous smoke from a wreck. */
  smoke(p, dt, intensity = 1) {
    this._acc = (this._acc || 0) + dt * 40 * intensity;
    const v = new THREE.Vector3();
    while (this._acc > 1) {
      this._acc--;
      v.set((Math.random() - 0.5) * 1.5, 2 + Math.random() * 2, (Math.random() - 0.5) * 1.5);
      const g = 0.12 + Math.random() * 0.1;
      this.dust.emit(p, v, { life: 3 + Math.random() * 3, size: 1.5 + Math.random() * 2, grow: 3, color: [g, g, g * 1.05], alpha: 0.35, drag: 0.6, grav: 0.25 });
    }
  }
  /** Tyre dust when the car slides. */
  tyreDust(p, vel, amount) {
    const v = new THREE.Vector3(vel.x * 0.2 + (Math.random() - 0.5) * 2, 1 + Math.random() * 1.5, vel.z * 0.2 + (Math.random() - 0.5) * 2);
    this.dust.emit(p, v, { life: 1 + Math.random() * 1.2, size: 1 + Math.random(), grow: 2.5, color: [0.72, 0.64, 0.52], alpha: 0.28 * amount, drag: 1.5, grav: -0.1 });
  }
  /** Wing-tip / downdraft dust near the ground. */
  downdraft(p, strength) {
    const v = new THREE.Vector3();
    for (let i = 0; i < 12 * strength; i++) {
      v.set((Math.random() - 0.5), 0.15, (Math.random() - 0.5)).normalize().multiplyScalar(6 + Math.random() * 8);
      this.dust.emit(p, v, { life: 1.5 + Math.random() * 1.5, size: 2 + Math.random() * 2, grow: 4, color: [0.74, 0.66, 0.52], alpha: 0.3, drag: 1.2, grav: -0.05 });
    }
  }
  update(dt) { this.dust.update(dt); this.sparks.update(dt); this.rocks.update(dt); this.glass.update(dt); this.metal.update(dt); }
}

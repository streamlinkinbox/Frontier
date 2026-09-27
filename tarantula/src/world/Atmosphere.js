import * as THREE from 'three';
import { SUN_DIR, SHAFT_TOP } from './Cave.js';
import { mulberry32 } from '../core/noise.js';

// Volumetric-looking sun shaft (soft cylinder with view-dependent density), dust motes that only
// glow inside the beam, and a particle system for urticating setae kicked off by the tarantula.

export class Atmosphere {
  constructor(scene, world) {
    this.scene = scene;
    this.uniforms = { uTime: { value: 0 }, uPixelWorld: { value: 0.001 } };
    // beam from shaft opening to floor
    const floorHit = world.raycast(SHAFT_TOP.clone(), SUN_DIR, 300);
    const end = floorHit ? floorHit.point.clone() : SHAFT_TOP.clone().addScaledVector(SUN_DIR, 120);
    this.beamStart = SHAFT_TOP.clone().addScaledVector(SUN_DIR, 30);
    this.beamEnd = end;
    this.floorSpot = end.clone();
    const len = this.beamStart.distanceTo(end);
    const geo = new THREE.CylinderGeometry(9.5, 12.5, len, 48, 24, true);
    geo.translate(0, -len / 2, 0);
    const mat = new THREE.ShaderMaterial({
      uniforms: { ...this.uniforms, uLen: { value: len }, uColor: { value: new THREE.Color(1.0, 0.86, 0.66).multiplyScalar(0.07) } },
      vertexShader: /* glsl */`
        varying vec3 vN; varying vec3 vV; varying float vY; varying vec3 vW;
        void main(){
          vec4 w = modelMatrix * vec4(position, 1.0);
          vW = w.xyz; vY = -position.y;
          vN = normalize(mat3(modelMatrix) * normal);
          vV = normalize(cameraPosition - w.xyz);
          gl_Position = projectionMatrix * viewMatrix * w;
        }`,
      fragmentShader: /* glsl */`
        uniform float uTime, uLen; uniform vec3 uColor;
        varying vec3 vN; varying vec3 vV; varying float vY; varying vec3 vW;
        float h3(vec3 p){ p = fract(p * .1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
        float vn(vec3 x){ vec3 i = floor(x), f = fract(x); f = f*f*(3.0-2.0*f);
          return mix(mix(mix(h3(i), h3(i+vec3(1,0,0)), f.x), mix(h3(i+vec3(0,1,0)), h3(i+vec3(1,1,0)), f.x), f.y),
                     mix(mix(h3(i+vec3(0,0,1)), h3(i+vec3(1,0,1)), f.x), mix(h3(i+vec3(0,1,1)), h3(i+vec3(1,1,1)), f.x), f.y), f.z); }
        void main(){
          float facing = abs(dot(normalize(vN), normalize(vV)));
          float core = pow(facing, 2.2);
          float t = vY / uLen;
          float fade = smoothstep(0.0, 0.18, t) * (1.0 - smoothstep(0.82, 1.0, t));
          float n = vn(vW * 0.08 + vec3(0.0, -uTime * 0.25, uTime * 0.1)) * 0.6 + vn(vW * 0.25 + vec3(uTime * 0.2, 0.0, 0.0)) * 0.4;
          float a = core * fade * (0.55 + 0.7 * n);
          gl_FragColor = vec4(uColor * a, 1.0);
        }`,
      transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide,
    });
    const beam = new THREE.Mesh(geo, mat);
    beam.position.copy(this.beamStart);
    beam.quaternion.setFromUnitVectors(new THREE.Vector3(0, -1, 0), SUN_DIR);
    beam.renderOrder = 10; beam.frustumCulled = false;
    this.beam = beam;
    scene.add(beam);

    this._dust();
    this._hairs();
  }

  _dust() {
    const N = 2500, rnd = mulberry32(5);
    const pos = new Float32Array(N * 3), seed = new Float32Array(N);
    const axis = new THREE.Vector3().subVectors(this.beamEnd, this.beamStart);
    const len = axis.length(); axis.normalize();
    const tmp = new THREE.Vector3(1, 0, 0).cross(axis).normalize(), tmp2 = new THREE.Vector3().crossVectors(axis, tmp);
    for (let i = 0; i < N; i++) {
      const t = rnd(), a = rnd() * Math.PI * 2, r = Math.sqrt(rnd()) * 20;
      const p = this.beamStart.clone().addScaledVector(axis, t * len).addScaledVector(tmp, Math.cos(a) * r).addScaledVector(tmp2, Math.sin(a) * r);
      pos.set([p.x, p.y, p.z], i * 3); seed[i] = rnd();
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('aSeed', new THREE.BufferAttribute(seed, 1));
    const mat = new THREE.ShaderMaterial({
      uniforms: { ...this.uniforms, uA: { value: this.beamStart }, uAxis: { value: axis }, uLen: { value: len } },
      vertexShader: /* glsl */`
        attribute float aSeed; uniform float uTime, uPixelWorld, uLen; uniform vec3 uA, uAxis;
        varying float vA;
        void main(){
          vec3 p = position;
          float s = aSeed * 6.2831;
          p += vec3(sin(uTime * 0.13 + s) * 1.6, sin(uTime * 0.09 + s * 1.7) * 1.2 - mod(uTime * 0.15 + aSeed * 7.0, 7.0) + 3.5, cos(uTime * 0.11 + s) * 1.6);
          vec3 d = p - uA; float t = dot(d, uAxis);
          float r = length(d - uAxis * t);
          float inBeam = (1.0 - smoothstep(4.0, 9.0 + t * 0.03, r)) * step(0.0, t) * (1.0 - smoothstep(uLen - 5.0, uLen, t));
          vec4 mv = viewMatrix * vec4(p, 1.0);
          float dist = -mv.z;
          float sizeW = 0.035 + 0.05 * fract(aSeed * 13.1);
          float px = sizeW / (uPixelWorld * dist);
          gl_PointSize = max(px, 1.5);
          vA = inBeam * pow(clamp(px / 1.5, 0.0, 1.0), 1.5) * exp(-dist * 0.012) * (0.35 + 0.65 * fract(aSeed * 91.7));
          gl_Position = projectionMatrix * mv;
        }`,
      fragmentShader: /* glsl */`
        varying float vA;
        void main(){ vec2 c = gl_PointCoord - 0.5; float d = length(c); float a = smoothstep(0.5, 0.1, d) * vA;
          if (a < 0.003) discard; gl_FragColor = vec4(vec3(1.0, 0.9, 0.75) * a * 0.9, 1.0); }`,
      transparent: true, depthWrite: false, blending: THREE.AdditiveBlending,
    });
    const pts = new THREE.Points(g, mat);
    pts.frustumCulled = false; pts.renderOrder = 11;
    this.scene.add(pts);
  }

  // urticating setae: fine barbed hairs, 0.3-1.2 mm, drifting on air currents
  _hairs() {
    const N = 3000;
    this.hair = { N, next: 0, pos: new Float32Array(N * 3), vel: new Float32Array(N * 3), life: new Float32Array(N), seed: new Float32Array(N) };
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(this.hair.pos, 3).setUsage(THREE.DynamicDrawUsage));
    g.setAttribute('aLife', new THREE.BufferAttribute(this.hair.life, 1).setUsage(THREE.DynamicDrawUsage));
    for (let i = 0; i < N; i++) this.hair.seed[i] = Math.random();
    g.setAttribute('aSeed', new THREE.BufferAttribute(this.hair.seed, 1));
    const mat = new THREE.ShaderMaterial({
      uniforms: { ...this.uniforms },
      vertexShader: /* glsl */`
        attribute float aLife; attribute float aSeed; uniform float uPixelWorld; varying float vA;
        void main(){
          vec4 mv = viewMatrix * vec4(position, 1.0);
          float px = 0.06 / (uPixelWorld * max(-mv.z, 0.1));
          gl_PointSize = aLife > 0.0 ? max(px, 1.25) : 0.0;
          vA = clamp(aLife, 0.0, 1.0) * clamp(px / 1.25, 0.2, 1.0) * (0.5 + 0.5 * aSeed);
          gl_Position = projectionMatrix * mv;
        }`,
      fragmentShader: /* glsl */`
        varying float vA;
        void main(){ vec2 c = gl_PointCoord - 0.5; float a = smoothstep(0.5, 0.0, length(c)) * vA; if (a < 0.004) discard;
          gl_FragColor = vec4(vec3(0.95, 0.72, 0.5) * a, a); }`,
      transparent: true, depthWrite: false, blending: THREE.NormalBlending,
    });
    this.hairPoints = new THREE.Points(g, mat);
    this.hairPoints.frustumCulled = false; this.hairPoints.renderOrder = 12;
    this.scene.add(this.hairPoints);
  }

  emitHairs(worldPos, backDir, upDir, count) {
    const h = this.hair;
    for (let k = 0; k < count; k++) {
      const i = h.next; h.next = (h.next + 1) % h.N;
      h.pos[i * 3] = worldPos.x + (Math.random() - 0.5) * 0.5;
      h.pos[i * 3 + 1] = worldPos.y + (Math.random() - 0.5) * 0.3;
      h.pos[i * 3 + 2] = worldPos.z + (Math.random() - 0.5) * 0.5;
      const sp = 6 + Math.random() * 14;
      const v = new THREE.Vector3().copy(backDir).multiplyScalar(sp).addScaledVector(upDir, 4 + Math.random() * 9)
        .add(new THREE.Vector3(Math.random() - 0.5, Math.random() - 0.5, Math.random() - 0.5).multiplyScalar(8));
      h.vel[i * 3] = v.x; h.vel[i * 3 + 1] = v.y; h.vel[i * 3 + 2] = v.z;
      h.life[i] = 3.5 + Math.random() * 3;
    }
  }

  update(dt, t) {
    this.uniforms.uTime.value = t;
    const h = this.hair;
    let any = false;
    for (let i = 0; i < h.N; i++) {
      if (h.life[i] <= 0) continue;
      any = true;
      h.life[i] -= dt;
      // very high drag (tiny setae) + slow settling + turbulence
      const drag = Math.exp(-dt * 4.5);
      h.vel[i * 3] = h.vel[i * 3] * drag + Math.sin(t * 1.3 + i) * 0.8 * dt;
      h.vel[i * 3 + 1] = h.vel[i * 3 + 1] * drag - 1.2 * dt;
      h.vel[i * 3 + 2] = h.vel[i * 3 + 2] * drag + Math.cos(t * 1.1 + i * 0.7) * 0.8 * dt;
      h.pos[i * 3] += h.vel[i * 3] * dt; h.pos[i * 3 + 1] += h.vel[i * 3 + 1] * dt; h.pos[i * 3 + 2] += h.vel[i * 3 + 2] * dt;
    }
    if (any || this._wasAny) {
      this.hairPoints.geometry.attributes.position.needsUpdate = true;
      this.hairPoints.geometry.attributes.aLife.needsUpdate = true;
    }
    this._wasAny = any;
  }
}

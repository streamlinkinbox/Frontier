// Vehicle = rigid body + 4 "hover" ray suspension points + simplified Pacejka
// tyres. Also: 8 body contact points against the height field (so the eagle
// can slam it into the cliff), crumple deformation and detachable wheels.
import * as THREE from '../vendor/three.module.js';
import { terrainHeight, terrainNormal, raycastTerrain, roadDir, roadY, roadZ } from './terrain.js';
import { clamp, lerp } from './noise.js';

const G = 9.81;
const _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3(), _n = new THREE.Vector3(), _t1 = new THREE.Vector3(), _t2 = new THREE.Vector3();
const _q = new THREE.Quaternion(), _m = new THREE.Matrix3();

/** Magic formula. x = slip (ratio or angle in rad). */
export function pacejka(x, B, C, D, E) {
  const Bx = B * x;
  return D * Math.sin(C * Math.atan(Bx - E * (Bx - Math.atan(Bx))));
}

export const CAR_SPEC = {
  mass: 1450,
  size: new THREE.Vector3(1.92, 1.25, 4.55),  // w, h, l
  comOffset: new THREE.Vector3(0, -0.1, 0.05),
  wheelBase: 2.72, track: 1.62,
  wheelRadius: 0.35, wheelWidth: 0.26,
  restLength: 0.35, maxTravel: 0.2,
  springK: 32000, damperC: 3200, antiRoll: 9000,
  maxSteer: 0.62,
  maxDrive: 8200, power: 330000, maxBrake: 12500, handBrake: 9000,
  topSpeed: 72,
  cdA: 0.72, downforce: 0.9,
  tyre: {
    // lateral: slip angle in radians. Peak near ~8°.
    latB: 12.0, latC: 1.4, latE: -1.0,
    // longitudinal: slip ratio. Peak near ~0.12
    lonB: 16.0, lonC: 1.65, lonE: 0.5,
    mu: 1.15,
  },
};

export class CarPhysics {
  constructor(spec = CAR_SPEC) {
    this.spec = spec;
    this.pos = new THREE.Vector3();
    this.quat = new THREE.Quaternion();
    this.vel = new THREE.Vector3();
    this.angVel = new THREE.Vector3();
    this.force = new THREE.Vector3();
    this.torque = new THREE.Vector3();
    const s = spec.size, m = spec.mass;
    this.invMass = 1 / m;
    // box inertia (slightly lowered to make it lively)
    this.invInertiaLocal = new THREE.Vector3(
      1 / (m / 12 * (s.y * s.y + s.z * s.z)),
      1 / (m / 12 * (s.x * s.x + s.z * s.z)),
      1 / (m / 12 * (s.x * s.x + s.y * s.y)));
    const hw = spec.track / 2, hb = spec.wheelBase / 2, y = 0; // suspension ray origins at COM height
    this.wheels = [
      { local: new THREE.Vector3(-hw, y, hb), front: true, omega: 0, compression: 0, contact: false, load: 0, steer: 0, slipA: 0, slipR: 0, world: new THREE.Vector3(), normal: new THREE.Vector3(0, 1, 0), spin: 0, side: -1 },
      { local: new THREE.Vector3(hw, y, hb), front: true, omega: 0, compression: 0, contact: false, load: 0, steer: 0, slipA: 0, slipR: 0, world: new THREE.Vector3(), normal: new THREE.Vector3(0, 1, 0), spin: 0, side: 1 },
      { local: new THREE.Vector3(-hw, y, -hb), front: false, omega: 0, compression: 0, contact: false, load: 0, steer: 0, slipA: 0, slipR: 0, world: new THREE.Vector3(), normal: new THREE.Vector3(0, 1, 0), spin: 0, side: -1 },
      { local: new THREE.Vector3(hw, y, -hb), front: false, omega: 0, compression: 0, contact: false, load: 0, steer: 0, slipA: 0, slipR: 0, world: new THREE.Vector3(), normal: new THREE.Vector3(0, 1, 0), spin: 0, side: 1 },
    ];
    // body contact points (8 corners + 4 side mids), relative to COM
    this.contacts = [];
    const c = spec.comOffset;
    for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) this.contacts.push(new THREE.Vector3(sx * s.x * 0.48, sy * s.y * 0.48 - c.y, sz * s.z * 0.48 - c.z));
    this.contacts.push(new THREE.Vector3(0, 0.3, s.z * 0.5 - c.z), new THREE.Vector3(0, 0.3, -s.z * 0.5 - c.z));
    this.input = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
    this.steerAngle = 0;
    this.onImpact = null;
    this.grounded = false;
    this.speed = 0;
    this.airTime = 0;
    this.kinematic = false; // when carried by the eagle
    this.slipEnergy = 0;
  }

  // helpers
  worldPoint(local, out) { return out.copy(local).applyQuaternion(this.quat).add(this.pos); }
  pointVelocity(worldP, out) { return out.copy(this.angVel).cross(_t1.copy(worldP).sub(this.pos)).add(this.vel); }
  applyForce(f, at) {
    this.force.add(f);
    _t2.copy(at).sub(this.pos);
    this.torque.add(_t2.cross(f));
  }
  applyImpulse(j, at) {
    this.vel.addScaledVector(j, this.invMass);
    _t2.copy(at).sub(this.pos).cross(j);
    this.angVel.add(this.worldInvInertia(_t2));
  }
  worldInvInertia(tLocalOut) {
    // I⁻¹_world · t = R · I⁻¹_local · Rᵀ · t
    _q.copy(this.quat).invert();
    tLocalOut.applyQuaternion(_q);
    tLocalOut.multiply(this.invInertiaLocal);
    tLocalOut.applyQuaternion(this.quat);
    return tLocalOut;
  }
  axes(out = {}) {
    out.right = (out.right || new THREE.Vector3()).set(1, 0, 0).applyQuaternion(this.quat);
    out.up = (out.up || new THREE.Vector3()).set(0, 1, 0).applyQuaternion(this.quat);
    out.fwd = (out.fwd || new THREE.Vector3()).set(0, 0, 1).applyQuaternion(this.quat);
    return out;
  }

  place(x, z, yaw) {
    this.pos.set(x, terrainHeight(x, z) + 0.62, z);
    this.quat.setFromAxisAngle(new THREE.Vector3(0, 1, 0), yaw);
    this.vel.set(0, 0, 0); this.angVel.set(0, 0, 0);
    for (const w of this.wheels) { w.omega = 0; w.compression = 0; }
  }

  step(dt) {
    if (this.kinematic) return;
    const sp = this.spec, t = sp.tyre;
    this.force.set(0, 0, 0); this.torque.set(0, 0, 0);
    const ax = this.axes(this._ax || (this._ax = {}));
    const { right, up, fwd } = ax;
    this.force.y -= sp.mass * G;

    // steering: smoothed, reduced at speed
    const speedFwd = this.vel.dot(fwd);
    this.speed = this.vel.length();
    const steerLimit = sp.maxSteer * (1 - 0.6 * clamp(Math.abs(speedFwd) / 55, 0, 1));
    const targetSteer = this.input.steer * steerLimit;
    this.steerAngle += (targetSteer - this.steerAngle) * clamp(dt * 9, 0, 1);

    // engine
    const throttle = this.input.throttle;
    const reversing = throttle < 0 && speedFwd < 1.0;
    let driveForce = 0;
    if (throttle > 0) driveForce = Math.min(sp.maxDrive, sp.power / Math.max(3, Math.abs(speedFwd))) * throttle * clamp(1 - speedFwd / sp.topSpeed, 0, 1);
    else if (reversing) driveForce = -sp.maxDrive * 0.5 * -throttle * clamp(1 + speedFwd / 18, 0, 1);
    let brakeForce = this.input.brake * sp.maxBrake + (throttle < 0 && !reversing ? sp.maxBrake * 0.8 * -throttle : 0);

    let groundedCount = 0;
    const loads = [0, 0, 0, 0];
    // --- suspension pass (compute loads first so anti-roll & tyres can use them)
    for (let i = 0; i < 4; i++) {
      const w = this.wheels[i];
      this.worldPoint(w.local, w.world);
      _v.copy(up).negate();
      const maxLen = sp.restLength + sp.wheelRadius;
      const d = raycastTerrain(w.world, _v, maxLen + 0.3, 6);
      w.contact = d >= 0 && d < maxLen;
      w.prevCompression = w.compression;
      if (w.contact) {
        groundedCount++;
        terrainNormal(w.world.x + _v.x * d, w.world.z + _v.z * d, w.normal, 0.4);
        const comp = clamp(maxLen - d, 0, sp.restLength + 0.2);
        w.compression = comp;
        const relVel = -this.pointVelocity(w.world, _v2).dot(w.normal);
        let f = sp.springK * comp + sp.damperC * relVel;
        // bump stop
        if (comp > sp.maxTravel) f += (comp - sp.maxTravel) * sp.springK * 8;
        f = Math.max(0, f);
        loads[i] = f;
        w.contactPoint = (w.contactPoint || new THREE.Vector3()).copy(w.world).addScaledVector(_v, d);
      } else { w.compression = Math.max(0, w.compression - dt * 2); loads[i] = 0; }
    }
    // anti-roll bars
    for (const pair of [[0, 1], [2, 3]]) {
      const a = this.wheels[pair[0]], b = this.wheels[pair[1]];
      const diff = a.compression - b.compression;
      if (a.contact) loads[pair[0]] = Math.max(0, loads[pair[0]] - diff * sp.antiRoll);
      if (b.contact) loads[pair[1]] = Math.max(0, loads[pair[1]] + diff * sp.antiRoll);
    }
    this.grounded = groundedCount > 0;
    this.airTime = this.grounded ? 0 : this.airTime + dt;

    // --- tyre pass
    const hb = this.input.handbrake;
    for (let i = 0; i < 4; i++) {
      const w = this.wheels[i];
      w.load = loads[i];
      w.steer = w.front ? this.steerAngle : 0;
      if (!w.contact || w.load <= 0) {
        // free spinning wheel in the air
        const tDrive = w.front ? driveForce * 0.175 : driveForce * 0.325; // AWD 35/65 split over 4 wheels
        w.omega += ((tDrive * sp.wheelRadius) / 1.4 - w.omega * 0.6) * dt;
        w.slipA = 0; w.slipR = 0;
        w.spin += w.omega * dt;
        continue;
      }
      const n = w.normal;
      // suspension force along the contact normal
      _v.copy(n).multiplyScalar(w.load);
      this.applyForce(_v, w.contactPoint);

      // wheel frame projected on the ground plane
      const cs = Math.cos(w.steer), sn = Math.sin(w.steer);
      const wf = _v2.copy(fwd).multiplyScalar(cs).addScaledVector(right, sn); // steered forward
      wf.addScaledVector(n, -wf.dot(n)).normalize();
      const ws = _v3.crossVectors(n, wf).normalize(); // points to the right? n×f: (0,1,0)×(0,0,1) = (1,0,0) yes right
      const pv = this.pointVelocity(w.contactPoint, this._pv || (this._pv = new THREE.Vector3()));
      const vx = pv.dot(wf), vy = pv.dot(ws);

      // longitudinal: drive / brake torques and slip ratio
      const drive = w.front ? driveForce * 0.175 : driveForce * 0.325;
      let brake = brakeForce * (w.front ? 0.62 : 0.38) + (!w.front ? hb * sp.handBrake : 0);
      const mu = t.mu * (!w.front && hb > 0.5 ? 0.75 : 1);
      const Fmax = mu * w.load;
      let Fx, Fy;
      const absVx = Math.abs(vx);
      if (absVx < 3.0) {
        // low-speed regime: quasi-static (avoids the slip-ratio singularity)
        const brakeF = Math.min(brake, Fmax) * -Math.sign(vx || 1) * clamp(absVx / 0.4, 0, 1);
        Fx = clamp(drive + brakeF, -Fmax, Fmax);
        Fy = clamp(-vy * w.load * 1.8, -Fmax, Fmax);
        w.omega = vx / sp.wheelRadius + drive / 2000;
        w.slipR = 0; w.slipA = 0;
      } else {
        // wheel spin dynamics (semi-implicit)
        const Iw = 1.4;
        const rollingOmega = vx / sp.wheelRadius;
        const tq = drive * sp.wheelRadius - Math.min(brake, Math.abs(w.omega) * Iw / dt) * Math.sign(w.omega || 1) * sp.wheelRadius;
        w.omega += (tq / Iw) * dt;
        let k = (w.omega * sp.wheelRadius - vx) / Math.max(absVx, 3);
        k = clamp(k, -1, 1);
        const alpha = Math.atan2(vy, absVx);
        w.slipR = k; w.slipA = alpha;
        const Fx0 = pacejka(k, t.lonB, t.lonC, Fmax, t.lonE);
        const Fy0 = -pacejka(alpha, t.latB, t.latC, Fmax, t.latE);
        // friction ellipse combination
        const rx = Fx0 / Fmax, ry = Fy0 / Fmax;
        const len = Math.hypot(rx, ry);
        const s = len > 1 ? 1 / len : 1;
        Fx = Fx0 * s; Fy = Fy0 * s;
        // tyre reaction on the wheel (keeps slip ratio bounded) with relaxation
        w.omega -= (Fx * sp.wheelRadius / Iw) * dt;
        // clamp omega toward rolling when close (numerical hygiene)
        if (Math.abs(w.omega - rollingOmega) < 0.5 && Math.abs(k) < 0.02) w.omega = lerp(w.omega, rollingOmega, 0.5);
        this.slipEnergy += (Math.abs(alpha) * 4 + Math.abs(k)) * w.load * 1e-5;
      }
      w.spin += w.omega * dt;
      _v.copy(wf).multiplyScalar(Fx).addScaledVector(ws, Fy);
      this.applyForce(_v, w.contactPoint);
      // rolling resistance
      _v.copy(wf).multiplyScalar(-vx * 12);
      this.applyForce(_v, w.contactPoint);
    }

    // aero drag + downforce
    const v2 = this.vel.lengthSq();
    if (v2 > 0.01) {
      _v.copy(this.vel).multiplyScalar(-0.5 * 1.2 * sp.cdA * Math.sqrt(v2));
      this.force.add(_v);
      this.force.addScaledVector(up, -sp.downforce * v2);
    }
    // body contacts vs terrain
    this.bodyContacts(dt);

    // angular damping (aero stability)
    this.angVel.multiplyScalar(1 - dt * 0.6);
    if (!this.grounded) { // gentle self-righting torque in the air toward terrain-up
      _v.crossVectors(up, new THREE.Vector3(0, 1, 0)).multiplyScalar(2500);
      this.torque.add(_v);
    }

    // integrate
    this.vel.addScaledVector(this.force, this.invMass * dt);
    this.angVel.add(this.worldInvInertia(_v.copy(this.torque)).multiplyScalar(dt));
    this.pos.addScaledVector(this.vel, dt);
    _q.set(this.angVel.x * dt * 0.5, this.angVel.y * dt * 0.5, this.angVel.z * dt * 0.5, 0);
    _q.multiply(this.quat);
    this.quat.x += _q.x; this.quat.y += _q.y; this.quat.z += _q.z; this.quat.w += _q.w;
    this.quat.normalize();
  }

  bodyContacts(dt) {
    let impact = 0, impactPoint = null, impactNormal = null;
    for (const c of this.contacts) {
      this.worldPoint(c, _v);
      const h = terrainHeight(_v.x, _v.z);
      const pen = h - _v.y;
      if (pen > 0) {
        terrainNormal(_v.x, _v.z, _n, 0.5);
        const pv = this.pointVelocity(_v, _v2);
        const vn = pv.dot(_n);
        // penalty + damping along the normal
        let f = pen * 180000 + (vn < 0 ? -vn * 12000 : 0);
        f = Math.min(f, 400000);
        _v3.copy(_n).multiplyScalar(f);
        // friction (tangential)
        const vt = _v2.addScaledVector(_n, -vn);
        _v3.addScaledVector(vt, -Math.min(f * 0.7, 20000) / Math.max(vt.length(), 0.5));
        this.applyForce(_v3, _v);
        if (-vn > impact) { impact = -vn; impactPoint = _v.clone(); impactNormal = _n.clone(); }
      }
    }
    if (impact > 5 && this.onImpact) this.onImpact(impact, impactPoint, impactNormal);
    this.lastImpact = impact;
  }
}

/* ============================================================================
 * CAR MESH — a lofted coupe hull (paint + glass), wheels, lights.
 * ========================================================================== */
const CAR_COLORS = [0xb8121b, 0x1f5eff, 0xe8e8ea, 0x1a1a1c, 0xd08a12, 0x2f7a4f, 0x8a8f96];

function loftCar(spec) {
  // longitudinal stations: z from front (+) to rear (-). each: [z, floorY, beltY, roofY, halfW, glassHalfW, glass?]
  const L = spec.size.z, W = spec.size.x;
  const st = [
    [ L * 0.50, 0.32, 0.42, 0.44, W * 0.34, W * 0.30, false],
    [ L * 0.48, 0.16, 0.58, 0.60, W * 0.44, W * 0.40, false],
    [ L * 0.40, 0.12, 0.66, 0.68, W * 0.48, W * 0.44, false],
    [ L * 0.20, 0.12, 0.74, 0.77, W * 0.50, W * 0.46, false],  // hood end
    [ L * 0.12, 0.12, 0.78, 0.94, W * 0.50, W * 0.45, true],   // windshield base
    [-L * 0.02, 0.12, 0.80, 1.25, W * 0.50, W * 0.40, true],   // roof front
    [-L * 0.16, 0.12, 0.80, 1.26, W * 0.50, W * 0.40, true],   // roof rear
    [-L * 0.30, 0.13, 0.80, 1.06, W * 0.50, W * 0.42, true],   // rear glass
    [-L * 0.40, 0.14, 0.84, 0.86, W * 0.49, W * 0.45, false],  // deck
    [-L * 0.48, 0.20, 0.82, 0.84, W * 0.45, W * 0.41, false],
    [-L * 0.50, 0.34, 0.60, 0.62, W * 0.36, W * 0.33, false],
  ];
  // cross-section (right half) param u in [0,1]: floor centre → sill → belt → roof centre
  const ring = (s) => {
    const [z, fy, by, ry, hw, gw] = s;
    const pts = [];
    const R = 0.1;
    pts.push([0, fy]); pts.push([hw - 0.3, fy]); pts.push([hw - 0.05, fy + 0.06]);
    pts.push([hw, fy + 0.25]); pts.push([hw, by - 0.12]); pts.push([hw - 0.03, by]);
    // greenhouse (tumblehome)
    pts.push([gw, by + 0.03 + (ry - by) * 0.08]); pts.push([gw - (gw - 0.0) * 0.18, by + (ry - by) * 0.7]); pts.push([gw * 0.55, ry - 0.01]); pts.push([0, ry]);
    return pts;
  };
  const rings = st.map(ring);
  const nR = rings[0].length;
  const verts = [], nrm = [], uv = [], idxPaint = [], idxGlass = [];
  // full ring = right half + mirrored left half
  const full = (i) => {
    const r = rings[i], out = [];
    for (let k = 0; k < nR; k++) out.push([r[k][0], r[k][1]]);
    for (let k = nR - 2; k >= 1; k--) out.push([-r[k][0], r[k][1]]);
    return out;
  };
  const fulls = st.map((_, i) => full(i));
  const M = fulls[0].length;
  for (let i = 0; i < st.length; i++) {
    for (let k = 0; k < M; k++) {
      verts.push(fulls[i][k][0], fulls[i][k][1], st[i][0]);
      uv.push(k / (M - 1), i / (st.length - 1));
    }
  }
  const isGlassSeg = (i, k) => {
    const kk = k < nR ? k : (2 * nR - 2 - k);
    return st[i][6] && st[i + 1] && st[i + 1][6] && kk >= 6;
  };
  for (let i = 0; i < st.length - 1; i++) for (let k = 0; k < M - 1; k++) {
    const a = i * M + k, b = a + 1, c = a + M, d = c + 1;
    const glass = isGlassSeg(i, k) || (st[i][6] && kk(k) >= 6 && i === 3) || (st[i + 1][6] && kk(k) >= 6 && i === 3);
    const arr = glass ? idxGlass : idxPaint;
    arr.push(a, b, c, b, d, c);
  }
  function kk(k) { return k < nR ? k : (2 * nR - 2 - k); }
  // caps
  const capFront = verts.length / 3; verts.push(0, 0.5, L * 0.5 + 0.02); uv.push(0.5, 0);
  const capRear = verts.length / 3; verts.push(0, 0.48, -L * 0.5 - 0.02); uv.push(0.5, 1);
  for (let k = 0; k < M - 1; k++) { idxPaint.push(capFront, k + 1, k); const o = (st.length - 1) * M; idxPaint.push(capRear, o + k, o + k + 1); }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  geo.setIndex([...idxPaint, ...idxGlass]);
  geo.addGroup(0, idxPaint.length, 0);
  geo.addGroup(idxPaint.length, idxGlass.length, 1);
  geo.computeVertexNormals();
  return geo;
}

function wheelMesh(spec) {
  const g = new THREE.Group();
  const r = spec.wheelRadius, w = spec.wheelWidth;
  const tyre = new THREE.Mesh(new THREE.CylinderGeometry(r, r, w, 28, 1), new THREE.MeshStandardMaterial({ color: 0x151515, roughness: 0.92 }));
  tyre.rotation.z = Math.PI / 2;
  const rim = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.66, r * 0.66, w * 1.02, 24, 1), new THREE.MeshStandardMaterial({ color: 0xbfc3c8, roughness: 0.35, metalness: 0.9 }));
  rim.rotation.z = Math.PI / 2;
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.2, r * 0.2, w * 1.06, 12), new THREE.MeshStandardMaterial({ color: 0x333, roughness: 0.5, metalness: 0.8 }));
  hub.rotation.z = Math.PI / 2;
  // spokes gaps (dark) to read rotation
  for (let i = 0; i < 5; i++) {
    const gap = new THREE.Mesh(new THREE.BoxGeometry(w * 1.08, r * 0.36, r * 0.16), new THREE.MeshStandardMaterial({ color: 0x1a1a1a, roughness: 0.8 }));
    const a = (i / 5) * Math.PI * 2;
    gap.position.set(0, Math.cos(a) * r * 0.42, Math.sin(a) * r * 0.42);
    gap.rotation.x = -a;
    g.add(gap);
  }
  g.add(tyre, rim, hub);
  g.traverse(o => { if (o.isMesh) o.castShadow = true; });
  return g;
}

export function buildCarMesh(spec = CAR_SPEC, color = CAR_COLORS[Math.floor(Math.random() * CAR_COLORS.length)]) {
  const root = new THREE.Group();
  const hull = loftCar(spec);
  const paint = new THREE.MeshPhysicalMaterial({ color, roughness: 0.32, metalness: 0.75, clearcoat: 1, clearcoatRoughness: 0.08, envMapIntensity: 1.2 });
  const glass = new THREE.MeshPhysicalMaterial({ color: 0x0c1218, roughness: 0.05, metalness: 0.9, clearcoat: 1, envMapIntensity: 1.5 });
  const body = new THREE.Mesh(hull, [paint, glass]);
  body.castShadow = true; body.receiveShadow = true;
  body.userData.basePos = hull.attributes.position.array.slice();
  root.add(body);

  // underbody / bumpers / lights
  const dark = new THREE.MeshStandardMaterial({ color: 0x141416, roughness: 0.8 });
  const floor = new THREE.Mesh(new THREE.BoxGeometry(spec.size.x * 0.9, 0.12, spec.size.z * 0.92), dark);
  floor.position.y = 0.16; root.add(floor);
  const lightMat = new THREE.MeshStandardMaterial({ color: 0xfff6dc, emissive: 0xfff2c0, emissiveIntensity: 1.6, roughness: 0.2 });
  const tailMat = new THREE.MeshStandardMaterial({ color: 0x8a0d10, emissive: 0xff2020, emissiveIntensity: 0.9, roughness: 0.3 });
  for (const s of [-1, 1]) {
    const hl = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.13, 0.08), lightMat);
    hl.position.set(s * spec.size.x * 0.3, 0.55, spec.size.z * 0.485); root.add(hl);
    const tl = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.1, 0.06), tailMat);
    tl.position.set(s * spec.size.x * 0.28, 0.74, -spec.size.z * 0.492); root.add(tl);
    const mirror = new THREE.Mesh(new THREE.BoxGeometry(0.1, 0.09, 0.2), paint);
    mirror.position.set(s * (spec.size.x * 0.5 + 0.06), 0.86, spec.size.z * 0.1); root.add(mirror);
  }
  const grille = new THREE.Mesh(new THREE.BoxGeometry(spec.size.x * 0.5, 0.16, 0.06), dark);
  grille.position.set(0, 0.36, spec.size.z * 0.495); root.add(grille);
  // plate for cabin interior darkness (seen through glass)
  const cabin = new THREE.Mesh(new THREE.BoxGeometry(spec.size.x * 0.76, 0.42, 1.9), new THREE.MeshStandardMaterial({ color: 0x1b1a1c, roughness: 1 }));
  cabin.position.set(0, 0.88, -0.3); root.add(cabin);

  const wheels = [];
  const hw = spec.track / 2, hb = spec.wheelBase / 2;
  const wp = [[-hw, hb], [hw, hb], [-hw, -hb], [hw, -hb]];
  for (let i = 0; i < 4; i++) {
    const pivot = new THREE.Group(); // steering pivot
    pivot.position.set(wp[i][0], spec.wheelRadius, wp[i][1]);
    const wm = wheelMesh(spec);
    pivot.add(wm);
    root.add(pivot);
    wheels.push({ pivot, mesh: wm });
  }
  root.userData = { body, wheels, paint, glass, spec };
  return root;
}

/* ============================================================================
 * CAR — glue between physics and mesh, with AI / player / grabbed / wreck modes.
 * ========================================================================== */
let carId = 0;
export class Car {
  constructor(scene, { color, isPlayer = false } = {}) {
    this.id = ++carId;
    this.isPlayer = isPlayer;
    this.phys = new CarPhysics();
    this.mesh = buildCarMesh(CAR_SPEC, color);
    this.scene = scene;
    scene.add(this.mesh);
    this.mode = isPlayer ? 'player' : 'ai';
    this.ai = { dir: 1, lane: 1, speed: 22, x: 0 };
    this.damage = 0;
    this.wreckTime = 0;
    this.attackable = true;
    this.detached = [];
    this.smokeTimer = 0;
    this.visualOffset = new THREE.Vector3(0, -(CAR_SPEC.restLength - 0.11 + CAR_SPEC.wheelRadius), 0); // mesh origin (ground plane at rest) relative to COM
    this.phys.onImpact = (v, p, n) => this.impact(v, p, n);
    this.impactCooldown = 0;
    this.onCrash = null;
  }

  spawnPlayer(x, dir = 1) {
    this.mode = 'player';
    this.phys.kinematic = false;
    const z = roadZ(x) - 2.6 * dir;
    const d = roadDir(x);
    this.phys.place(x, z, Math.atan2(d.x * dir, d.z * dir));
    this.resetDamage();
  }

  spawnAI(x, dir) {
    this.mode = 'ai';
    this.phys.kinematic = true;
    this.ai.dir = dir; this.ai.x = x; this.ai.speed = 19 + Math.random() * 9;
    this.resetDamage();
    this.updateAIPose(0);
  }

  resetDamage() {
    this.damage = 0; this.wreckTime = 0; this.attackable = true;
    const body = this.mesh.userData.body;
    body.geometry.attributes.position.array.set(body.userData.basePos);
    body.geometry.attributes.position.needsUpdate = true;
    body.geometry.computeVertexNormals();
    for (const w of this.mesh.userData.wheels) { w.pivot.visible = true; }
    for (const d of this.detached) this.scene.remove(d.mesh);
    this.detached.length = 0;
    this.mesh.visible = true;
  }

  updateAIPose(dt) {
    const a = this.ai;
    a.x += a.dir * a.speed * dt;
    const laneOff = -2.6 * a.dir; // drive on the right
    const d = roadDir(a.x);
    const side = new THREE.Vector3(0, 1, 0).cross(d).normalize().negate();
    this.phys.pos.set(a.x + side.x * laneOff, roadY(a.x) + 0.05 - this.visualOffset.y, roadZ(a.x) + side.z * laneOff);
    const yaw = Math.atan2(d.x * a.dir, d.z * a.dir);
    const pitch = -Math.atan2(d.y, Math.hypot(d.x, d.z)) * a.dir;
    this.phys.quat.setFromEuler(new THREE.Euler(pitch, yaw, 0, 'YXZ'));
    this.phys.vel.copy(d).multiplyScalar(a.speed * a.dir);
    for (const w of this.phys.wheels) { w.omega = a.speed / CAR_SPEC.wheelRadius; w.spin += w.omega * dt; }
    this.phys.steerAngle = 0;
  }

  impact(v, p, n) {
    if (this.impactCooldown > 0) return;
    if (v < 6) return;
    this.impactCooldown = 0.25;
    const amount = clamp((v - 5) / 22, 0, 1);
    this.damage = Math.min(1.5, this.damage + amount);
    this.crumple(p, n, amount);
    if (v > 11) this.detachWheels(Math.min(4, Math.floor(amount * 4)));
    if (this.onCrash) this.onCrash(this, v, p, n);
    if (this.damage > 0.9 && this.mode !== 'wreck') { this.mode = 'wreck'; this.wreckTime = 0; this.attackable = false; }
  }

  crumple(worldP, n, amount) {
    const body = this.mesh.userData.body;
    const pos = body.geometry.attributes.position;
    const local = body.worldToLocal(worldP.clone());
    const ln = n.clone().transformDirection(new THREE.Matrix4().copy(body.matrixWorld).invert());
    const R = 1.4 + amount * 1.2;
    const v = new THREE.Vector3();
    for (let i = 0; i < pos.count; i++) {
      v.fromBufferAttribute(pos, i);
      const dist = v.distanceTo(local);
      if (dist > R) continue;
      const f = (1 - dist / R); const push = f * f * amount * 0.7;
      v.addScaledVector(ln, push).add(new THREE.Vector3((Math.random() - 0.5), (Math.random() - 0.5), (Math.random() - 0.5)).multiplyScalar(push * 0.35));
      pos.setXYZ(i, v.x, v.y, v.z);
    }
    pos.needsUpdate = true;
    body.geometry.computeVertexNormals();
  }

  detachWheels(count) {
    const ws = this.mesh.userData.wheels;
    for (let i = 0; i < 4 && count > 0; i++) {
      if (!ws[i].pivot.visible) continue;
      ws[i].pivot.visible = false; count--;
      const m = wheelMesh(CAR_SPEC);
      ws[i].pivot.getWorldPosition(m.position);
      ws[i].pivot.getWorldQuaternion(m.quaternion);
      this.scene.add(m);
      const vel = this.phys.vel.clone().add(new THREE.Vector3((Math.random() - 0.5) * 12, 4 + Math.random() * 8, (Math.random() - 0.5) * 12));
      this.detached.push({ mesh: m, vel, ang: new THREE.Vector3(Math.random() * 10, Math.random() * 10, Math.random() * 10), r: CAR_SPEC.wheelRadius, life: 0 });
    }
  }

  stepDebris(dt) {
    for (const d of this.detached) {
      d.life += dt;
      d.vel.y -= G * dt;
      d.mesh.position.addScaledVector(d.vel, dt);
      const h = terrainHeight(d.mesh.position.x, d.mesh.position.z);
      if (d.mesh.position.y < h + d.r) {
        d.mesh.position.y = h + d.r;
        const n = terrainNormal(d.mesh.position.x, d.mesh.position.z);
        const vn = d.vel.dot(n);
        if (vn < 0) { d.vel.addScaledVector(n, -vn * 1.4); d.vel.multiplyScalar(0.8); }
      }
      d.mesh.rotation.x += d.ang.x * dt; d.mesh.rotation.y += d.ang.y * dt; d.mesh.rotation.z += d.ang.z * dt;
      d.ang.multiplyScalar(1 - dt * 0.8);
    }
  }

  /** Called once per frame after physics; syncs mesh. */
  sync() {
    const p = this.phys;
    this.mesh.quaternion.copy(p.quat);
    this.mesh.position.copy(this.visualOffset).applyQuaternion(p.quat).add(p.pos);
    const ws = this.mesh.userData.wheels;
    for (let i = 0; i < 4; i++) {
      const w = p.wheels[i];
      const comp = p.kinematic ? 0.11 : clamp(w.compression, 0, CAR_SPEC.restLength + 0.05);
      // wheel centre hangs below the ray origin by the remaining suspension travel
      ws[i].pivot.position.y = -this.visualOffset.y - (CAR_SPEC.restLength - comp);
      ws[i].pivot.rotation.y = w.steer;
      ws[i].mesh.rotation.x = w.spin;
    }
  }

  update(dt, input) {
    this.impactCooldown = Math.max(0, this.impactCooldown - dt);
    if (this.mode === 'ai') {
      this.updateAIPose(dt);
    } else if (this.mode === 'player') {
      if (input) { this.phys.input.throttle = input.throttle; this.phys.input.brake = input.brake; this.phys.input.steer = input.steer; this.phys.input.handbrake = input.handbrake; }
      this.phys.kinematic = false;
      const sub = 4, h = dt / sub;
      for (let i = 0; i < sub; i++) this.phys.step(h);
    } else if (this.mode === 'grabbed') {
      // pose is driven by the eagle
    } else if (this.mode === 'wreck' || this.mode === 'thrown') {
      this.phys.input.throttle = 0; this.phys.input.brake = 0.4; this.phys.input.steer = 0; this.phys.input.handbrake = 0;
      this.phys.kinematic = false;
      const sub = 4, h = dt / sub;
      for (let i = 0; i < sub; i++) this.phys.step(h);
      this.wreckTime += dt;
    }
    this.stepDebris(dt);
    this.sync();
  }
}

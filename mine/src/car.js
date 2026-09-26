// Box rally car: raycast-suspension rigid body with AWD, power-limited engine,
// boost, handbrake drifting, downforce and wall impulses.
import * as THREE from 'three';

const UPV = new THREE.Vector3(0, 1, 0);

export const CAR_SPEC = {
  mass: 1150,
  size: new THREE.Vector3(1.9, 1.15, 4.2),
  power: 560000,        // W  (~750 hp)  -- "more power"
  maxDriveForce: 19000, // N
  boostMul: 1.55,
  brakeForce: 21000,
  reversePower: 90000,
  grip: 1.65,           // tyre friction coefficient
  driftGrip: 0.75,      // rear grip multiplier on handbrake
  springK: 52000,
  damperC: 5200,
  restLen: 0.36,
  wheelR: 0.37,
  drag: 0.85,
  downforce: 2.1,
  wheels: [
    { x: 0.84, y: 0.0, z: 1.34, front: true },
    { x: -0.84, y: 0.0, z: 1.34, front: true },
    { x: 0.84, y: 0.0, z: -1.30, front: false },
    { x: -0.84, y: 0.0, z: -1.30, front: false },
  ],
  // collision spheres (local, relative to centre of mass)
  spheres: [
    [0.48, 0.38, 1.5], [-0.48, 0.38, 1.5],
    [0.48, 0.42, 0.0], [-0.48, 0.42, 0.0],
    [0.48, 0.38, -1.5], [-0.48, 0.38, -1.5],
  ],
  sphereR: 0.56,
};

export class CarPhysics {
  constructor(world, spec = CAR_SPEC) {
    this.world = world;
    this.spec = spec;
    this.pos = new THREE.Vector3();
    this.quat = new THREE.Quaternion();
    this.vel = new THREE.Vector3();
    this.ang = new THREE.Vector3();
    const { x: w, y: h, z: l } = spec.size, m = spec.mass;
    this.invI = new THREE.Vector3(12 / (m * (h * h + l * l)), 12 / (m * (w * w + l * l)) * 0.8, 12 / (m * (w * w + h * h)));
    this.wheelState = spec.wheels.map(() => ({ comp: 0, prevComp: 0, contact: false, spin: 0, steer: 0, point: new THREE.Vector3(), slip: 0 }));
    this.steer = 0;
    this.boost = 1;          // 0..1 boost tank
    this.boosting = false;
    this.grounded = 0;
    this.lastImpact = 0;
    this.impactFn = null;
  }
  reset(pos, forward) {
    this.pos.copy(pos);
    const f = forward.clone().setY(0).normalize();
    this.quat.setFromUnitVectors(new THREE.Vector3(0, 0, 1), f);
    this.vel.set(0, 0, 0); this.ang.set(0, 0, 0);
    this.wheelState.forEach((w) => { w.comp = w.prevComp = 0; });
  }
  get speed() { return this.vel.length(); }
  forward(t = new THREE.Vector3()) { return t.set(0, 0, 1).applyQuaternion(this.quat); }
  up(t = new THREE.Vector3()) { return t.set(0, 1, 0).applyQuaternion(this.quat); }

  // world-space inverse inertia * v
  applyInvI(v, out) {
    const qi = this.quat.clone().invert();
    out.copy(v).applyQuaternion(qi);
    out.x *= this.invI.x; out.y *= this.invI.y; out.z *= this.invI.z;
    return out.applyQuaternion(this.quat);
  }

  step(dtFrame, input) {
    const sub = Math.min(8, Math.max(2, Math.ceil(dtFrame / (1 / 240))));
    const dt = Math.min(dtFrame, 1 / 20) / sub;
    for (let i = 0; i < sub; i++) this.substep(dt, input);
  }

  substep(dt, input) {
    const S = this.spec;
    const up = this.up();
    const fwd = this.forward();
    const speed = this.vel.length();
    const vLongBody = this.vel.dot(fwd);

    // steering: less lock at speed, smoothed
    const maxSteer = THREE.MathUtils.lerp(0.62, 0.13, THREE.MathUtils.clamp(speed / 55, 0, 1));
    const target = input.steer * maxSteer;
    this.steer += (target - this.steer) * Math.min(1, dt * 10);

    // boost
    this.boosting = input.boost && this.boost > 0.02 && input.throttle > 0;
    if (this.boosting) this.boost = Math.max(0, this.boost - dt * 0.22);
    else this.boost = Math.min(1, this.boost + dt * 0.07);
    const pMul = this.boosting ? S.boostMul : 1;

    const force = new THREE.Vector3(0, -9.81 * S.mass, 0);
    const torque = new THREE.Vector3();
    const addForceAt = (F, p) => {
      force.add(F);
      torque.add(new THREE.Vector3().subVectors(p, this.pos).cross(F));
    };

    // drive force split
    let drive = 0;
    if (input.throttle > 0) {
      drive = input.throttle * Math.min(S.maxDriveForce * pMul, (S.power * pMul) / Math.max(Math.abs(vLongBody), 4));
    }
    let braking = false;
    if (input.brake > 0) {
      if (vLongBody > 1.0) braking = true;
      else drive = -input.brake * Math.min(9000, S.reversePower / Math.max(Math.abs(vLongBody), 3)) * (vLongBody > -16 ? 1 : 0);
    }

    this.grounded = 0;
    const groundN = new THREE.Vector3();
    const down = up.clone().negate();
    S.wheels.forEach((wd, i) => {
      const ws = this.wheelState[i];
      const anchor = new THREE.Vector3(wd.x, wd.y, wd.z).applyQuaternion(this.quat).add(this.pos);
      const maxLen = S.restLen + S.wheelR;
      let hit = this.world.raycast(anchor, down, maxLen);
      // tyres only grip road-like surfaces; walls are handled by the body spheres
      if (hit && Math.abs(hit.normal.y) < 0.55) hit = null;
      ws.prevComp = ws.comp;
      ws.steer = wd.front ? this.steer : 0;
      if (!hit) { ws.contact = false; ws.comp = 0; ws.len = S.restLen; return; }
      ws.contact = true;
      this.grounded++;
      ws.comp = THREE.MathUtils.clamp(maxLen - hit.distance, 0, S.restLen);
      ws.len = S.restLen - ws.comp;
      let n = hit.normal;
      if (n.dot(up) < 0) n.negate();
      groundN.add(n);
      const compVel = (ws.comp - ws.prevComp) / dt;
      let Fs = S.springK * ws.comp + S.damperC * compVel;
      // bump stop
      if (ws.comp > S.restLen * 0.9) Fs += (ws.comp - S.restLen * 0.9) * 400000;
      Fs = Math.max(0, Fs);
      addForceAt(up.clone().multiplyScalar(Fs), anchor);

      // tyre frame on the contact plane
      const cp = hit.point;
      ws.point.copy(cp);
      const wf = new THREE.Vector3(Math.sin(ws.steer), 0, Math.cos(ws.steer)).applyQuaternion(this.quat);
      wf.addScaledVector(n, -wf.dot(n)).normalize();
      const wsd = new THREE.Vector3().crossVectors(n, wf).normalize(); // left
      const r = new THREE.Vector3().subVectors(cp, this.pos);
      const vc = this.vel.clone().add(new THREE.Vector3().crossVectors(this.ang, r));
      const vLong = vc.dot(wf), vLat = vc.dot(wsd);

      const load = Math.min(Fs, S.mass * 9.81 * 1.6);
      let mu = S.grip;
      if (!wd.front && input.handbrake) mu *= S.driftGrip;
      // lateral: critically damped "stop the slide" force (per wheel mass share)
      let Flat = -vLat * (S.mass / 4) / dt * 0.22;
      let Flong = drive / 4;
      if (braking) Flong -= Math.sign(vLong) * S.brakeForce / 4 * input.brake;
      if (!wd.front && input.handbrake) Flong -= Math.sign(vLong) * 3000;
      Flong -= vLong * 12; // rolling resistance
      const maxF = mu * load;
      const tot = Math.hypot(Flat, Flong);
      ws.slip = tot > maxF ? 1 - maxF / tot : 0;
      if (tot > maxF) { const sc = maxF / tot; Flat *= sc; Flong *= sc; }
      const F = wf.clone().multiplyScalar(Flong).addScaledVector(wsd, Flat);
      // apply slightly above the ground to reduce body roll (arcade stability)
      const app = cp.clone().addScaledVector(up, 0.3);
      addForceAt(F, app);
      ws.spin += (vLong / S.wheelR) * dt;
    });

    // aero
    force.addScaledVector(this.vel, -S.drag * speed);
    if (this.grounded > 0) force.addScaledVector(up, -S.downforce * speed * speed);

    // anti-roll / stability: damp angular velocity, and self-right in the air
    if (this.grounded === 0) {
      const levelAxis = new THREE.Vector3().crossVectors(up, UPV);
      torque.addScaledVector(levelAxis, 2600);
      torque.addScaledVector(this.ang, -500);
    } else {
      // stability control: keep the body aligned with the road surface
      const right = new THREE.Vector3(-1, 0, 0).applyQuaternion(this.quat);
      groundN.normalize();
      const rollErr = Math.asin(THREE.MathUtils.clamp(up.clone().cross(groundN).dot(fwd), -1, 1));
      const rollRate = this.ang.dot(fwd);
      torque.addScaledVector(fwd, rollErr * 16000 - rollRate * 3000);
      const pitchRate = this.ang.dot(right);
      torque.addScaledVector(right, -pitchRate * 900);
    }

    // integrate
    this.vel.addScaledVector(force, dt / S.mass);
    const angAcc = this.applyInvI(torque, new THREE.Vector3());
    this.ang.addScaledVector(angAcc, dt);
    this.ang.multiplyScalar(1 - dt * 0.4);
    this.pos.addScaledVector(this.vel, dt);
    const w = this.ang;
    const dq = new THREE.Quaternion(w.x * dt * 0.5, w.y * dt * 0.5, w.z * dt * 0.5, 0).multiply(this.quat);
    this.quat.x += dq.x; this.quat.y += dq.y; this.quat.z += dq.z; this.quat.w += dq.w;
    this.quat.normalize();

    this.collideWalls();
  }

  collideWalls() {
    const S = this.spec;
    for (let iter = 0; iter < 2; iter++) {
      let any = false;
      for (const s of S.spheres) {
        const c = new THREE.Vector3(s[0], s[1], s[2]).applyQuaternion(this.quat).add(this.pos);
        const hit = this.world.sphereContact(c, S.sphereR);
        if (!hit || hit.depth <= 1e-4) continue;
        any = true;
        this.pos.addScaledVector(hit.normal, hit.depth * 0.9);
        this.resolveImpulse(c, hit.normal, 0.15, 0.25);
      }
      if (!any) break;
    }
  }

  // impulse against a static surface (or moving obstacle with velocity vObs)
  resolveImpulse(point, n, restitution, friction, vObs = null) {
    const r = new THREE.Vector3().subVectors(point, this.pos);
    const v = this.vel.clone().add(new THREE.Vector3().crossVectors(this.ang, r));
    if (vObs) v.sub(vObs);
    const vn = v.dot(n);
    if (vn >= 0) return 0;
    const rxn = new THREE.Vector3().crossVectors(r, n);
    const k = 1 / this.spec.mass + n.dot(this.applyInvI(rxn, new THREE.Vector3()).cross(r));
    const j = (-(1 + restitution) * vn) / k;
    const J = n.clone().multiplyScalar(j);
    // friction along the surface
    const vt = v.clone().addScaledVector(n, -vn);
    const vtl = vt.length();
    if (vtl > 1e-3) {
      const t = vt.divideScalar(vtl);
      const rxt = new THREE.Vector3().crossVectors(r, t);
      const kt = 1 / this.spec.mass + t.dot(this.applyInvI(rxt, new THREE.Vector3()).cross(r));
      const jt = Math.min(vtl / kt, friction * j);
      J.addScaledVector(t, -jt);
    }
    this.vel.addScaledVector(J, 1 / this.spec.mass);
    const dAng = this.applyInvI(new THREE.Vector3().crossVectors(r, J), new THREE.Vector3());
    // walls may yaw the car but should not flip it: strip most of the roll impulse
    const f = this.forward();
    dAng.addScaledVector(f, -dAng.dot(f) * 0.9);
    this.ang.add(dAng.multiplyScalar(0.6));
    const impact = -vn;
    if (impact > 3 && this.impactFn) this.impactFn(impact);
    return impact;
  }
}

// ---------------- visual ----------------
export function createCarMesh() {
  const g = new THREE.Group();
  const paint = new THREE.MeshPhysicalMaterial({ color: 0xd9531e, roughness: 0.38, metalness: 0.25, clearcoat: 1, clearcoatRoughness: 0.2 });
  const dark = new THREE.MeshStandardMaterial({ color: 0x1b1c1e, roughness: 0.7, metalness: 0.3 });
  const glass = new THREE.MeshPhysicalMaterial({ color: 0x0b1116, roughness: 0.05, metalness: 0.4, clearcoat: 1 });
  const chrome = new THREE.MeshStandardMaterial({ color: 0xbbbbbb, roughness: 0.2, metalness: 1 });
  const headMat = new THREE.MeshStandardMaterial({ color: 0xffffff, emissive: 0xfff3d0, emissiveIntensity: 2.2 });
  const tailMat = new THREE.MeshStandardMaterial({ color: 0x440000, emissive: 0xff1a0a, emissiveIntensity: 2.5 });
  const amberMat = new THREE.MeshStandardMaterial({ color: 0x442200, emissive: 0xffa010, emissiveIntensity: 4 });
  const box = (w, h, d, mat, x, y, z) => {
    const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
    m.position.set(x, y, z); m.castShadow = true; m.receiveShadow = true; g.add(m); return m;
  };
  const body = new THREE.Group();
  g.add(body);
  const add = (m) => { g.remove(m); body.add(m); return m; };
  // lower body + hood + cabin (all boxes)
  add(box(1.86, 0.5, 4.1, paint, 0, 0.12, 0));
  add(box(1.78, 0.18, 1.35, paint, 0, 0.44, 1.30)); // hood
  add(box(1.62, 0.62, 1.9, paint, 0, 0.66, -0.35)); // cabin
  add(box(1.5, 0.5, 0.06, glass, 0, 0.68, 0.62)).rotation.x = -0.35; // windscreen
  add(box(1.64, 0.44, 1.5, glass, 0, 0.7, -0.38)); // side glass band
  add(box(1.5, 0.4, 0.05, glass, 0, 0.7, -1.31)); // rear glass
  add(box(1.66, 0.06, 1.95, paint, 0, 0.99, -0.35)); // roof panel
  // bumpers, skid plate, flares
  add(box(1.96, 0.24, 0.22, dark, 0, -0.04, 2.1));
  add(box(1.96, 0.24, 0.22, dark, 0, -0.04, -2.1));
  add(box(1.5, 0.06, 1.2, chrome, 0, -0.14, 1.5));
  for (const sx of [-1, 1]) for (const sz of [1.34, -1.3]) add(box(0.14, 0.2, 1.0, dark, sx * 0.99, 0.28, sz));
  // lights
  for (const sx of [-1, 1]) {
    add(box(0.34, 0.14, 0.04, headMat, sx * 0.62, 0.2, 2.06));
    add(box(0.3, 0.12, 0.04, tailMat, sx * 0.66, 0.22, -2.06));
    add(box(0.07, 0.1, 0.2, chrome, sx * 0.4, -0.12, -2.12)); // exhaust
  }
  // roof light bar + amber beacons
  add(box(1.3, 0.1, 0.18, dark, 0, 1.07, 0.3));
  for (const sx of [-0.45, -0.15, 0.15, 0.45]) add(box(0.22, 0.08, 0.04, headMat, sx, 1.07, 0.4));
  add(box(0.16, 0.1, 0.16, amberMat, 0.6, 1.07, -1.1));
  add(box(0.16, 0.1, 0.16, amberMat, -0.6, 1.07, -1.1));
  // spoiler
  add(box(1.7, 0.05, 0.36, dark, 0, 1.12, -1.95));
  add(box(0.06, 0.2, 0.2, dark, 0.6, 1.0, -1.9));
  add(box(0.06, 0.2, 0.2, dark, -0.6, 1.0, -1.9));
  // wheels
  const wheels = [];
  const tyreGeo = new THREE.CylinderGeometry(CAR_SPEC.wheelR, CAR_SPEC.wheelR, 0.3, 20);
  tyreGeo.rotateZ(Math.PI / 2);
  const rimGeo = new THREE.CylinderGeometry(0.22, 0.22, 0.31, 6);
  rimGeo.rotateZ(Math.PI / 2);
  const tyreMat = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.9 });
  for (const wd of CAR_SPEC.wheels) {
    const pivot = new THREE.Group();
    const spin = new THREE.Group();
    const t = new THREE.Mesh(tyreGeo, tyreMat); t.castShadow = true;
    const rim = new THREE.Mesh(rimGeo, chrome);
    spin.add(t, rim);
    pivot.add(spin);
    pivot.position.set(wd.x, wd.y, wd.z);
    g.add(pivot);
    wheels.push({ pivot, spin, def: wd });
  }
  body.position.y = 0.05;
  g.userData = { wheels, body, headMat, tailMat };
  return g;
}

export function syncCarMesh(mesh, phys) {
  mesh.position.copy(phys.pos);
  mesh.quaternion.copy(phys.quat);
  const { wheels } = mesh.userData;
  wheels.forEach((w, i) => {
    const s = phys.wheelState[i];
    const len = s.contact ? s.len : phys.spec.restLen;
    w.pivot.position.y = w.def.y - len;
    w.pivot.rotation.y = s.steer;
    w.spin.rotation.x = s.spin;
  });
}

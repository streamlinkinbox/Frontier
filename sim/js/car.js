// Arcade-realistic rigid body car on flat ground (XZ plane, Y up).
// Local frame: +Z forward, +X = (cos h, 0, -sin h) — matches mat4.trs yaw rotation.
export class Car {
  constructor() {
    this.mass = 1200;
    this.inertia = 1800;
    this.halfExt = [0.9, 0.38, 2.1]; // body box collider half extents (m)
    this.bodyY = 0.78;
    this.wheelR = 0.36;
    this.wheelW = 0.28;
    this.wheels = [
      { lx: -1.0, lz: 1.32, front: true }, { lx: 1.0, lz: 1.32, front: true },
      { lx: -1.0, lz: -1.28, front: false }, { lx: 1.0, lz: -1.28, front: false },
    ].map(w => ({ ...w, spin: 0, spinVel: 0, slipLat: 0, slipLong: 0, sand: 0, smoke: 0, pos: [0, 0, 0], vel: [0, 0, 0], fwd: [0, 0, 1] }));
    this.reset();
  }
  reset() {
    this.x = 0; this.z = 0; this.heading = 0;
    this.vx = 0; this.vz = 0; this.w = 0;
    this.steer = 0; this.drift = 0; this.speed = 0;
    this.roll = 0; this.pitch = 0;
  }
  fwd() { return [Math.sin(this.heading), Math.cos(this.heading)]; }
  right() { return [Math.cos(this.heading), -Math.sin(this.heading)]; }
  toWorld(lx, lz) {
    const c = Math.cos(this.heading), s = Math.sin(this.heading);
    return [lx * c + lz * s, -lx * s + lz * c];
  }

  step(dt, input, obstacles) {
    const g = 9.81, mu = 1.05;
    const steerTarget = (input.left ? 1 : 0) - (input.right ? 1 : 0);
    const speed = Math.hypot(this.vx, this.vz);
    const maxSteer = 0.62 - Math.min(speed / 40, 0.35);
    this.steer += (steerTarget * maxSteer - this.steer) * Math.min(1, dt * 7);

    const [fx, fz] = this.fwd();
    const vLongCar = this.vx * fx + this.vz * fz;
    let Fx = 0, Fz = 0, torque = 0;
    const N = this.mass * g / 4;
    let driftAcc = 0;

    for (const wh of this.wheels) {
      const [rx, rz] = this.toWorld(wh.lx, wh.lz);
      // velocity of contact patch: v + w x r
      const wvx = this.vx + this.w * rz, wvz = this.vz - this.w * rx;
      const ang = this.heading + (wh.front ? this.steer : 0);
      const dfx = Math.sin(ang), dfz = Math.cos(ang);
      const drx = Math.cos(ang), drz = -Math.sin(ang);
      const vLong = wvx * dfx + wvz * dfz;
      const vLat = wvx * drx + wvz * drz;

      let grip = mu * N;
      if (!wh.front && input.handbrake) grip *= 0.42;
      // lateral: saturating slip curve
      let fLat = -grip * Math.tanh(vLat / 1.2);
      // longitudinal
      let fLong = 0, spinSlip = 0;
      if (!wh.front) {
        if (input.throttle) {
          const demand = 5200 * (vLongCar < 40 ? 1 : 0.2);
          fLong += demand;
          const excess = demand - grip * 0.9;
          if (excess > 0) spinSlip = excess / 180 + (speed < 8 ? 6 : 2); // wheelspin (m/s)
        }
        if (input.handbrake) { fLong += -Math.sign(vLong) * Math.min(grip * 0.8, Math.abs(vLong) * 3000); spinSlip = -Math.abs(vLong); }
      }
      if (input.brake) {
        if (vLongCar > 0.8) fLong += -Math.sign(vLong) * grip * 0.9;
        else if (!wh.front) fLong += -2600; // reverse
      }
      fLong += -vLong * 25; // rolling resistance
      // friction circle
      const tot = Math.hypot(fLong, fLat), lim = grip * 1.05;
      if (tot > lim) { fLong *= lim / tot; fLat *= lim / tot; }

      const wfx = fLong * dfx + fLat * drx, wfz = fLong * dfz + fLat * drz;
      Fx += wfx; Fz += wfz;
      torque += rz * wfx - rx * wfz;

      wh.slipLat = vLat; wh.slipLong = spinSlip;
      wh.spinVel = (vLong + spinSlip) / this.wheelR;
      if (input.handbrake && !wh.front) wh.spinVel = 0;
      wh.spin += wh.spinVel * dt;
      wh.pos = [this.x + rx, 0, this.z + rz];
      wh.vel = [wvx, 0, wvz];
      wh.fwd = [dfx, 0, dfz];
      if (!wh.front) driftAcc += Math.abs(vLat);
    }
    // aero drag
    Fx -= this.vx * speed * 0.9; Fz -= this.vz * speed * 0.9;

    const ax = Fx / this.mass, az = Fz / this.mass;
    this.vx += ax * dt; this.vz += az * dt;
    this.w += (torque / this.inertia) * dt;
    this.w *= 1 - Math.min(1, dt * 0.6);
    this.x += this.vx * dt; this.z += this.vz * dt;
    this.heading += this.w * dt;
    this.speed = Math.hypot(this.vx, this.vz);

    // body roll/pitch for visuals from local acceleration
    const [rgx, rgz] = this.right();
    const aLat = ax * rgx + az * rgz, aLong = ax * fx + az * fz;
    this.roll += (-aLat * 0.012 - this.roll) * Math.min(1, dt * 6);
    this.pitch += (aLong * 0.008 - this.pitch) * Math.min(1, dt * 6);

    // drift metric and emission rates
    const rearLat = driftAcc / 2;
    this.drift = this.speed > 3 ? Math.max(0, Math.min(1, (rearLat - 1.8) / 5)) : 0;
    for (const wh of this.wheels) {
      const slip = Math.abs(wh.slipLat) + Math.abs(wh.slipLong);
      const roll = Math.abs(wh.spinVel * this.wheelR);
      wh.sand = roll < 0.3 && slip < 0.3 ? 0 : Math.min(1, slip / 6 + roll / 30);
      const spinSmoke = !wh.front && wh.slipLong > 9 ? Math.min(1, (wh.slipLong - 9) / 10) * 0.35 : 0;
      wh.smoke = wh.front ? this.drift * 0.25 : Math.max(this.drift, spinSmoke);
    }

    for (const o of obstacles) this.collide(o);
    // world bounds
    const B = 140;
    if (Math.abs(this.x) > B) { this.x = Math.sign(this.x) * B; this.vx *= -0.3; }
    if (Math.abs(this.z) > B) { this.z = Math.sign(this.z) * B; this.vz *= -0.3; }
  }

  // 2D OBB (car) vs OBB (crate) SAT with positional + velocity response
  collide(o) {
    const A = { c: [this.x, this.z], h: [this.halfExt[0], this.halfExt[2]], ax: [this.right(), this.fwd()] };
    const co = Math.cos(o.yaw), so = Math.sin(o.yaw);
    const Bx = { c: [o.x, o.z], h: [o.hx, o.hz], ax: [[co, -so], [so, co]] };
    const d = [Bx.c[0] - A.c[0], Bx.c[1] - A.c[1]];
    let minOv = Infinity, nrm = null;
    for (const axis of [...A.ax, ...Bx.ax]) {
      const proj = (box) => box.h[0] * Math.abs(box.ax[0][0] * axis[0] + box.ax[0][1] * axis[1]) + box.h[1] * Math.abs(box.ax[1][0] * axis[0] + box.ax[1][1] * axis[1]);
      const dist = d[0] * axis[0] + d[1] * axis[1];
      const ov = proj(A) + proj(Bx) - Math.abs(dist);
      if (ov <= 0) return false;
      if (ov < minOv) { minOv = ov; nrm = dist > 0 ? [-axis[0], -axis[1]] : [axis[0], axis[1]]; }
    }
    // push car out along nrm (points from crate to car)
    this.x += nrm[0] * minOv; this.z += nrm[1] * minOv;
    const vn = this.vx * nrm[0] + this.vz * nrm[1];
    if (vn < 0) {
      this.vx -= 1.35 * vn * nrm[0]; this.vz -= 1.35 * vn * nrm[1];
      // spin from off-centre hit
      const [fx, fz] = this.fwd();
      const side = fx * nrm[1] - fz * nrm[0];
      this.w += side * vn * 0.08;
      this.w *= 0.85;
      this.impact = Math.min(1, -vn / 10);
    }
    return true;
  }
}

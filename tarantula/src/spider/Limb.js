import * as THREE from 'three';

// A tarantula limb is modelled as a chain of rigid podomeres (true to an arthropod exoskeleton,
// so no skinning is needed). All kinematics are expressed in the (posed) body frame:
//   - the coxa swings the whole leg plane around the body's dorsoventral axis (promotor/remotor),
//   - every distal joint flexes inside that vertical leg plane,
//   - the patella-tibia joint only has a small fixed flexion (it mainly moves laterally in reality).
// Joint angles are stored as absolute pitch angles inside the leg plane; IK outputs the same
// representation so that procedural poses (attacks, threat display) blend cleanly with IK.

const _up = new THREE.Vector3(0, 1, 0);
const _d = new THREE.Vector3(), _pn = new THREE.Vector3(), _dir = new THREE.Vector3();
const _x = new THREE.Vector3(), _y = new THREE.Vector3(), _m = new THREE.Matrix4();
const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
const lerp = (a, b, t) => a + (b - a) * t;

export class Limb {
  /**
   * @param {object} o
   *  lengths: podomere lengths (legs: 7, palps: 6)
   *  socket: body-space Vector3; side: +1 left / -1 right; yaw0 (rad)
   *  isPalp
   */
  constructor({ lengths, radii, socket, side, yaw0, isPalp = false, coxaPitch = -0.32, patTibFlex = 0.1 }) {
    this.L = lengths.slice();
    this.R = radii.slice();
    this.S = socket.clone();
    this.side = side;
    this.yaw0 = yaw0;
    this.isPalp = isPalp;
    this.coxaPitch = coxaPitch;
    this.patTibFlex = patTibFlex;
    this.n = lengths.length;
    this.extension = 0.93;
    this.segments = []; // Object3D per podomere (assigned by builder)
    // pose: yaw + absolute in-plane angles [coxa, troch, femur, patella, tibia, (meta), tarsus]
    this.pose = { yaw: yaw0, cyaw: 0, a: new Float32Array(this.n) };
    this.joints = Array.from({ length: this.n + 1 }, () => new THREE.Vector3());
    this.hip = new THREE.Vector3();
    this.tip = new THREE.Vector3();
    this.totalLen = lengths.slice(2).reduce((a, b) => a + b, 0);
    // comfortable reach from the coxal socket (3D), used to reject footholds the leg cannot take
    this.maxReach = (lengths[0] + lengths[1]) * 0.85 + lengths.slice(2).reduce((a, b) => a + b, 0) * 0.9;
    // combined patella+tibia chord
    const Lp = this.L[3], Lt = this.L[4], f = patTibFlex;
    const vx = Lp + Lt * Math.cos(f), vy = -Lt * Math.sin(f);
    this.L23 = Math.hypot(vx, vy);
    this.delta = Math.atan2(vy, vx); // chord angle relative to patella
  }

  planeDir(yaw, out = _d) {
    return out.set(Math.sin(yaw) * this.side, 0, Math.cos(yaw));
  }

  // coxa + trochanter angles (fixed-ish anatomy, trochanter follows femur partially)
  _proximal(a, femurAngle) {
    a[0] = this.coxaPitch;
    a[1] = lerp(this.coxaPitch, femurAngle, 0.35) - 0.05;
  }

  /**
   * Solve for the tip (tarsus end) reaching `target` (body space) with the tarsus making absolute
   * in-plane angle derived from `contactNormal` (body space). Writes into `outPose`.
   */
  solve(target, contactNormal, outPose, tarsusLift = 0, femurLift = 0, tarsusBias = 0, coxaYaw = 0) {
    // Two passes: the tarsus wants to lie on the substrate, but the tarso-metatarsal joint has a
    // limited range (it flexes ventrally, barely hyper-extends). If the wish violates that range
    // (walls, ledges, steep slopes) clamp it relative to the metatarsus and re-solve the chain.
    let aTar = this._tarsusWish(target, contactNormal, tarsusLift) + tarsusBias;
    this._solveChain(target, aTar, outPose, femurLift, coxaYaw);
    if (!this.isPalp) {
      const meta = outPose.a[5];
      const rel = aTar - meta;
      const lo = -1.05, hi = 0.3;
      if (rel < lo || rel > hi) {
        aTar = meta + clamp(rel, lo, hi);
        this._solveChain(target, aTar, outPose, femurLift, coxaYaw);
      }
    }
    return outPose;
  }

  _tarsusWish(target, contactNormal, tarsusLift) {
    const S = this.S;
    let tx = target.x - S.x, tz = target.z - S.z;
    let yaw = Math.atan2(tx * this.side, tz);
    if (yaw < -Math.PI * 0.5) yaw += Math.PI * 2;
    yaw = clamp(yaw, -0.35, Math.PI + 0.25);
    const d = this.planeDir(yaw, new THREE.Vector3());
    let aTar;
    if (contactNormal) {
      const nr = contactNormal.dot(d), ny = contactNormal.y;
      aTar = Math.atan2(-nr, ny) - 0.42; // tangent (ny, -nr); tarsus inclined ~24 deg, scopula pad on the substrate
    } else aTar = -0.25;
    return clamp(aTar + tarsusLift, -1.6, 1.2);
  }

  // coxaYaw: extra promotor/remotor rotation of the coxa+trochanter relative to the straight
  // socket->target direction. The coxa is a real joint with its own yaw; the distal leg plane then
  // passes through the hip (end of trochanter) and the target. 0 = classic single-plane leg.
  _solveChain(target, aTar, outPose, femurLift, coxaYaw = 0) {
    const S = this.S;
    const wrapYaw = (y) => clamp(y < -Math.PI * 0.5 ? y + Math.PI * 2 : y, -0.35, Math.PI + 0.25);
    // targets behind the animal on its own side wrap to yaw > pi/2; keep the plane on its own side
    const yaw = wrapYaw(Math.atan2((target.x - S.x) * this.side, target.z - S.z));
    const a = outPose.a;

    // hip = end of trochanter; use femur guess for trochanter angle from last frame
    const guessFem = this.pose.a[2] || 0.7;
    this._proximal(a, guessFem);
    const hx = Math.cos(a[0]) * this.L[0] + Math.cos(a[1]) * this.L[1];
    const hy = Math.sin(a[0]) * this.L[0] + Math.sin(a[1]) * this.L[1];

    let yawD = yaw, yawC = yaw;
    const dC = this.planeDir(yaw, new THREE.Vector3());
    if (coxaYaw) {
      yawC = yaw + coxaYaw;
      this.planeDir(yawC, dC);
      yawD = wrapYaw(Math.atan2((target.x - S.x - dC.x * hx) * this.side, target.z - S.z - dC.z * hx));
    }
    const d = this.planeDir(yawD, new THREE.Vector3());
    outPose.yaw = yawD;
    outPose.cyaw = yawC - yawD;

    // target in (distal) leg-plane coordinates relative to the hip
    const tr = (target.x - S.x - dC.x * hx) * d.x + (target.z - S.z - dC.z * hx) * d.z;
    const ty = target.y - S.y - hy;

    const last = this.n - 1;
    const Ltar = this.L[last];
    const Px = tr - Math.cos(aTar) * Ltar, Py = ty - Math.sin(aTar) * Ltar;
    const L1 = this.L[2];

    if (this.isPalp) {
      // femur + (patella+tibia) two-link, "knee up"
      const L2 = this.L23;
      let D = Math.hypot(Px, Py);
      D = clamp(D, Math.abs(L1 - L2) + 1e-3, L1 + L2 - 1e-3);
      // knee-up solution, limited to the femur's real range: when the wrist target comes close to or
      // behind the hip (tucked palp) the unclamped solution flips the femur backwards/down under the
      // prosoma; keep it raised instead and let the distal chain point at the target
      let th = Math.atan2(Py, Px) + Math.acos(clamp((L1 * L1 + D * D - L2 * L2) / (2 * L1 * D), -1, 1));
      // (atan2 + acos lies in (-pi, 2pi): anything past the upper limit is over-rotation up/back)
      // femurLift (collision steering) lowers/raises the palp knee; the distal pair keeps aiming at
      // the wrist target, trading a little reach for clearance
      th = clamp(clamp(th, -0.5, 1.95) + femurLift, -0.5, 1.95);
      const Kx = Math.cos(th) * L1, Ky = Math.sin(th) * L1;
      const a23 = Math.atan2(Py - Ky, Px - Kx);
      a[2] = th;
      a[3] = a23 - this.delta;
      a[4] = a[3] - this.patTibFlex;
      a[5] = aTar;
    } else {
      const L23 = this.L23, L4 = this.L[5];
      const Lmax = L23 + L4, Lmin = Math.abs(L23 - L4);
      const D = Math.hypot(Px, Py), thP = Math.atan2(Py, Px);
      // Femur elevation is chosen so the distal pair (patella+tibia, metatarsus) stays ~93 % extended:
      // the patella becomes the apex of the leg and each distal podomere descends more steeply,
      // as in a resting/walking theraphosid. (A fixed "knee-up" angle folds the leg into a table leg.)
      const f = (th) => Math.hypot(Px - Math.cos(th) * L1, Py - Math.sin(th) * L1);
      const want = Lmax * this.extension;
      const thMax = 1.38;
      let th;
      if (f(thP) >= want) th = thP;                 // target far away: reach straight toward it
      else {
        let lo = thP, hi = Math.min(thP + Math.PI * 0.95, thMax + 0.8);
        if (f(hi) < want) th = hi;
        else { for (let i = 0; i < 20; i++) { const mid = (lo + hi) / 2; if (f(mid) < want) lo = mid; else hi = mid; } th = (lo + hi) / 2; }
      }
      // femurLift: collision avoidance (raises the patella apex over bumps / ledges). Only meaningful
      // while the target is comfortably reachable; on a stretched leg it would just fold it wrongly.
      const slack = clamp((L1 + Lmax - Math.hypot(Px, Py)) / (0.25 * L1), 0, 1);
      const fl = femurLift > 0 ? femurLift * slack : femurLift;
      th = Math.min(th + fl, thMax + Math.min(fl, 0.35));
      const Kx = Math.cos(th) * L1, Ky = Math.sin(th) * L1;
      const vx = Px - Kx, vy = Py - Ky;
      const dd = clamp(Math.hypot(vx, vy), Lmin + 1e-3, Lmax - 1e-3);
      const A = Math.acos(clamp((L23 * L23 + dd * dd - L4 * L4) / (2 * L23 * dd), -1, 1));
      const a23 = Math.atan2(vy, vx) + A;
      const Jx = Kx + Math.cos(a23) * L23, Jy = Ky + Math.sin(a23) * L23;
      a[2] = th;
      a[3] = a23 - this.delta;
      a[4] = a[3] - this.patTibFlex;
      a[5] = Math.atan2(Py - Jy, Px - Jx);
      a[6] = aTar;
    }
    this._proximal(a, a[2]);
    return outPose;
  }

  // Forward kinematics -> place podomere objects (children of the body group)
  apply(pose = this.pose) {
    if (pose !== this.pose) { this.pose.yaw = pose.yaw; this.pose.cyaw = pose.cyaw || 0; this.pose.a.set(pose.a); }
    const cy = this.pose.cyaw || 0;
    let d = this.planeDir(this.pose.yaw + cy, _d);
    _pn.crossVectors(_up, d).normalize();
    const p = this.joints[0].copy(this.S);
    for (let i = 0; i < this.n; i++) {
      if (i === 2 && cy) { d = this.planeDir(this.pose.yaw, _d); _pn.crossVectors(_up, d).normalize(); }
      const ang = this.pose.a[i];
      _dir.copy(d).multiplyScalar(Math.cos(ang)).addScaledVector(_up, Math.sin(ang));
      const seg = this.segments[i];
      if (seg) {
        seg.position.copy(p);
        _y.crossVectors(_dir, _pn);
        _m.makeBasis(_pn, _y, _dir);
        seg.quaternion.setFromRotationMatrix(_m);
      }
      const next = this.joints[i + 1].copy(p).addScaledVector(_dir, this.L[i]);
      if (i === 1) this.hip.copy(next);
      p.copy(next);
    }
    this.tip.copy(p);
  }

  static lerpPose(a, b, t, out) {
    out.yaw = lerp(a.yaw, b.yaw, t);
    out.cyaw = lerp(a.cyaw || 0, b.cyaw || 0, t);
    for (let i = 0; i < out.a.length; i++) out.a[i] = lerp(a.a[i], b.a[i], t);
    return out;
  }

  makePose() { return { yaw: this.yaw0, cyaw: 0, a: new Float32Array(this.n) }; }
}

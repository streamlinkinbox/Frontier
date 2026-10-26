// Editor camera: orbit for sculpting context, fly for going inside caves.
// No collision — this is an editor; a "Go to cave" preset puts you at the mouth.

import { clamp } from './noise.js';

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

export function perspective(fovY, aspect, near, far) {
  const f = 1 / Math.tan(fovY / 2);
  const m = new Float32Array(16);
  m[0] = f / aspect; m[5] = f;
  m[10] = far / (near - far); m[11] = -1;
  m[14] = (near * far) / (near - far);
  return m;
}
export function lookAt(eye, target, up = [0, 1, 0]) {
  const z = norm(sub(eye, target));
  const x = norm(cross(up, z));
  const y = cross(z, x);
  const m = new Float32Array(16);
  m[0] = x[0]; m[1] = y[0]; m[2] = z[0];
  m[4] = x[1]; m[5] = y[1]; m[6] = z[1];
  m[8] = x[2]; m[9] = y[2]; m[10] = z[2];
  m[12] = -dot(x, eye); m[13] = -dot(y, eye); m[14] = -dot(z, eye); m[15] = 1;
  return m;
}
export function multiply(a, b) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  }
  return out;
}

export class EditorCamera {
  constructor() {
    this.mode = 'orbit';
    this.fov = 50 * Math.PI / 180;
    this.near = 0.08;
    this.far = 1600;
    // orbit state
    this.target = [-8, 22, 0];
    this.radius = 92;
    this.azimuth = 0.85;   // around Y
    this.polar = 1.06;     // from +Y
    // fly state
    this.pos = [38, 30, 42];
    this.yaw = Math.atan2(-46, -42);
    this.pitch = -0.12;
    this.flySpeed = 14;
  }

  getPosition() {
    if (this.mode === 'fly') return this.pos.slice();
    const sp = Math.sin(this.polar), cp = Math.cos(this.polar);
    return [
      this.target[0] + this.radius * sp * Math.sin(this.azimuth),
      this.target[1] + this.radius * cp,
      this.target[2] + this.radius * sp * Math.cos(this.azimuth),
    ];
  }
  getForward() {
    if (this.mode === 'fly') {
      const cp = Math.cos(this.pitch);
      return [Math.sin(this.yaw) * cp, Math.sin(this.pitch), Math.cos(this.yaw) * cp];
    }
    return norm(sub(this.target, this.getPosition()));
  }
  getBasis() {
    const pos = this.getPosition();
    const forward = this.getForward();
    let right = cross(forward, [0, 1, 0]);
    if (Math.hypot(right[0], right[1], right[2]) < 1e-5) right = [1, 0, 0]; else right = norm(right);
    const up = cross(right, forward);
    return { pos, forward, right, up };
  }
  viewProj(aspect) {
    const { pos, forward } = this.getBasis();
    const view = lookAt(pos, add(pos, forward));
    return multiply(perspective(this.fov, aspect, this.near, this.far), view);
  }
  ray(ndcX, ndcY, aspect) {
    const { pos, forward, right, up } = this.getBasis();
    const tanY = Math.tan(this.fov / 2), tanX = tanY * aspect;
    const dir = norm(add(add(forward, scale(right, ndcX * tanX)), scale(up, ndcY * tanY)));
    return { origin: pos, dir };
  }

  // ---- orbit controls
  orbit(dx, dy) {
    this.azimuth -= dx * 0.005;
    this.polar = clamp(this.polar - dy * 0.005, 0.05, Math.PI - 0.05);
  }
  pan(dx, dy, viewportHeight) {
    const { right, up } = this.getBasis();
    const scaleWorld = (2 * this.radius * Math.tan(this.fov / 2)) / Math.max(1, viewportHeight);
    const move = add(scale(right, -dx * scaleWorld), scale(up, dy * scaleWorld));
    if (this.mode === 'fly') this.pos = add(this.pos, move);
    else this.target = add(this.target, move);
  }
  dolly(factor) {
    if (this.mode === 'fly') {
      const f = this.getForward();
      const step = this.flySpeed * 0.35 * (factor > 1 ? 1 : -1) * Math.abs(Math.log(factor)) * 4;
      this.pos = add(this.pos, scale(f, step));
    } else {
      this.radius = clamp(this.radius * factor, 1.2, 700);
    }
  }
  frame(target, radius, azimuth = 0.85, polar = 1.06) {
    this.mode = 'orbit';
    this.target = target.slice();
    this.radius = radius;
    this.azimuth = azimuth; this.polar = polar;
  }

  // ---- fly controls
  toFly(atPos = null, lookAtPoint = null) {
    if (atPos) this.pos = atPos.slice();
    else if (this.mode !== 'fly') this.pos = this.getPosition();
    const target = lookAtPoint ?? (this.mode !== 'fly' ? this.target : add(this.pos, this.getForward()));
    const f = norm(sub(target, this.pos));
    this.yaw = Math.atan2(f[0], f[2]);
    this.pitch = Math.asin(clamp(f[1], -1, 1));
    this.mode = 'fly';
  }
  toOrbit() {
    if (this.mode !== 'fly') return;
    const f = this.getForward();
    this.target = add(this.pos, scale(f, 24));
    const rel = sub(this.pos, this.target);
    this.radius = Math.hypot(rel[0], rel[1], rel[2]);
    this.polar = Math.acos(clamp(rel[1] / this.radius, -1, 1));
    this.azimuth = Math.atan2(rel[0], rel[2]);
    this.mode = 'orbit';
  }
  flyLook(dx, dy) {
    this.yaw -= dx * 0.0042;
    this.pitch = clamp(this.pitch - dy * 0.0042, -1.5, 1.5);
  }
  flyMove(keys, dt) {
    if (this.mode !== 'fly') return;
    const { forward, right } = this.getBasis();
    const speed = this.flySpeed * ((keys.has('ShiftLeft') || keys.has('ShiftRight')) ? 3.2 : 1);
    let move = [0, 0, 0];
    if (keys.has('KeyW') || keys.has('ArrowUp')) move = add(move, forward);
    if (keys.has('KeyS') || keys.has('ArrowDown')) move = sub(move, forward);
    if (keys.has('KeyD') || keys.has('ArrowRight')) move = add(move, right);
    if (keys.has('KeyA') || keys.has('ArrowLeft')) move = sub(move, right);
    if (keys.has('KeyE') || keys.has('Space')) move = add(move, [0, 1, 0]);
    if (keys.has('KeyQ') || keys.has('ControlLeft')) move = sub(move, [0, 1, 0]);
    if (move[0] || move[1] || move[2]) this.pos = add(this.pos, scale(norm(move), speed * dt));
  }
}

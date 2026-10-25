// Unreal-Engine-style editor viewport camera (perspective view).
//
// Pure logic: no DOM and no GPU, so it can be unit-tested in Node and reused by any engine
// front-end. Your input layer feeds it mouse deltas and key states; demo/input.js is the browser
// binding.
//
// Conventions:
// - The world is Y-up and right-handed; looking down +Z, the camera's right is -X.
// - Mouse deltas are in CSS pixels, with dy > 0 meaning the mouse moved DOWN (DOM convention).
//
// Mapping to the UE 5 "Viewport Controls" for a perspective view:
//   RMB drag            look()          rotate the view in place
//   LMB drag            moveTurn()      move forward/back on the ground plane + turn left/right
//   MMB or LMB+RMB drag pan()           move up/down (world) and left/right, no rotation
//   wheel               wheel()         move forward/back by increments
//   RMB + W A S D       update()        fly; Q/E down/up (world), R/F up/down (local), Z/C FOV
//   RMB/LMB + wheel     changeSpeed()   camera speed 1..8
//   Alt + LMB drag      orbit()         tumble around the pivot (point of interest)
//   Alt + RMB drag      dollyToPivot()  move toward / away from the pivot
//   Alt + MMB drag      track()         move in the screen plane
//   F                   focus()         frame a bounding sphere; it becomes the pivot
//   RMB released        endFlight()     the temporary Z/C zoom snaps back to the default FOV
import { v3, clamp } from '../src/math.js';

const DEG = Math.PI / 180;
const MAX_PITCH = 89 * DEG;

/** Fly speed in m/s for camera speed settings 1..8. UE also uses 8 steps; these suit a 100–200 m level. */
export const CAMERA_SPEEDS = [0.5, 1, 2, 4, 8, 16, 32, 64];

/**
 * @typedef {object} FlyInput  per-frame key state while RMB is held; every axis is in -1..1
 * @property {number} [forward]  W − S        (also Up/Down arrows, Numpad 8/2)
 * @property {number} [right]    D − A        (also Right/Left arrows, Numpad 6/4)
 * @property {number} [up]       E − Q        world up/down (also PageUp/PageDown, Numpad 9/7)
 * @property {number} [localUp]  R − F        camera-local up/down
 * @property {number} [zoom]     Z − C        positive = wider FOV (also Numpad 1/3)
 * @property {boolean} [boost]   Shift: × boostFactor (an extra; UE has no default boost key)
 */

const rotateY = (v, a) => {
  const c = Math.cos(a), s = Math.sin(a);
  return [v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c];
};
// Rodrigues rotation of v around the unit axis k by angle a
const rotateAxis = (v, k, a) => {
  const c = Math.cos(a), s = Math.sin(a);
  const kxv = v3.cross(k, v), kv = v3.dot(k, v);
  return [
    v[0] * c + kxv[0] * s + k[0] * kv * (1 - c),
    v[1] * c + kxv[1] * s + k[1] * kv * (1 - c),
    v[2] * c + kxv[2] * s + k[2] * kv * (1 - c),
  ];
};
const dirFrom = (yaw, pitch) => [Math.sin(yaw) * Math.cos(pitch), Math.sin(pitch), Math.cos(yaw) * Math.cos(pitch)];
const easeOut = (t) => 1 - (1 - t) ** 3;

export class EditorCamera {
  /**
   * @param {object} [o]
   * @param {number[]} [o.position]
   * @param {number[]} [o.target]     initial look-at point
   * @param {number} [o.fov]          default vertical FOV (radians)
   * @param {number} [o.speedSetting] 1..8
   * @param {(x:number, z:number) => number} [o.floor]  lowest allowed eye height (collision), optional
   * @param {{min:number[], max:number[]}} [o.bounds]    optional box the camera is kept inside
   */
  constructor({ position = [0, 10, 0], target = null, fov = 62 * DEG, speedSetting = 5, floor = null, bounds = null } = {}) {
    this.position = position.slice();
    this.yaw = 0;          // 0 looks down +Z; positive turns towards +X (to the left)
    this.pitch = 0;        // positive looks up
    this.baseFov = fov;
    this.fov = fov;
    this.speedSetting = speedSetting;
    this.speedScalar = 1;          // UE "Speed Scalar"
    this.boostFactor = 3;          // Shift while flying
    this.lookSensitivity = 0.0032; // rad per pixel (≈ 0.18°/px)
    this.invertY = false;          // UE "Invert Mouse Look Y Axis"
    this.acceleration = 14;        // 1/s: fly velocity smoothing (0 = instant start/stop)
    this.dragFactor = 0.004;       // LMB/MMB drag: metres per pixel per (m/s of camera speed)
    this.wheelStep = 0.35;         // wheel: metres per notch per (m/s of camera speed)
    this.zoomRate = 45 * DEG;      // Z / C: FOV change per second
    this.minFov = 8 * DEG;
    this.maxFov = 125 * DEG;
    this.minPivotDistance = 0.4;
    this.defaultPivotDistance = 12;
    this.floor = floor;
    this.bounds = bounds;
    /** Orbit pivot (point of interest) in world space, or null. Flying and panning carry it along. */
    this.pivot = null;
    this.velocity = [0, 0, 0];
    this._pending = [0, 0, 0];     // wheel dolly still to be applied (smoothed)
    this._focus = null;            // running focus transition
    if (target) this.setLookAt(this.position, target);
  }

  /** Fly speed in m/s for the current setting. */
  get speed() { return CAMERA_SPEEDS[clamp(Math.round(this.speedSetting), 1, CAMERA_SPEEDS.length) - 1] * this.speedScalar; }
  get focusing() { return this._focus !== null; }

  forward() { return dirFrom(this.yaw, this.pitch); }
  forwardFlat() { return [Math.sin(this.yaw), 0, Math.cos(this.yaw)]; }
  right() { return [-Math.cos(this.yaw), 0, Math.sin(this.yaw)]; }
  up() { return v3.cross(this.right(), this.forward()); }
  /** A point `dist` metres along the view direction. */
  target(dist = 1) { return v3.madd(this.position, this.forward(), dist); }

  setLookAt(position, target) {
    this.position = position.slice();
    const d = v3.sub(target, position), l = v3.len(d);
    if (l > 1e-6) {
      this.yaw = Math.atan2(d[0], d[2]);
      this.pitch = clamp(Math.asin(clamp(d[1] / l, -1, 1)), -MAX_PITCH, MAX_PITCH);
    }
    this.velocity = [0, 0, 0];
    this._pending = [0, 0, 0];
    this._focus = null;
    this._constrain();
  }

  setPivot(p) { this.pivot = p ? p.slice() : null; }

  // ---- mouse gestures ------------------------------------------------------------------------

  /** RMB drag: rotate the view in place. */
  look(dx, dy) {
    this._cancelFocus();
    const d = this._pivotDistance();
    this.yaw -= dx * this.lookSensitivity;
    this.pitch = clamp(this.pitch - dy * this.lookSensitivity * (this.invertY ? -1 : 1), -MAX_PITCH, MAX_PITCH);
    this._repivot(d);
  }

  /** LMB drag: push the mouse forward to move forward along the ground plane; move it sideways to turn. */
  moveTurn(dx, dy) {
    this._cancelFocus();
    const d = this._pivotDistance();
    this.yaw -= dx * this.lookSensitivity;
    this._repivot(d);
    this._move(v3.scale(this.forwardFlat(), -dy * this.dragFactor * this.speed));
  }

  /** MMB drag (or LMB+RMB drag): move left/right and up/down without rotating. The camera follows the mouse. */
  pan(dx, dy) {
    this._cancelFocus();
    const k = this.dragFactor * this.speed;
    this._move(v3.add(v3.scale(this.right(), dx * k), [0, -dy * k, 0]));
  }

  /** Mouse wheel with no button held: move forward (notches > 0) or back along the view, smoothed. */
  wheel(notches) {
    this._cancelFocus();
    this._pending = v3.madd(this._pending, this.forward(), notches * this.wheelStep * this.speed);
  }

  /** RMB/LMB + wheel: step the camera speed. Returns the new setting (1..8). */
  changeSpeed(steps) {
    this.speedSetting = clamp(Math.round(this.speedSetting + steps), 1, CAMERA_SPEEDS.length);
    return this.speedSetting;
  }

  /** Alt+LMB drag: tumble around the pivot. The pivot keeps its place on screen. */
  orbit(dx, dy) {
    this._cancelFocus();
    const p = this._ensurePivot();
    const dYaw = -dx * this.lookSensitivity;
    const pitch = clamp(this.pitch - dy * this.lookSensitivity * (this.invertY ? -1 : 1), -MAX_PITCH, MAX_PITCH);
    let off = v3.sub(this.position, p);
    off = rotateAxis(off, this.right(), pitch - this.pitch);
    off = rotateY(off, dYaw);
    this.yaw += dYaw;
    this.pitch = pitch;
    this.position = v3.add(p, off);
    this._constrain();
  }

  /**
   * Alt+RMB drag: move toward the pivot (drag right or up) or away from it (left or down). The
   * motion is exponential, so the camera slows down near the pivot and never passes through it.
   */
  dollyToPivot(dx, dy) {
    this._cancelFocus();
    const p = this._ensurePivot();
    const off = v3.sub(this.position, p);
    const d = v3.len(off) || 1e-3;
    const nd = clamp(d * Math.exp(-(dx - dy) * 0.005), Math.min(d, this.minPivotDistance), 5000);
    this.position = v3.add(p, v3.scale(off, nd / d));
    this._constrain();
  }

  /**
   * Alt+MMB drag: track left/right/up/down in the screen plane, in the direction of the mouse.
   * The motion is scaled so that the pivot plane moves one pixel per pixel. The pivot travels along.
   */
  track(dx, dy, viewportHeight = 800) {
    this._cancelFocus();
    const p = this._ensurePivot();
    const depth = Math.max(0.5, v3.dot(v3.sub(p, this.position), this.forward()));
    const k = (2 * depth * Math.tan(this.fov / 2)) / Math.max(1, viewportHeight);
    this._move(v3.add(v3.scale(this.right(), dx * k), v3.scale(this.up(), -dy * k)));
  }

  /**
   * F: frame a bounding sphere. The camera keeps its heading, tilts to look down on the target
   * by 12–70° and backs off until the sphere fits the view. The sphere centre becomes the pivot.
   * `center` may be a function: a moving target is then tracked while the transition plays.
   */
  focus(center, radius = 1, { aspect = 16 / 9, duration = 0.35 } = {}) {
    const getC = typeof center === 'function' ? center : () => center;
    const half = Math.min(this.fov, 2 * Math.atan(Math.tan(this.fov / 2) * aspect)) / 2;
    const dist = Math.max(1.5, (radius / Math.sin(half)) * 1.1);
    const pitch = clamp(this.pitch, -70 * DEG, -12 * DEG);
    this.velocity = [0, 0, 0];
    this._pending = [0, 0, 0];
    this._focus = { getC, dist, pitch, from: { pos: this.position.slice(), pitch: this.pitch }, t: 0, duration };
    this.pivot = getC().slice();
    if (!(duration > 0)) this._stepFocus(Infinity);
  }

  /** Call when RMB is released: the temporary Z/C zoom snaps back to the default FOV. */
  endFlight() { this.fov = this.baseFov; }

  // ---- per frame -------------------------------------------------------------------------------

  /**
   * Advance the camera by dt seconds.
   * @param {number} dt
   * @param {FlyInput|null} [fly]  key state while flying (RMB held), or null
   */
  update(dt, fly = null) {
    dt = clamp(dt, 0, 0.1);
    const i = fly ?? {};
    if (this._focus) this._stepFocus(dt);

    // fly (RMB + WASD/QE/RF)
    let dir = [0, 0, 0];
    dir = v3.madd(dir, this.forward(), i.forward || 0);
    dir = v3.madd(dir, this.right(), i.right || 0);
    dir = v3.madd(dir, [0, 1, 0], i.up || 0);
    dir = v3.madd(dir, this.up(), i.localUp || 0);
    const l = v3.len(dir);
    if (l > 1) dir = v3.scale(dir, 1 / l);
    if (l > 1e-6) this._cancelFocus();
    const want = v3.scale(dir, this.speed * (i.boost ? this.boostFactor : 1));
    const a = this.acceleration > 0 ? 1 - Math.exp(-dt * this.acceleration) : 1;
    this.velocity = v3.lerp(this.velocity, want, a);
    if (l < 1e-6 && v3.len(this.velocity) < 0.01 * this.speed + 1e-3) this.velocity = [0, 0, 0];
    let move = v3.scale(this.velocity, dt);

    // smoothed wheel dolly
    if (this._pending[0] || this._pending[1] || this._pending[2]) {
      const b = 1 - Math.exp(-dt * 16);
      let step = v3.scale(this._pending, b);
      if (v3.len(this._pending) * (1 - b) < 1e-3) step = this._pending;
      this._pending = v3.sub(this._pending, step);
      move = v3.add(move, step);
    }
    if (move[0] || move[1] || move[2]) this._move(move);

    // Z / C zoom (held FOV until RMB is released → endFlight)
    if (i.zoom) this.fov = clamp(this.fov + i.zoom * this.zoomRate * dt, this.minFov, this.maxFov);
  }

  // ---- internals -------------------------------------------------------------------------------

  _stepFocus(dt) {
    const f = this._focus;
    f.t = Math.min(1, f.t + dt / Math.max(1e-6, f.duration));
    const c = f.getC();
    const end = v3.madd(c, dirFrom(this.yaw, f.pitch), -f.dist);
    const e = easeOut(f.t);
    this.position = v3.lerp(f.from.pos, end, e);
    this.pitch = f.from.pitch + (f.pitch - f.from.pitch) * e;
    this.pivot = c.slice();
    this._constrain();
    if (f.t >= 1) this._focus = null;
  }

  _cancelFocus() { this._focus = null; }

  _pivotDistance() {
    return this.pivot ? Math.max(this.minPivotDistance, v3.len(v3.sub(this.pivot, this.position))) : this.defaultPivotDistance;
  }

  // after a rotation in place the point of interest moves back in front of the camera (as in UE)
  _repivot(d) { if (this.pivot) this.pivot = this.target(d); }

  _ensurePivot() {
    if (!this.pivot) this.pivot = this.target(this.defaultPivotDistance);
    return this.pivot;
  }

  _move(delta) {
    const before = this.position.slice();
    this.position = v3.add(this.position, delta);
    this._constrain();
    if (this.pivot) this.pivot = v3.add(this.pivot, v3.sub(this.position, before));
  }

  _constrain() {
    const p = this.position;
    if (this.bounds) for (let k = 0; k < 3; k++) p[k] = clamp(p[k], this.bounds.min[k], this.bounds.max[k]);
    if (this.floor) {
      const m = this.floor(p[0], p[2]);
      if (p[1] < m) p[1] = m;
    }
  }
}

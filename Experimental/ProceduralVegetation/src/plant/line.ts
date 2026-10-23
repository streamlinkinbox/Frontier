/**
 * Centrelines for grass organs: a polyline with a transported frame, grown by
 * a turtle that pitches (about its own width axis), droops (about the
 * horizontal, i.e. under gravity) and yaws (about its normal).
 */

import { V3, UP, addScaled, anyPerpendicular, clone, cross, length, lengthSq, lerp, normalize, projectOnPlane, rotateAxis, sub } from '../core/math';

export interface Frame {
  pos: V3;
  dir: V3;
  /** Width axis of a blade / reference direction of a round organ. Perpendicular to `dir`. */
  right: V3;
  /** cross(dir, right): the adaxial (upper-face) normal of a blade. */
  up: V3;
}

const frame = (pos: V3, dir: V3, right: V3): Frame => ({ pos, dir, right, up: cross(dir, right) });

export class Line {
  readonly pts: V3[] = [];
  readonly dirs: V3[] = [];
  readonly rights: V3[] = [];
  readonly ss: number[] = [];

  get length(): number {
    return this.ss.length ? this.ss[this.ss.length - 1] : 0;
  }

  push(p: V3, d: V3, r: V3): void {
    const n = this.pts.length;
    this.ss.push(n === 0 ? 0 : this.ss[n - 1] + length(sub(p, this.pts[n - 1])));
    this.pts.push(p);
    this.dirs.push(d);
    this.rights.push(r);
  }

  /** Frame at arc length `s` (clamped). */
  at(s: number): Frame {
    const n = this.pts.length;
    if (s <= 0 || n === 1) return frame(clone(this.pts[0]), clone(this.dirs[0]), clone(this.rights[0]));
    if (s >= this.length) return frame(clone(this.pts[n - 1]), clone(this.dirs[n - 1]), clone(this.rights[n - 1]));
    let lo = 0;
    let hi = n - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (this.ss[mid] <= s) lo = mid;
      else hi = mid;
    }
    const t = (s - this.ss[lo]) / Math.max(1e-12, this.ss[hi] - this.ss[lo]);
    const pos = lerp(this.pts[lo], this.pts[hi], t);
    const dir = normalize(lerp(this.dirs[lo], this.dirs[hi], t));
    let right = projectOnPlane(lerp(this.rights[lo], this.rights[hi], t), dir);
    right = lengthSq(right) < 1e-12 ? anyPerpendicular(dir) : normalize(right);
    return frame(pos, dir, right);
  }
}

export interface Turn {
  /** Rotation about the width axis, radians; positive bends towards −up (a blade curling over). */
  pitch?: number;
  /** Rotation about the horizontal axis perpendicular to the heading, radians; positive droops under gravity. */
  gravity?: number;
  /** Rotation about the normal, radians (lateral waviness). */
  yaw?: number;
  /** Rotation of the width axis about the heading, radians (twist). */
  roll?: number;
}

/**
 * Grow a polyline of `steps` equal steps from `pos` along `dir`. `turn` is
 * asked for the rotation to apply over each step, given the normalised
 * positions of its start and end.
 */
export function growLine(pos: V3, dir: V3, right: V3, L: number, steps: number, turn: (t0: number, t1: number) => Turn): Line {
  const line = new Line();
  let p = clone(pos);
  let d = normalize(dir);
  let r = projectOnPlane(right, d);
  r = lengthSq(r) < 1e-12 ? anyPerpendicular(d) : normalize(r);
  line.push(p, d, r);
  const n = Math.max(1, Math.round(steps));
  const ds = L / n;
  for (let k = 0; k < n; k++) {
    const tr = turn(k / n, (k + 1) / n);
    if (tr.pitch) d = rotateAxis(d, r, tr.pitch);
    if (tr.gravity) {
      let h = cross(UP, d);
      h = lengthSq(h) < 1e-8 ? r : normalize(h);
      d = rotateAxis(d, h, tr.gravity);
      r = rotateAxis(r, h, tr.gravity);
    }
    if (tr.yaw) {
      const u = cross(d, r);
      d = rotateAxis(d, u, tr.yaw);
      r = rotateAxis(r, u, tr.yaw);
    }
    if (tr.roll) r = rotateAxis(r, d, tr.roll);
    d = normalize(d);
    r = projectOnPlane(r, d);
    r = lengthSq(r) < 1e-12 ? anyPerpendicular(d) : normalize(r);
    p = addScaled(p, d, ds);
    line.push(p, d, r);
  }
  return line;
}

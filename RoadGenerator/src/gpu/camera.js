// Orbit / fly camera producing the matrices the renderer needs, plus screen→world rays.
import { mat4, add, sub, mul, madd, norm, cross, clamp } from "../core/math.js";

export class Camera {
  constructor() {
    this.target = [0, 0, 0];
    this.distance = 120;
    this.yaw = -0.6;      // radians around Y
    this.pitch = 0.62;    // radians above the horizon
    this.fov = 50 * Math.PI / 180;
    this.near = 0.3;
    this.far = 2500;
    this.aspect = 1.6;
    this.position = [0, 0, 0];
    this.view = mat4.identity(); this.proj = mat4.identity(); this.viewProj = mat4.identity();
    this.update();
  }
  forward() { return [-Math.sin(this.yaw) * Math.cos(this.pitch), -Math.sin(this.pitch), -Math.cos(this.yaw) * Math.cos(this.pitch)]; }
  right() { return [Math.cos(this.yaw), 0, -Math.sin(this.yaw)]; }
  update(aspect = this.aspect) {
    this.aspect = aspect;
    this.pitch = clamp(this.pitch, -0.05, Math.PI / 2 - 0.01);
    this.distance = clamp(this.distance, 1.5, 3000);
    const f = this.forward();
    this.position = madd(this.target, f, -this.distance);
    if (this.position[1] < 0.4 && !this.allowUnderground) this.position[1] = 0.4;
    this.view = mat4.lookAt(this.position, this.target, [0, 1, 0]);
    this.near = Math.max(0.2, this.distance * 0.002);
    this.proj = mat4.perspective(this.fov, aspect, this.near, this.far);
    this.viewProj = mat4.multiply(this.proj, this.view);
  }
  orbit(dx, dy) { this.yaw -= dx * 0.005; this.pitch += dy * 0.005; }
  pan(dx, dy) {
    const s = this.distance * 0.0012;
    const r = this.right(), f = this.forward();
    const fl = norm([f[0], 0, f[2]]);
    this.target = add(this.target, add(mul(r, -dx * s), mul(fl, dy * s)));
  }
  dolly(delta, towards = null) {
    const k = Math.exp(delta * 0.0012);
    if (towards) { // zoom toward a world point: move target along the ray
      const t = 1 - 1 / k; if (k < 1) this.target = add(this.target, mul(sub(towards, this.target), t * 0.5));
    }
    this.distance *= k;
  }
  fly(dirLocal, dt, fast) {
    const sp = (fast ? 4 : 1) * Math.max(4, this.distance * 0.6) * dt;
    const r = this.right(), f = this.forward();
    const fl = norm([f[0], 0, f[2]]);
    const mv = add(add(mul(fl, dirLocal[2] * sp), mul(r, dirLocal[0] * sp)), [0, dirLocal[1] * sp, 0]);
    this.target = add(this.target, mv);
  }
  /** Ray through CSS pixel (x,y) of an element width w / height h. */
  ray(x, y, w, h) {
    const nx = (x / w) * 2 - 1, ny = 1 - (y / h) * 2;
    const inv = mat4.invert(this.viewProj);
    const p0 = mat4.transformPoint(inv, [nx, ny, 0]), p1 = mat4.transformPoint(inv, [nx, ny, 1]);
    return { o: p0, d: norm(sub(p1, p0)) };
  }
  /** Intersect a ray with the horizontal plane y = h. */
  static hitPlane(ray, h = 0) {
    if (Math.abs(ray.d[1]) < 1e-6) return null;
    const t = (h - ray.o[1]) / ray.d[1];
    if (t < 0) return null;
    return madd(ray.o, ray.d, t);
  }
  project(p, w, h) {
    const c = mat4.transformPoint(this.viewProj, p);
    return [((c[0] + 1) / 2) * w, ((1 - c[1]) / 2) * h, c[2]];
  }
  frame(bounds) {
    if (!bounds) return;
    const c = [(bounds.min[0] + bounds.max[0]) / 2, (bounds.min[1] + bounds.max[1]) / 2, (bounds.min[2] + bounds.max[2]) / 2];
    const r = Math.hypot(bounds.max[0] - bounds.min[0], bounds.max[2] - bounds.min[2]) / 2;
    this.target = c; this.distance = Math.max(20, r / Math.tan(this.fov / 2) * 0.9);
  }
}

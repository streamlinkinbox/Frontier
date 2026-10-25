// Orbit/follow camera + planar-reflection matrix helpers.
import { mat4LookAt, mat4Multiply, mat4Perspective, mat4Identity, type Mat4, type Vec3 } from './math';

export class camera {
  target: Vec3 = [0, 1, 0];
  yaw = 0.7;
  pitch = 0.42;
  distance = 38;
  fovY = (55 * Math.PI) / 180;
  near = 0.15;
  far = 1200;

  eye(): Vec3 {
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
    return [
      this.target[0] + this.distance * cp * Math.sin(this.yaw),
      this.target[1] + this.distance * sp,
      this.target[2] + this.distance * cp * Math.cos(this.yaw),
    ];
  }

  view(): Mat4 {
    return mat4LookAt(this.eye(), this.target, [0, 1, 0]);
  }

  proj(aspect: number): Mat4 {
    return mat4Perspective(this.fovY, aspect, this.near, this.far);
  }

  viewProj(aspect: number): Mat4 {
    return mat4Multiply(this.proj(aspect), this.view());
  }

  forward(): Vec3 {
    const e = this.eye();
    const d: Vec3 = [this.target[0] - e[0], this.target[1] - e[1], this.target[2] - e[2]];
    const l = Math.hypot(d[0], d[1], d[2]) || 1;
    return [d[0] / l, d[1] / l, d[2] / l];
  }
}

/** Reflection matrix across the horizontal plane y = level (reflects geometry). */
export function reflectionMatrix(level: number): Mat4 {
  const m = mat4Identity();
  m[5] = -1;
  m[13] = 2 * level;
  return m;
}

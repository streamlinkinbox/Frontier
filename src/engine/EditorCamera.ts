import { add, clamp, cross, normalize, scale, sub } from "./math";
import type { Vec3 } from "./types";

export const FLY_KEYS = new Set([
  "KeyW",
  "KeyA",
  "KeyS",
  "KeyD",
  "KeyQ",
  "KeyE",
]);
export type NavigationMode = "orbit" | "fly";

/** Unreal-style, Y-up editor navigation. It is a noclip inspection camera,
 * not a walking/heightfield controller. Motion is independent of render FPS. */
export class EditorCamera {
  mode: NavigationMode = "orbit";
  speed = 10;
  yaw = 0.4;
  pitch = 0.61;
  distance = 151;
  target: Vec3 = [0, 7, 0];
  position: Vec3 = [0, 0, 0];
  private keys = new Set<string>();
  private fit(aspect: number) {
    return Math.max(1, 1.18 / Math.max(aspect, 0.01));
  }

  basis(aspect: number) {
    const offset: Vec3 = [
      Math.sin(this.yaw) * Math.cos(this.pitch),
      Math.sin(this.pitch),
      Math.cos(this.yaw) * Math.cos(this.pitch),
    ];
    const eye =
      this.mode === "fly"
        ? ([...this.position] as Vec3)
        : add(this.target, scale(offset, this.distance * this.fit(aspect)));
    const forward = scale(offset, -1),
      right = normalize(cross(forward, [0, 1, 0])),
      up = cross(right, forward);
    return { eye, forward, right, up };
  }
  enterFly(aspect: number) {
    if (this.mode === "fly") return;
    this.position = this.basis(aspect).eye;
    this.mode = "fly";
  }
  enterOrbit(aspect: number) {
    if (this.mode === "orbit") return;
    const view = this.basis(aspect);
    const focusDistance = 12;
    this.target = add(view.eye, scale(view.forward, focusDistance));
    this.distance = focusDistance / this.fit(aspect);
    this.mode = "orbit";
    this.clearKeys();
  }
  look(dx: number, dy: number, aspect: number) {
    this.enterFly(aspect);
    this.yaw -= dx * 0.004;
    this.pitch = clamp(this.pitch + dy * 0.004, -1.54, 1.54);
  }
  orbit(dx: number, dy: number, aspect: number) {
    this.enterOrbit(aspect);
    this.yaw -= dx * 0.006;
    this.pitch = clamp(this.pitch + dy * 0.005, -1.48, 1.48);
  }
  pan(dx: number, dy: number, aspect: number) {
    const view = this.basis(aspect),
      factor = (this.mode === "fly" ? 12 : this.distance) * 0.0012;
    const delta = add(
      scale(view.right, -dx * factor),
      scale(view.up, dy * factor),
    );
    if (this.mode === "fly") this.position = add(this.position, delta);
    else this.target = add(this.target, delta);
  }
  wheel(delta: number, looking: boolean, aspect: number) {
    if (looking) {
      this.speed = clamp(this.speed * Math.exp(-delta * 0.0015), 0.25, 80);
      return;
    }
    if (this.mode === "fly") {
      this.position = add(
        this.position,
        scale(
          this.basis(aspect).forward,
          -delta * Math.max(0.008, this.speed * 0.012),
        ),
      );
    } else
      this.distance = clamp(this.distance * Math.exp(delta * 0.001), 1, 350);
  }
  key(code: string, pressed: boolean, aspect: number) {
    if (pressed && FLY_KEYS.has(code)) this.enterFly(aspect);
    if (pressed) this.keys.add(code);
    else this.keys.delete(code);
  }
  get moving() {
    return [...this.keys].some((key) => FLY_KEYS.has(key));
  }
  clearKeys() {
    this.keys.clear();
  }
  update(dt: number, aspect: number) {
    if (!this.moving || this.mode !== "fly") return;
    const view = this.basis(aspect),
      down = (key: string) => (this.keys.has(key) ? 1 : 0);
    const direction = add(
      add(
        scale(view.forward, down("KeyW") - down("KeyS")),
        scale(view.right, down("KeyD") - down("KeyA")),
      ),
      [0, down("KeyE") - down("KeyQ"), 0],
    );
    const boost =
      this.keys.has("ShiftLeft") || this.keys.has("ShiftRight") ? 3 : 1;
    this.position = add(
      this.position,
      scale(normalize(direction), this.speed * boost * clamp(dt, 0, 0.1)),
    );
  }
  /** Useful for a chosen inspection viewpoint; no terrain regeneration. */
  lookAt(eye: Vec3, target: Vec3) {
    const backwards = normalize(sub(eye, target));
    this.position = [...eye];
    this.yaw = Math.atan2(backwards[0], backwards[2]);
    this.pitch = Math.asin(clamp(backwards[1], -0.9995, 0.9995));
    this.mode = "fly";
    this.clearKeys();
  }
  frame(top = false) {
    this.mode = "orbit";
    this.yaw = top ? 0 : 0.4;
    this.pitch = top ? 1.48 : 0.61;
    this.distance = top ? 142 : 151;
    this.target = [0, 7, 0];
    this.clearKeys();
  }
  focusDistance(aspect: number) {
    return this.mode === "fly" ? 12 : this.distance * this.fit(aspect);
  }
}

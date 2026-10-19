import * as THREE from "three";

/** Unreal-style editor navigation. No pointer lock, and no global WASD hijacking. */
export class FlyCamera {
  constructor(world) {
    this.world = world;
    this.enabled = false;
    this.active = false;
    this.keys = new Set();
    this.speed = 25;
    this.abort = new AbortController();
    const signal = this.abort.signal,
      canvas = world.renderer.domElement;
    const on = (target, type, fn, options = {}) =>
      target.addEventListener(type, fn, { ...options, signal });
    const editable = () =>
      document.activeElement?.matches(
        'input,textarea,select,[contenteditable="true"]',
      ) || document.querySelector('[aria-modal="true"]');
    on(
      canvas,
      "pointerdown",
      (e) => {
        if (
          !this.enabled ||
          editable() ||
          !(
            e.button === 2 ||
            e.button === 1 ||
            (e.button === 0 && world.tool === "pan")
          )
        )
          return;
        e.preventDefault();
        e.stopImmediatePropagation();
        canvas.focus({ preventScroll: true });
        this.active = true;
        this.pan = e.button !== 2;
        this.pointer = e.pointerId;
        this.last = { x: e.clientX, y: e.clientY };
        this.euler = new THREE.Euler().setFromQuaternion(
          world.camera.quaternion,
          "YXZ",
        );
        canvas.setPointerCapture(e.pointerId);
        canvas.style.cursor = this.pan ? "grabbing" : "none";
        world.callbacks.onChange?.();
      },
      { capture: true },
    );
    on(
      canvas,
      "pointermove",
      (e) => {
        if (!this.active) return;
        e.preventDefault();
        e.stopImmediatePropagation();
        const dx = e.clientX - this.last.x,
          dy = e.clientY - this.last.y;
        this.last = { x: e.clientX, y: e.clientY };
        if (this.pan) {
          world.camera.translateX(-dx * this.speed * 0.003);
          world.camera.translateY(dy * this.speed * 0.003);
        } else {
          this.euler.y -= dx * 0.0025;
          this.euler.x = THREE.MathUtils.clamp(
            this.euler.x - dy * 0.0025,
            -Math.PI * 0.494,
            Math.PI * 0.494,
          );
          world.camera.quaternion.setFromEuler(this.euler);
        }
        world.camera.updateMatrixWorld(true);
        world.needsRender = true;
      },
      { capture: true },
    );
    on(
      canvas,
      "pointerup",
      (e) => {
        if (!this.active) return;
        e.preventDefault();
        e.stopImmediatePropagation();
        this.stop();
      },
      { capture: true },
    );
    on(canvas, "pointercancel", () => this.stop());
    on(canvas, "lostpointercapture", () => this.stop());
    on(canvas, "contextmenu", (e) => e.preventDefault());
    on(
      canvas,
      "wheel",
      (e) => {
        if (!this.enabled || editable()) return;
        e.preventDefault();
        e.stopImmediatePropagation();
        if (this.active && !this.pan)
          this.setSpeed(this.speed * (e.deltaY < 0 ? 1.25 : 0.8));
        else {
          const dir = world.camera.getWorldDirection(new THREE.Vector3());
          world.camera.position.addScaledVector(
            dir,
            -Math.sign(e.deltaY) * this.speed * 0.3,
          );
          world.camera.updateMatrixWorld(true);
          world.needsRender = true;
        }
      },
      { capture: true, passive: false },
    );
    on(
      window,
      "keydown",
      (e) => {
        if (!this.active || editable()) return;
        if (e.code === "Escape") {
          this.stop();
          return;
        }
        if (
          [
            "KeyW",
            "KeyA",
            "KeyS",
            "KeyD",
            "KeyQ",
            "KeyE",
            "ShiftLeft",
            "ShiftRight",
          ].includes(e.code) &&
          !e.ctrlKey &&
          !e.metaKey &&
          !e.altKey
        ) {
          e.preventDefault();
          e.stopImmediatePropagation();
          this.keys.add(e.code);
        }
      },
      { capture: true },
    );
    on(window, "keyup", (e) => this.keys.delete(e.code), { capture: true });
    on(window, "blur", () => this.stop());
    on(document, "visibilitychange", () => {
      if (document.hidden) this.stop();
    });
  }
  setSpeed(speed) {
    this.speed = THREE.MathUtils.clamp(speed, 1, 200);
    this.world.callbacks.onChange?.();
  }
  stop() {
    this.active = false;
    this.keys.clear();
    const c = this.world.renderer.domElement;
    c.style.cursor = "";
    if (this.pointer !== undefined && c.hasPointerCapture(this.pointer))
      c.releasePointerCapture(this.pointer);
    this.pointer = undefined;
    this.world.callbacks.onChange?.();
  }
  update(dt) {
    if (!this.enabled || !this.active || this.pan || !this.keys.size)
      return false;
    const c = this.world.camera,
      forward = c.getWorldDirection(new THREE.Vector3()),
      right = new THREE.Vector3(1, 0, 0).applyQuaternion(c.quaternion),
      delta = new THREE.Vector3();
    for (const [key, direction, sign] of [
      ["KeyW", forward, 1],
      ["KeyS", forward, -1],
      ["KeyD", right, 1],
      ["KeyA", right, -1],
      ["KeyE", new THREE.Vector3(0, 1, 0), 1],
      ["KeyQ", new THREE.Vector3(0, 1, 0), -1],
    ])
      if (this.keys.has(key)) delta.addScaledVector(direction, sign);
    if (delta.lengthSq() === 0) return false;
    const fast = this.keys.has("ShiftLeft") || this.keys.has("ShiftRight");
    c.position.addScaledVector(
      delta.normalize(),
      this.speed * (fast ? 4 : 1) * Math.min(dt, 0.05),
    );
    c.updateMatrixWorld(true);
    this.world.needsRender = true;
    return true;
  }
  destroy() {
    this.stop();
    this.abort.abort();
  }
}

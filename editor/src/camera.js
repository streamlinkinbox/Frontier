// Unreal-style fly camera: WASD to move (relative to view), Q/E down/up,
// hold right mouse to look around, Shift to boost, wheel to change base speed.
import * as THREE from 'three';

export class FlyCamera {
  constructor(camera, dom) {
    this.camera = camera;
    this.dom = dom;
    this.yaw = Math.PI; // look towards -z by default
    this.pitch = -0.45;
    this.speed = 120;
    this.keys = new Set();
    this.looking = false;
    this.position = new THREE.Vector3(0, 220, 320);
    this.onKeyUp = null;
    window.addEventListener('keydown', (e) => {
      if (isTyping(e)) return;
      this.keys.add(e.code);
      if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'Space'].includes(e.code)) e.preventDefault();
    });
    window.addEventListener('keyup', (e) => this.keys.delete(e.code));
    window.addEventListener('blur', () => this.keys.clear());
    dom.addEventListener('contextmenu', (e) => e.preventDefault());
    dom.addEventListener('mousedown', (e) => {
      if (e.button === 2) this.looking = true;
    });
    window.addEventListener('mouseup', (e) => {
      if (e.button === 2) this.looking = false;
    });
    window.addEventListener('mousemove', (e) => {
      if (!this.looking) return;
      this.yaw -= e.movementX * 0.0025;
      this.pitch = THREE.MathUtils.clamp(this.pitch - e.movementY * 0.0025, -1.5, 1.5);
    });
    dom.addEventListener(
      'wheel',
      (e) => {
        this.speed = THREE.MathUtils.clamp(this.speed * (e.deltaY < 0 ? 1.15 : 1 / 1.15), 5, 2000);
        e.preventDefault();
      },
      { passive: false },
    );
    this.apply();
  }

  apply() {
    this.camera.position.copy(this.position);
    this.camera.rotation.set(this.pitch, this.yaw, 0, 'YXZ');
  }

  update(dt) {
    const k = this.keys;
    const boost = k.has('ShiftLeft') || k.has('ShiftRight') ? 3 : 1;
    const v = this.speed * boost * dt;
    const fx = -Math.sin(this.yaw);
    const fz = -Math.cos(this.yaw);
    const rx = Math.cos(this.yaw);
    const rz = -Math.sin(this.yaw);
    let mx = 0, my = 0, mz = 0;
    if (k.has('KeyW')) { mx += fx; mz += fz; }
    if (k.has('KeyS')) { mx -= fx; mz -= fz; }
    if (k.has('KeyD')) { mx += rx; mz += rz; }
    if (k.has('KeyA')) { mx -= rx; mz -= rz; }
    if (k.has('KeyE') || k.has('Space')) my += 1;
    if (k.has('KeyQ') || k.has('KeyC')) my -= 1;
    const l = Math.hypot(mx, my, mz);
    if (l > 0) {
      this.position.x += (mx / l) * v;
      this.position.y += (my / l) * v;
      this.position.z += (mz / l) * v;
    }
    this.position.y = Math.max(1.5, this.position.y);
    this.apply();
  }
}

function isTyping(e) {
  const t = e.target;
  return t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA');
}

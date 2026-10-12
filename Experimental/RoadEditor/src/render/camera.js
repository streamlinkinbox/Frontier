// Unreal Engine–style viewport camera.
//  RMB + mouse      : look around          RMB + WASD/QE : fly (Q down, E up)
//  RMB + wheel      : change fly speed     Shift         : boost ×4, Ctrl/Alt slow ×0.25 (while flying)
//  MMB drag         : pan (truck/pedestal) Alt + LMB     : orbit around point under cursor
//  Alt + RMB        : dolly                Wheel         : dolly toward cursor
//  WASD/arrow keys also fly when the viewport has focus (no RMB required).
import * as THREE from 'three';

export const SPEED_STEPS = [4, 10, 25, 50, 90, 150, 260, 420];

export class CameraController {
  constructor(camera, dom) {
    this.camera = camera; this.dom = dom;
    this.yaw = -0.6; this.pitch = -0.45;
    this.vel = new THREE.Vector3();
    this.keys = new Set();
    this.speedIndex = 3;
    this.mode = null;             // 'look' | 'pan' | 'orbit' | 'dolly'
    this.pivot = new THREE.Vector3();
    this.enabled = true;
    this.pickPoint = null;        // fn(clientX, clientY) -> Vector3 | null (set by world/editor)
    this.onChange = null;
    this.minY = 1.2;
    this.flyTo = null;
    this._down = this.onDown.bind(this); this._move = this.onMove.bind(this); this._up = this.onUp.bind(this);
    this._wheel = this.onWheel.bind(this); this._kd = this.onKey.bind(this, true); this._ku = this.onKey.bind(this, false);
    this._ctx = e => e.preventDefault(); this._blur = () => this.keys.clear();
    dom.addEventListener('pointerdown', this._down);
    window.addEventListener('pointermove', this._move);
    window.addEventListener('pointerup', this._up);
    dom.addEventListener('wheel', this._wheel, { passive: false });
    dom.addEventListener('contextmenu', this._ctx);
    window.addEventListener('keydown', this._kd);
    window.addEventListener('keyup', this._ku);
    window.addEventListener('blur', this._blur);
    this.applyRotation();
  }
  get speed() { return SPEED_STEPS[this.speedIndex]; }
  setSpeedIndex(i) { this.speedIndex = THREE.MathUtils.clamp(i, 0, SPEED_STEPS.length - 1); this.onChange?.(); }

  applyRotation() {
    this.pitch = THREE.MathUtils.clamp(this.pitch, -1.55, 1.55);
    this.camera.quaternion.setFromEuler(new THREE.Euler(this.pitch, this.yaw, 0, 'YXZ'));
  }
  lookAt(target) {
    const d = new THREE.Vector3().subVectors(target, this.camera.position).normalize();
    this.yaw = Math.atan2(-d.x, -d.z); this.pitch = Math.asin(THREE.MathUtils.clamp(d.y, -1, 1));
    this.applyRotation();
  }
  focus(target, dist = 120) {
    const dir = new THREE.Vector3(); this.camera.getWorldDirection(dir);
    if (dir.y > -0.35) { dir.y = -0.55; dir.normalize(); }
    const to = target.clone().addScaledVector(dir, -dist);
    to.y = Math.max(to.y, target.y + 8);
    this.flyTo = { from: this.camera.position.clone(), to, target: target.clone(), t: 0 };
  }
  typing() { const a = document.activeElement; return a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' || a.tagName === 'SELECT' || a.isContentEditable); }

  onDown(e) {
    if (!this.enabled) return;
    this.dom.focus?.();
    this.lastX = e.clientX; this.lastY = e.clientY; this.moved = 0;
    if (e.button === 2) this.mode = e.altKey ? 'dolly' : 'look';
    else if (e.button === 1) { this.mode = 'pan'; e.preventDefault(); }
    else if (e.button === 0 && e.altKey) {
      this.mode = 'orbit';
      const p = this.pickPoint?.(e.clientX, e.clientY);
      if (p) this.pivot.copy(p); else { const d = new THREE.Vector3(); this.camera.getWorldDirection(d); this.pivot.copy(this.camera.position).addScaledVector(d, 150); }
    } else return;
    this.flyTo = null;
    this.dom.setPointerCapture?.(e.pointerId);
  }
  onMove(e) {
    if (!this.mode) return;
    const dx = e.clientX - this.lastX, dy = e.clientY - this.lastY;
    this.lastX = e.clientX; this.lastY = e.clientY; this.moved += Math.abs(dx) + Math.abs(dy);
    const h = Math.max(4, this.camera.position.y);
    if (this.mode === 'look') { this.yaw -= dx * 0.0032; this.pitch -= dy * 0.0032; this.applyRotation(); }
    else if (this.mode === 'pan') {
      const k = h * 0.0022 + 0.02;
      const right = new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion);
      const up = new THREE.Vector3(0, 1, 0).applyQuaternion(this.camera.quaternion);
      this.camera.position.addScaledVector(right, -dx * k).addScaledVector(up, dy * k);
    } else if (this.mode === 'orbit') {
      const off = new THREE.Vector3().subVectors(this.camera.position, this.pivot);
      const sph = new THREE.Spherical().setFromVector3(off);
      sph.theta -= dx * 0.005; sph.phi = THREE.MathUtils.clamp(sph.phi - dy * 0.005, 0.05, Math.PI / 2 - 0.02);
      off.setFromSpherical(sph);
      this.camera.position.copy(this.pivot).add(off);
      this.lookAt(this.pivot);
    } else if (this.mode === 'dolly') {
      const f = new THREE.Vector3(); this.camera.getWorldDirection(f);
      this.camera.position.addScaledVector(f, -dy * (h * 0.006 + 0.1));
    }
    this.clampY();
  }
  onUp(e) {
    if (!this.mode) return;
    this.mode = null; this.keys.clear();
    this.dom.releasePointerCapture?.(e.pointerId);
  }
  get looking() { return this.mode === 'look'; }
  onWheel(e) {
    if (!this.enabled) return;
    e.preventDefault();
    if (this.mode === 'look') { this.setSpeedIndex(this.speedIndex + (e.deltaY < 0 ? 1 : -1)); return; }
    this.flyTo = null;
    const h = Math.max(3, this.camera.position.y);
    const amt = -Math.sign(e.deltaY) * Math.min(Math.abs(e.deltaY), 120) / 120 * (h * 0.22 + 2);
    const p = this.pickPoint?.(e.clientX, e.clientY);
    const dir = p ? new THREE.Vector3().subVectors(p, this.camera.position) : new THREE.Vector3(); 
    if (p && dir.length() < amt + 2 && amt > 0) return;
    if (p) dir.normalize(); else this.camera.getWorldDirection(dir);
    this.camera.position.addScaledVector(dir, amt);
    this.clampY();
  }
  onKey(down, e) {
    if (this.typing()) return;
    const k = e.code;
    const fly = ['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'ShiftLeft', 'ShiftRight', 'ControlLeft', 'PageUp', 'PageDown', 'KeyZ', 'KeyC'];
    if (!fly.includes(k)) return;
    if (down && (e.ctrlKey || e.metaKey) && !this.mode && k !== 'ControlLeft') return; // leave Ctrl+Z/S etc. to the app
    if (down) this.keys.add(k); else this.keys.delete(k);
  }
  clampY() { if (this.camera.position.y < this.minY) this.camera.position.y = this.minY; }

  update(dt) {
    if (this.flyTo) {
      const F = this.flyTo; F.t = Math.min(1, F.t + dt * 1.8);
      const t = 1 - Math.pow(1 - F.t, 3);
      this.camera.position.lerpVectors(F.from, F.to, t);
      this.lookAt(F.target);
      if (F.t >= 1) this.flyTo = null;
      this.onChange?.();
      return;
    }
    const K = this.keys;
    const want = new THREE.Vector3();
    const flying = this.mode === 'look' || !this.mode;
    if (flying) {
      if (K.has('KeyW') || K.has('ArrowUp')) want.z -= 1;
      if (K.has('KeyS') || K.has('ArrowDown')) want.z += 1;
      if (K.has('KeyA') || K.has('ArrowLeft')) want.x -= 1;
      if (K.has('KeyD') || K.has('ArrowRight')) want.x += 1;
      if (K.has('KeyE') || K.has('PageUp') || K.has('KeyC')) want.y += 1;
      if (K.has('KeyQ') || K.has('PageDown') || K.has('KeyZ')) want.y -= 1;
    }
    let sp = this.speed;
    if (K.has('ShiftLeft') || K.has('ShiftRight')) sp *= 4;
    if (K.has('ControlLeft') && this.mode === 'look') sp *= 0.25;
    if (want.lengthSq() > 0) {
      want.normalize();
      const local = new THREE.Vector3(want.x, 0, want.z).applyQuaternion(this.camera.quaternion);
      local.y += want.y;
      want.copy(local).multiplyScalar(sp);
    }
    // critically damped approach to target velocity
    const a = 1 - Math.exp(-dt * 10);
    this.vel.lerp(want, a);
    if (this.vel.lengthSq() > 1e-4) { this.camera.position.addScaledVector(this.vel, dt); this.clampY(); this.onChange?.(); }
  }
  dispose() {
    this.dom.removeEventListener('pointerdown', this._down);
    window.removeEventListener('pointermove', this._move);
    window.removeEventListener('pointerup', this._up);
    this.dom.removeEventListener('wheel', this._wheel);
    this.dom.removeEventListener('contextmenu', this._ctx);
    window.removeEventListener('keydown', this._kd);
    window.removeEventListener('keyup', this._ku);
    window.removeEventListener('blur', this._blur);
  }
}

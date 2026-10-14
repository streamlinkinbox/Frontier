// Orbit / pan / zoom camera with WASD fly-through.
//   LMB drag  orbit          RMB / MMB / Shift+LMB drag  pan
//   Wheel     zoom to cursor-ish (dolly toward target)
//   WASD/QE   move target (hold Shift for faster)   F  frame terrain
export class Camera {
  constructor() {
    this.target = [1024, 120, 1024];
    this.yaw = 0.7; this.pitch = 0.42; this.distance = 2600;
    this.fov = (45 * Math.PI) / 180;
    this.onChange = null;
    this.keys = new Set();
    this.minPitch = -0.15; this.maxPitch = 1.52;
  }
  eye() {
    const cp = Math.cos(this.pitch);
    return [
      this.target[0] + this.distance * cp * Math.sin(this.yaw),
      this.target[1] + this.distance * Math.sin(this.pitch),
      this.target[2] + this.distance * cp * Math.cos(this.yaw),
    ];
  }
  set(o) { Object.assign(this, o); this.onChange?.(); }
  frame(S, Hs) {
    this.target = [S / 2, Hs * 0.18, S / 2];
    this.distance = S * 1.25;
    this.onChange?.();
  }
  toJSON() { return { target: [...this.target], yaw: this.yaw, pitch: this.pitch, distance: this.distance }; }

  attach(el, getWorld) {
    this.el = el; this.getWorld = getWorld;
    let drag = null;
    el.addEventListener('contextmenu', (e) => e.preventDefault());
    el.addEventListener('pointerdown', (e) => {
      el.setPointerCapture(e.pointerId);
      const pan = e.button === 1 || e.button === 2 || e.shiftKey;
      drag = { x: e.clientX, y: e.clientY, pan };
      el.focus?.();
    });
    el.addEventListener('pointermove', (e) => {
      if (!drag) return;
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      drag.x = e.clientX; drag.y = e.clientY;
      if (drag.pan) {
        const s = (this.distance * Math.tan(this.fov / 2) * 2) / el.clientHeight;
        const r = [Math.cos(this.yaw), 0, -Math.sin(this.yaw)];
        const f = [-Math.sin(this.yaw), 0, -Math.cos(this.yaw)];
        this.target[0] += (-dx * r[0] + dy * f[0]) * s;
        this.target[2] += (-dx * r[2] + dy * f[2]) * s;
      } else {
        this.yaw -= dx * 0.005;
        this.pitch = Math.min(this.maxPitch, Math.max(this.minPitch, this.pitch + dy * 0.005));
      }
      this.onChange?.();
    });
    const end = () => { drag = null; };
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      const k = Math.exp(Math.sign(e.deltaY) * Math.min(Math.abs(e.deltaY), 120) * 0.0012);
      const [S] = this.getWorld();
      this.distance = Math.min(S * 6, Math.max(5, this.distance * k));
      this.onChange?.();
    }, { passive: false });
    el.tabIndex = 0;
    el.addEventListener('keydown', (e) => {
      if (e.key === 'f' || e.key === 'F') { const [S, Hs] = this.getWorld(); this.frame(S, Hs); return; }
      this.keys.add(e.key.toLowerCase());
      if (e.shiftKey) this.keys.add('shift');
    });
    el.addEventListener('keyup', (e) => { this.keys.delete(e.key.toLowerCase()); if (!e.shiftKey) this.keys.delete('shift'); });
    el.addEventListener('blur', () => this.keys.clear());
  }
  // returns true if moved
  update(dt) {
    if (!this.keys.size) return false;
    const k = this.keys;
    let fx = 0, fz = 0, fy = 0;
    if (k.has('w')) fz += 1; if (k.has('s')) fz -= 1;
    if (k.has('a')) fx -= 1; if (k.has('d')) fx += 1;
    if (k.has('e')) fy += 1; if (k.has('q')) fy -= 1;
    if (!fx && !fz && !fy) return false;
    const sp = this.distance * 0.6 * dt * (k.has('shift') ? 3 : 1);
    const f = [-Math.sin(this.yaw), -Math.cos(this.yaw)], r = [Math.cos(this.yaw), -Math.sin(this.yaw)];
    this.target[0] += (f[0] * fz + r[0] * fx) * sp;
    this.target[2] += (f[1] * fz + r[1] * fx) * sp;
    this.target[1] += fy * sp;
    this.onChange?.();
    return true;
  }
}

// Browser input for the demo viewport: Unreal-Engine-style editor camera navigation, car keys and
// click-to-select. The camera maths lives in EditorCamera.js; this file only maps DOM events onto it.
//
// Mouse, as in the UE 5 perspective viewport:
//   RMB drag look · LMB drag move/turn · MMB drag or LMB+RMB drag pan · wheel forward/back
//   Alt+LMB orbit · Alt+RMB dolly · Alt+MMB track · RMB/LMB + wheel camera speed · LMB click select
// In the car camera modes LMB drag still looks around the car and the wheel zooms. Pressing RMB or
// MMB, or Alt + any button, switches to the free camera at the current view (like UE's "eject").

export const FLY_KEYS = {
  forward: [['KeyW', 'ArrowUp', 'Numpad8'], ['KeyS', 'ArrowDown', 'Numpad2']],
  right: [['KeyD', 'ArrowRight', 'Numpad6'], ['KeyA', 'ArrowLeft', 'Numpad4']],
  up: [['KeyE', 'PageUp', 'Numpad9'], ['KeyQ', 'PageDown', 'Numpad7']],
  localUp: [['KeyR'], ['KeyF']],
  zoom: [['KeyZ', 'Numpad1'], ['KeyC', 'Numpad3']],
};
/** keys owned by the camera while flying (RMB held) */
export const FLY_CODES = new Set([...Object.values(FLY_KEYS).flat(2), 'ShiftLeft', 'ShiftRight']);

const LMB = 1, RMB = 2, MMB = 4;
const CLICK_SLOP = 4; // px of travel before a LMB press turns from a click into a drag

export class ViewportInput {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {import('./App.js').App} app
   * @param {{onToast?: (msg: string) => void, onChange?: () => void}} [hooks]
   */
  constructor(canvas, app, { onToast = () => {}, onChange = () => {} } = {}) {
    this.canvas = canvas;
    this.app = app;
    this.onToast = onToast;
    this.onChange = onChange;
    this.keys = new Set();        // physical key codes currently held
    this.suppressed = new Set();  // fly keys still held after RMB went up – they must not drive the car
    this.buttons = 0;             // mouse button bitmask (1 LMB, 2 RMB, 4 MMB)
    this.alt = false;
    this.pointerLock = false;     // option: hide the cursor while a button is held (Pointer Lock API)
    this.lockUnavailable = !('requestPointerLock' in canvas);
    this._last = null;
    this._click = null;
    this._gesture = null;
    this._wheelAcc = 0;
    this._skipMove = 0;
    this._bind();
  }

  /** RMB held in the free camera (no Alt): WASD/QE/RF/ZC move the camera instead of the car. */
  get flying() { return (this.buttons & RMB) !== 0 && !this.alt && this.app.cam.mode === 'free'; }
  get locked() { return document.pointerLockElement === this.canvas; }

  /** key held and available for driving (not left over from a camera flight) */
  isDown(code) { return this.keys.has(code) && !this.suppressed.has(code); }

  /** FlyInput for EditorCamera.update, or null when not flying */
  cameraInput() {
    if (!this.flying) return null;
    const on = (codes) => codes.some((c) => this.keys.has(c));
    const axis = ([pos, neg]) => (on(pos) ? 1 : 0) - (on(neg) ? 1 : 0);
    return {
      forward: axis(FLY_KEYS.forward), right: axis(FLY_KEYS.right), up: axis(FLY_KEYS.up),
      localUp: axis(FLY_KEYS.localUp), zoom: axis(FLY_KEYS.zoom),
      boost: this.keys.has('ShiftLeft') || this.keys.has('ShiftRight'),
    };
  }

  keyDown(e) { this.alt = e.altKey; this.keys.add(e.code); }
  keyUp(e) { this.alt = e.altKey; this.keys.delete(e.code); this.suppressed.delete(e.code); }

  /** forget all held keys / buttons (window lost focus) */
  reset() {
    this._release();
    this.keys.clear();
    this.suppressed.clear();
    this.alt = false;
  }

  setPointerLock(on) {
    if (on && !('requestPointerLock' in this.canvas)) {
      this.onToast('Mouse lock is not supported by this browser');
      on = false;
    }
    this.pointerLock = on;
    this.lockUnavailable = !('requestPointerLock' in this.canvas);
  }

  // ---- DOM -------------------------------------------------------------------------------------

  _bind() {
    const c = this.canvas;
    c.addEventListener('contextmenu', (e) => e.preventDefault());
    c.addEventListener('pointerdown', (e) => this._onPointer(e, 'down'));
    c.addEventListener('pointermove', (e) => this._onPointer(e, 'move'));
    c.addEventListener('pointerup', (e) => this._onPointer(e, 'up'));
    c.addEventListener('pointercancel', () => this._release());
    c.addEventListener('mousedown', (e) => { if (e.button === 1) e.preventDefault(); }); // no auto-scroll
    c.addEventListener('auxclick', (e) => e.preventDefault());
    c.addEventListener('wheel', (e) => this._onWheel(e), { passive: false });
    document.addEventListener('pointerlockchange', () => { if (this.locked) this._skipMove = 1; });
    document.addEventListener('pointerlockerror', () => this._lockFailed());
  }

  _onPointer(e, type) {
    if (type === 'down') {
      this.canvas.focus({ preventScroll: true });
      try { this.canvas.setPointerCapture(e.pointerId); } catch { /* ignore */ }
    }
    this.alt = e.altKey;
    let dx = 0, dy = 0;
    if (this.locked) {
      dx = e.movementX; dy = e.movementY;
      if (this._skipMove > 0) { this._skipMove--; dx = dy = 0; } // first delta after locking can jump
    } else if (this._last) {
      dx = e.clientX - this._last[0]; dy = e.clientY - this._last[1];
    }
    this._last = [e.clientX, e.clientY];
    // movement belongs to the buttons held before this event (a chorded press arrives as a move)
    if (type === 'move' && this.buttons && (dx || dy)) this._drag(dx, dy, e);
    if (e.buttons !== this.buttons) this._setButtons(e.buttons, e);
  }

  _setButtons(next, e) {
    const prev = this.buttons, pressed = next & ~prev, released = prev & ~next;
    const app = this.app;
    this.buttons = next;
    this._gesture = null; // re-evaluated (and the pivot re-picked) on the next move
    app.cam.dragging = next !== 0;

    // a lone LMB press may be a click (select)
    if (pressed & LMB && next === LMB && !e.altKey) this._click = { x: e.clientX, y: e.clientY, moved: 0 };
    else if (pressed) this._click = null;

    // RMB / MMB / Alt + button take over from the car cameras (Unreal's "eject")
    if (app.cam.mode !== 'free' && ((pressed & (RMB | MMB)) || (pressed && e.altKey))) {
      app.setCameraMode('free');
      this.onChange();
    }
    if (released & RMB) {
      app.editorCam.endFlight();
      for (const code of this.keys) if (FLY_CODES.has(code)) this.suppressed.add(code);
    }
    if (released & LMB && this._click) {
      const r = this.canvas.getBoundingClientRect();
      app.selectAt(((this._click.x - r.left) / r.width) * 2 - 1, 1 - ((this._click.y - r.top) / r.height) * 2);
      this._click = null;
      this.onChange();
    }
    if (next === 0) {
      this._click = null;
      this._wheelAcc = 0;
      this._unlock();
    } else if (this.pointerLock && !this.lockUnavailable && !this.locked && !this._click) {
      this._lock();
    }
  }

  _drag(dx, dy, e) {
    const app = this.app, ec = app.editorCam, b = this.buttons;
    if (this._click) {
      this._click.moved += Math.abs(dx) + Math.abs(dy);
      if (this._click.moved <= CLICK_SLOP) return;
      this._click = null; // it's a drag after all
      if (this.pointerLock && !this.lockUnavailable && !this.locked) this._lock();
    }
    const alt = e.altKey;
    let g;
    if (alt && b & MMB) g = 'track';
    else if (alt && b & LMB && !(b & RMB)) g = 'orbit';
    else if (alt && b & RMB) g = 'dolly';
    else if (b & MMB || (b & LMB && b & RMB)) g = 'pan';
    else if (b & RMB) g = 'look';
    else g = app.cam.mode === 'free' ? 'moveTurn' : 'follow';
    if (g !== 'follow' && app.cam.mode !== 'free') {
      app.setCameraMode('free');
      this.onChange();
    }
    if (g !== this._gesture) {
      this._gesture = g;
      if (g === 'orbit' || g === 'dolly' || g === 'track') {
        const p = app.pivotAtScreenCenter();
        if (p) ec.setPivot(p);
      }
    }
    switch (g) {
      case 'look': ec.look(dx, dy); break;
      case 'moveTurn': ec.moveTurn(dx, dy); break;
      case 'pan': ec.pan(dx, dy); break;
      case 'orbit': ec.orbit(dx, dy); break;
      case 'dolly': ec.dollyToPivot(dx, dy); break;
      case 'track': ec.track(dx, dy, this.canvas.clientHeight); break;
      default: app.followDrag(dx, dy);
    }
  }

  _onWheel(e) {
    e.preventDefault();
    const unit = e.deltaMode === 1 ? 1 / 3 : e.deltaMode === 2 ? 1 : 1 / 100;
    const notches = Math.max(-6, Math.min(6, -e.deltaY * unit)); // > 0: wheel pushed away from you
    const app = this.app, ec = app.editorCam;
    if (this.buttons & (LMB | RMB) && app.cam.mode === 'free') {
      // UE: hold either mouse button and scroll to change the camera speed
      this._click = null;
      this._wheelAcc += notches;
      const steps = Math.trunc(this._wheelAcc);
      if (steps) {
        this._wheelAcc -= steps;
        ec.changeSpeed(steps);
        this.onToast(`Camera speed ${ec.speedSetting} · ${ec.speed} m/s`);
        this.onChange();
      }
      return;
    }
    if (app.cam.mode === 'free') ec.wheel(notches);
    else app.followZoom(-notches * 100);
  }

  _release() {
    if (this.buttons & RMB) this.app.editorCam.endFlight();
    this.buttons = 0;
    this._click = null;
    this._gesture = null;
    this.app.cam.dragging = false;
    this._unlock();
  }

  _lock() {
    try {
      const p = this.canvas.requestPointerLock();
      if (p && typeof p.catch === 'function') p.catch(() => this._lockFailed());
    } catch {
      this._lockFailed();
    }
  }

  _lockFailed() {
    if (!this.pointerLock || this.lockUnavailable) return;
    this.lockUnavailable = true; // don't retry on every press; toggling the option re-enables it
    this.onToast('Mouse lock is blocked here (embedded page?) – using normal mouse capture');
    this.onChange();
  }

  _unlock() { if (this.locked) document.exitPointerLock(); }
}

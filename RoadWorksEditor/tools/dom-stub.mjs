// Minimal DOM + three.js stubs so the editor shell can be booted and exercised in Node.
// This is deliberately dumb: it only implements the surface App.js / Viewport.js / Gizmo.js actually touch, and it
// records enough state (text, classes, listeners) for the boot test to assert against.

import { mkdirSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');

// ── three.js stub, installed as a real node_modules package so the bare "three" specifier resolves ────────────────

const THREE_STUB = `
class V3 {
  constructor(x = 0, y = 0, z = 0) { this.x = x; this.y = y; this.z = z; }
  set(x, y, z) { this.x = x; this.y = y; this.z = z; return this; }
  copy(v) { return this.set(v.x, v.y, v.z); }
  clone() { return new V3(this.x, this.y, this.z); }
  add(v) { this.x += v.x; this.y += v.y; this.z += v.z; return this; }
  sub(v) { this.x -= v.x; this.y -= v.y; this.z -= v.z; return this; }
  subVectors(a, b) { return this.set(a.x - b.x, a.y - b.y, a.z - b.z); }
  addScaledVector(v, s) { return this.set(this.x + v.x * s, this.y + v.y * s, this.z + v.z * s); }
  multiplyScalar(s) { return this.set(this.x * s, this.y * s, this.z * s); }
  negate() { return this.multiplyScalar(-1); }
  dot(v) { return this.x * v.x + this.y * v.y + this.z * v.z; }
  length() { return Math.hypot(this.x, this.y, this.z); }
  normalize() { const l = this.length() || 1; return this.multiplyScalar(1 / l); }
  cross(v) { return this.crossVectors(this.clone(), v); }
  crossVectors(a, b) { return this.set(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
  distanceTo(v) { return Math.hypot(this.x - v.x, this.y - v.y, this.z - v.z); }
  setScalar(s) { return this.set(s, s, s); }
  // Stub projection is the identity: world coordinates double as NDC, which is enough to exercise rectangle picking.
  project() { this.z = 0; return this; }
  unproject() { return this; }
  lookAt() { return this; }
}
class Obj3 {
  constructor() { this.children = []; this.position = new V3(); this.scale = new V3(1, 1, 1); this.rotation = { x: 0, y: 0, z: 0 }; this.up = new V3(0, 1, 0); this.userData = {}; this.visible = true; this.quaternion = { copy() {}, setFromUnitVectors() { return this; } }; }
  add(...o) { this.children.push(...o); return this; }
  remove(o) { this.children = this.children.filter((c) => c !== o); return this; }
  lookAt() {}
  updateMatrixWorld() {}
  copy(v) { return v; }
}
class Geometry { constructor() { this.attributes = {}; } setAttribute(n, a) { this.attributes[n] = a; return this; } setIndex() { return this; } computeVertexNormals() {} computeBoundingSphere() {} dispose() {} setFromPoints() { return this; } }
class Material { constructor(p = {}) { Object.assign(this, p); } dispose() {} }
const noop = () => {};
export class WebGLRenderer {
  constructor(p = {}) { this.canvas = p.canvas; this.shadowMap = { enabled: false, needsUpdate: false }; this.domElement = p.canvas; this.renderCalls = 0; }
  setPixelRatio() {} setSize() {} render() { this.renderCalls++; }
}
export class Scene extends Obj3 {}
export class Group extends Obj3 {}
export class Mesh extends Obj3 { constructor(g, m) { super(); this.geometry = g; this.material = m; } }
export class Line extends Mesh {}
export class PerspectiveCamera extends Obj3 { constructor(f, a, n, fa) { super(); this.fov = f; this.aspect = a; this.near = n; this.far = fa; } updateProjectionMatrix() {} }
export class HemisphereLight extends Obj3 {}
export class DirectionalLight extends Obj3 { constructor() { super(); this.castShadow = false; this.shadow = { mapSize: { set: noop }, camera: {}, bias: 0 }; this.target = new Obj3(); } }
export class GridHelper extends Obj3 { constructor() { super(); this.material = new Material(); } }
export class Color { constructor(c) { this.value = c; } }
export class Fog { constructor(c, n, f) { this.color = c; this.near = n; this.far = f; } }
export class Vector2 { constructor(x = 0, y = 0) { this.x = x; this.y = y; } }
export const Vector3 = V3;
export class Quaternion { setFromUnitVectors() { return this; } copy() { return this; } }
export class Plane {
  constructor(n = new V3(0, 0, 1), c = 0) { this.normal = n; this.constant = c; }
  setFromNormalAndCoplanarPoint(n, p) { this.normal = n.clone(); this.constant = -n.dot(p); return this; }
}
// Tests drive picking by setting pickControl.filter — there is no real intersection maths in this stub.
export const pickControl = { filter: null };
export class Raycaster {
  constructor() { this.params = { Line: { threshold: 1 } }; this.ray = { origin: new V3(0, 0, 50), direction: new V3(0, 0, -1), intersectPlane: (plane, target) => target.set(0, 0, 0) }; }
  setFromCamera() {}
  intersectObjects(objects) {
    if (!pickControl.filter) return [];
    return objects.filter(pickControl.filter).map((object) => ({ object, point: new V3(object.position.x, object.position.y, object.position.z) }));
  }
}
export class BufferGeometry extends Geometry {}
export class PlaneGeometry extends Geometry {}
export class SphereGeometry extends Geometry {}
export class RingGeometry extends Geometry {}
export class CylinderGeometry extends Geometry {}
export class CircleGeometry extends Geometry {}
export class ConeGeometry extends Geometry {}
export class Float32BufferAttribute { constructor(a, i) { this.array = a; this.itemSize = i; } }
export class MeshStandardMaterial extends Material { constructor(p = {}) { super(p); this.color = { value: p.color, multiplyScalar() { return this; } }; } }
export class MeshBasicMaterial extends Material {}
export class SpriteMaterial extends Material {}
export class Sprite extends Obj3 { constructor(m) { super(); this.material = m; } }
export class LineBasicMaterial extends Material {}
export class CanvasTexture {
  constructor(image) {
    this.image = image;
    this.wrapS = 0; this.wrapT = 0; this.anisotropy = 1; this.colorSpace = '';
    this.repeat = { x: 1, y: 1, set(x, y) { this.x = x; this.y = y; } };
    this.disposed = false;
  }
  clone() { return new CanvasTexture(this.image); }
  dispose() { this.disposed = true; }
}
export const RepeatWrapping = 1000;
export const PCFSoftShadowMap = 1;
export const SRGBColorSpace = 'srgb';
export const ACESFilmicToneMapping = 4;
export const DoubleSide = 2;
`;

export function installThreeStub() {
  const dir = resolve(root, 'node_modules/three');
  if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
  writeFileSync(resolve(dir, 'package.json'), JSON.stringify({ name: 'three', version: '0.0.0-stub', type: 'module', main: 'index.js' }));
  writeFileSync(resolve(dir, 'index.js'), THREE_STUB);
}

// ── DOM ───────────────────────────────────────────────────────────────────────────────────────────────────────────

class ClassList {
  constructor(el) { this.el = el; this.set = new Set(); }
  add(...c) { c.forEach((x) => this.set.add(x)); }
  remove(...c) { c.forEach((x) => this.set.delete(x)); }
  contains(c) { return this.set.has(c); }
  toggle(c, force) { const on = force ?? !this.set.has(c); if (on) this.set.add(c); else this.set.delete(c); return on; }
  get value() { return [...this.set].join(' '); }
}

class El {
  constructor(tag = 'div', id = '') {
    this.tagName = tag.toUpperCase();
    this.id = id;
    this.children = [];
    this.listeners = new Map();
    this.classList = new ClassList(this);
    this.style = { setProperty() {} };
    this.dataset = {};
    this.attributes = {};
    this._text = '';
    this.value = '';
    this.hidden = false;
    this.checked = false;
    this.disabled = false;
    this.clientWidth = 1200;
    this.clientHeight = 700;
  }
  set className(v) { this.classList.set = new Set(String(v).split(/\s+/).filter(Boolean)); }
  get className() { return this.classList.value; }
  set textContent(v) { this._text = String(v); this.children = []; }
  get textContent() { return this._text || this.children.map((c) => c.textContent).join(''); }
  set innerHTML(v) { this._html = String(v); this.children = []; }
  get innerHTML() { return this._html || ''; }
  appendChild(c) { this.children.push(c); if (c) c.parentNode = this; return c; }
  append(...c) {
    c.forEach((x) => {
      const node = typeof x === 'string' ? new TextNode(x) : x;
      node.parentNode = this;
      this.children.push(node);
    });
  }
  insertBefore(c) { this.children.unshift(c); if (c) c.parentNode = this; return c; }
  replaceWith(node) {
    const parent = this.parentNode;
    if (!parent) return;
    parent.children = parent.children.map((c) => (c === this ? node : c));
    node.parentNode = parent;
  }
  select() {}
  removeChild(c) { this.children = this.children.filter((x) => x !== c); }
  setAttribute(k, v) { this.attributes[k] = v; }
  getAttribute(k) { return this.attributes[k]; }
  addEventListener(type, fn) { if (!this.listeners.has(type)) this.listeners.set(type, []); this.listeners.get(type).push(fn); }
  removeEventListener() {}
  dispatch(type, event = {}) {
    const list = this.listeners.get(type) || [];
    for (const fn of list) fn({ target: this, preventDefault() {}, stopPropagation() {}, ...event });
    return list.length;
  }
  click() { this.dispatch('click', { button: 0 }); }
  matches() { return false; }
  getBoundingClientRect() { return { left: 0, top: 0, width: this.clientWidth, height: this.clientHeight }; }
  setPointerCapture() {}
  releasePointerCapture() {}
  focus() {}
  getContext(kind) { return kind === '2d' ? make2d(this) : {}; }
  // deep search helpers for assertions
  find(predicate) {
    if (predicate(this)) return this;
    for (const c of this.children) {
      const hit = c.find?.(predicate);
      if (hit) return hit;
    }
    return null;
  }
  all(predicate, out = []) {
    if (predicate(this)) out.push(this);
    for (const c of this.children) c.all?.(predicate, out);
    return out;
  }
}

// A recording 2D context. It performs no rasterisation, but it accepts every call the texture generators make and
// rejects non-finite coordinates — which is the part worth testing headlessly: that no paving pattern, at any scale,
// ever emits a NaN vertex or blows up before the material is built.
export const canvasStats = { calls: 0, bad: [] };

function make2d(canvas) {
  if (canvas._ctx) return canvas._ctx;
  const guard = (name, args) => {
    canvasStats.calls++;
    for (const a of args) {
      if (typeof a === 'number' && !Number.isFinite(a)) canvasStats.bad.push(`${name}(${args.join(', ')})`);
    }
  };
  const size = () => Math.max(1, canvas.width | 0) * Math.max(1, canvas.height | 0);
  const noop = (name) => (...args) => guard(name, args);
  const ctx = {
    canvas,
    fillStyle: '#000', strokeStyle: '#000', lineWidth: 1, font: '', textAlign: '', textBaseline: '', globalAlpha: 1,
    fillRect: noop('fillRect'), strokeRect: noop('strokeRect'), clearRect: noop('clearRect'),
    beginPath: noop('beginPath'), closePath: noop('closePath'), moveTo: noop('moveTo'), lineTo: noop('lineTo'),
    arc: noop('arc'), fill: noop('fill'), stroke: noop('stroke'), save: noop('save'), restore: noop('restore'),
    translate: noop('translate'), rotate: noop('rotate'), scale: noop('scale'), clip: noop('clip'),
    fillText: (text, x, y) => guard('fillText', [x, y]),
    createRadialGradient: (...a) => { guard('createRadialGradient', a); return { addColorStop() {} }; },
    createLinearGradient: (...a) => { guard('createLinearGradient', a); return { addColorStop() {} }; },
    getImageData: (x, y, w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
    createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
    putImageData: noop('putImageData'),
    drawImage: noop('drawImage'),
    measureText: () => ({ width: 10 }),
  };
  void size;
  canvas._ctx = ctx;
  return ctx;
}

class TextNode {
  constructor(t) { this._text = t; this.children = []; }
  get textContent() { return this._text; }
  find() { return null; }
  all(_p, out = []) { return out; }
}

export function installDom(ids) {
  const registry = new Map();
  for (const id of ids) registry.set(id, new El(id === 'SceneCanvas' ? 'canvas' : 'div', id));

  const document = {
    getElementById: (id) => registry.get(id) || null,
    createElement: (tag) => new El(tag),
    createElementNS: (_ns, tag) => new El(tag),
    createTextNode: (t) => new TextNode(t),
    body: new El('body'),
    addEventListener() {},
  };

  const windowListeners = new Map();
  const window = {
    devicePixelRatio: 1,
    addEventListener(type, fn) { if (!windowListeners.has(type)) windowListeners.set(type, []); windowListeners.get(type).push(fn); },
    dispatch(type, event = {}) { (windowListeners.get(type) || []).forEach((fn) => fn({ target: document.body, preventDefault() {}, ...event })); },
  };

  const frames = [];
  globalThis.window = window;
  globalThis.document = document;
  globalThis.performance = globalThis.performance || { now: () => Date.now() };
  globalThis.requestAnimationFrame = (fn) => { frames.push(fn); return frames.length; };
  globalThis.cancelAnimationFrame = () => {};
  globalThis.Blob = class Blob { constructor(parts) { this.parts = parts; this.size = String(parts[0] || '').length; } };
  globalThis.URL = { createObjectURL: () => 'blob:stub', revokeObjectURL() {} };
  globalThis.console.debug = () => {};

  return {
    document,
    window,
    el: (id) => registry.get(id),
    // Run queued animation frames (one generation at a time so the render loop doesn't spin forever).
    pump(times = 1) {
      for (let i = 0; i < times; i++) {
        const batch = frames.splice(0, frames.length);
        for (const fn of batch) fn(performance.now());
      }
    },
    pendingFrames: () => frames.length,
  };
}

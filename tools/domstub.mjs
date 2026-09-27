/**
 * Minimal DOM + 2D-canvas stub so the procedural texture foundry and the
 * whole simulation stack can be exercised in plain Node (no GPU, no browser).
 */

function makeCtx(canvas) {
  const noop = () => { };
  const grad = { addColorStop: noop };
  const ctx = {
    canvas,
    fillStyle: '#000', strokeStyle: '#000', lineWidth: 1, globalAlpha: 1,
    globalCompositeOperation: 'source-over', font: '', textAlign: '', textBaseline: '',
    lineCap: '', lineJoin: '', filter: '', shadowBlur: 0, shadowColor: '',
    imageSmoothingEnabled: true,
    createLinearGradient: () => grad,
    createRadialGradient: () => grad,
    createConicGradient: () => grad,
    createPattern: () => null,
    getImageData: (x, y, w, h) => ({ data: new Uint8ClampedArray(Math.max(4, w * h * 4)), width: w, height: h }),
    createImageData: (w, h) => ({ data: new Uint8ClampedArray(Math.max(4, w * h * 4)), width: w, height: h }),
    putImageData: noop, drawImage: noop,
    measureText: () => ({ width: 10, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2 }),
    fillText: noop, strokeText: noop,
  };
  for (const m of ['fillRect', 'clearRect', 'strokeRect', 'beginPath', 'closePath', 'moveTo',
    'lineTo', 'arc', 'arcTo', 'ellipse', 'bezierCurveTo', 'quadraticCurveTo', 'fill', 'stroke',
    'save', 'restore', 'translate', 'rotate', 'scale', 'setTransform', 'resetTransform',
    'clip', 'rect', 'setLineDash']) ctx[m] = noop;
  return ctx;
}

class FakeCanvas {
  constructor(w = 1, h = 1) { this.width = w; this.height = h; this._ctx = null; this.style = {}; }
  getContext() { return (this._ctx ||= makeCtx(this)); }
  toDataURL() { return 'data:,'; }
  addEventListener() { }
}

export function installDOM() {
  globalThis.HTMLCanvasElement = FakeCanvas;
  const el = () => ({
    style: {}, dataset: {}, children: [], clientWidth: 1600, clientHeight: 900,
    appendChild() { }, remove() { }, addEventListener() { }, setAttribute() { },
    classList: { add() { }, remove() { }, toggle() { }, contains() { return false; } },
    getContext: () => null,
  });
  globalThis.document = {
    createElement: (t) => (t === 'canvas' ? new FakeCanvas() : el()),
    createElementNS: (ns, t) => (t === 'canvas' ? new FakeCanvas() : el()),
    querySelector: () => null,
    querySelectorAll: () => [],
    addEventListener() { },
    body: el(),
    head: el(),
    documentElement: el(),
  };
  globalThis.window = {
    devicePixelRatio: 1, innerWidth: 1600, innerHeight: 900,
    addEventListener() { }, location: { href: 'http://localhost/' },
  };
  globalThis.addEventListener = () => { };
  globalThis.self = globalThis;
  let t = 0;
  globalThis.performance = { now: () => (t += 1e-4) * 1000 };
  globalThis.requestAnimationFrame = () => 0;
  globalThis.cancelAnimationFrame = () => { };
  globalThis.Blob = class { constructor(p) { this.parts = p; } };
  globalThis.URL = { createObjectURL: () => 'blob:x', revokeObjectURL() { } };
  return { FakeCanvas, makeCtx };
}

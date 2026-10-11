// Boots the real App (UI + Scene + Renderer) against a validating WebGPU mock and a tiny DOM stub.
// Catches reference errors, bind-group/layout mismatches, attachment conflicts and bad uploads
// that cannot be unit-tested without a GPU.
import test from "node:test";
import assert from "node:assert/strict";
import { installGlobals, CanvasContext } from "../../tools/gpu-mock.mjs";

class Elem {
  constructor(tag) { this.tagName = tag.toUpperCase(); this.children = []; this.attrs = {}; this.style = {}; this.listeners = {}; this._cls = new Set(); this.value = ""; this.textContent = ""; this.width = 800; this.height = 600; }
  get classList() { const s = this._cls; return { add: (...c) => c.forEach((x) => s.add(x)), remove: (...c) => c.forEach((x) => s.delete(x)), toggle: (c, f) => { if (f === undefined) f = !s.has(c); f ? s.add(c) : s.delete(c); return f; }, contains: (c) => s.has(c) }; }
  set className(v) { this._cls = new Set(String(v).split(/\s+/).filter(Boolean)); } get className() { return [...this._cls].join(" "); }
  set innerHTML(v) { this.children = []; this._html = v; } get innerHTML() { return this._html || ""; }
  append(...cs) { for (const c of cs) { if (c == null) continue; if (typeof c === "string") this.children.push({ text: c }); else { c.parentNode = this; this.children.push(c); } } }
  appendChild(c) { this.append(c); return c; }
  remove() { if (this.parentNode) this.parentNode.children = this.parentNode.children.filter((c) => c !== this); }
  setAttribute(k, v) { this.attrs[k] = v; if (k === "value") this.value = v; }
  addEventListener(t, f) { (this.listeners[t] ||= []).push(f); }
  dispatch(t, e) { for (const f of this.listeners[t] || []) f({ preventDefault() {}, stopPropagation() {}, ...e }); }
  getBoundingClientRect() { return { left: 0, top: 0, width: 800, height: 600 }; }
  get lastChild() { return this.children[this.children.length - 1]; } get firstChild() { return this.children[0]; }
  querySelector() { return null; } focus() {} setPointerCapture() {} releasePointerCapture() {} click() {}
  get clientWidth() { return 800; } get clientHeight() { return 600; } get nodeType() { return 1; }
  getContext(kind) { if (kind !== "webgpu") throw new Error("mock canvas only supports webgpu"); return (this._ctx ||= new CanvasContext(this)); }
}
const byId = new Map();
function installDom() {
  globalThis.document = { createElement: (t) => new Elem(t), createTextNode: (t) => ({ text: t, nodeType: 3 }), querySelector: (sel) => { const id = sel.replace("#", ""); if (!byId.has(id)) { const e = new Elem("div"); e.id = id; byId.set(id, e); } return byId.get(id); }, body: new Elem("body"), activeElement: null };
  globalThis.window = { addEventListener() {}, devicePixelRatio: 1, innerWidth: 1280, innerHeight: 800 };
  globalThis.requestAnimationFrame = (f) => setTimeout(() => f(performance.now()), 0);
  globalThis.cancelAnimationFrame = (h) => clearTimeout(h);
  globalThis.alert = () => {};
  globalThis.URL = { createObjectURL: () => "blob:", revokeObjectURL() {} };
  globalThis.Blob = class { constructor(parts) { this.size = parts.join("").length; } };
  globalThis.FileReader = class {};
  installGlobals();
}
const tick = (ms = 0) => new Promise((r) => setTimeout(r, ms));

test("app boots on the WebGPU mock and renders frames without validation errors", { timeout: 120000 }, async () => {
  installDom();
  const errors = []; const origErr = console.error; console.error = (...a) => { errors.push(a.map(String).join(" ")); };
  process.on("unhandledRejection", (e) => errors.push("unhandled: " + (e?.stack || e)));
  await import("../src/ui/app.js");
  const app = globalThis.window.app;
  assert.ok(app, "App attached to window");
  // wait for boot (loading element removed) – boot is async
  for (let i = 0; i < 400 && !app.renderer?.meshes?.size; i++) await tick(25);
  for (let i = 0; i < 200 && !app.build; i++) await tick(25);
  console.error = origErr;
  assert.equal(errors.length, 0, "no console errors during boot:\n" + errors.join("\n"));
  assert.ok(app.build, "network built");
  const dev = app.renderer.device;
  const draws0 = dev.stats.draws;
  await tick(120); // a few animation frames
  assert.ok(dev.stats.draws > draws0, "frames are being rendered");
  assert.ok(dev.stats.passes >= 8, "all passes encoded per frame");
  // interactions through the real event handlers
  const canvas = document.querySelector("#gl");
  canvas.dispatch("pointermove", { clientX: 400, clientY: 300, buttons: 0, pointerId: 1 });
  await tick(60);
  canvas.dispatch("pointerdown", { clientX: 400, clientY: 300, button: 0, buttons: 1, pointerId: 1, shiftKey: false });
  canvas.dispatch("pointerup", { clientX: 400, clientY: 300, button: 0, buttons: 0, pointerId: 1 });
  canvas.dispatch("pointerdown", { clientX: 300, clientY: 300, button: 2, buttons: 2, pointerId: 1 });
  canvas.dispatch("pointermove", { clientX: 350, clientY: 320, buttons: 2, pointerId: 1 });
  canvas.dispatch("pointerup", { clientX: 350, clientY: 320, button: 2, buttons: 0, pointerId: 1 });
  canvas.dispatch("wheel", { clientX: 400, clientY: 300, deltaY: -120 });
  await tick(60);
  // select a junction, move it, toggle layers, rebake a material, run the sim, resize
  const node = [...app.net.nodes.values()].find((n) => app.build.plan.nodes.get(n.id)?.kind === "junction");
  app.select({ kind: "node", id: node.id }); app.setTool("move");
  app.drag = { axis: "xz", node, start: [...node.p], startMouse: [400, 300] }; app.onDragMove([420, 310], {}); app.onDragEnd?.();
  await tick(40);
  app.scene.setLayer("markings", false); app.scene.setLayer("subway", false); app.scene.setLayer("subway", true);
  app.setXray(true); await tick(40); app.setXray(false);
  app.rebakeMaterial("asphalt");
  app.setMode("sim"); await tick(120); app.setMode("edit");
  canvas.width = 1024; canvas.height = 768; Object.defineProperty(canvas, "clientWidth", { value: 1024 }); Object.defineProperty(canvas, "clientHeight", { value: 768 });
  await tick(80);
  app.generate("diamond"); for (let i = 0; i < 200 && app.busy; i++) await tick(25); await tick(80);
  app.generate("cloverleaf"); for (let i = 0; i < 200 && app.busy; i++) await tick(25); await tick(80);
  assert.equal(errors.length, 0, "no errors during interaction:\n" + errors.join("\n"));
  assert.ok(app.renderer.stats.draws > 50, "scene draws > 50 (" + app.renderer.stats.draws + ")");
  app.running = false; await tick(50);
});

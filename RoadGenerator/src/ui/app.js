// frontier. roads — application shell: WebGPU boot, project state, build pipeline, viewport tools.
import { Renderer } from "../gpu/renderer.js";
import { Camera } from "../gpu/camera.js";
import { Network } from "../core/network.js";
import { buildRoadNetwork, auditMesh } from "../core/build.js";
import { buildMarkings, defaultMarkingSettings } from "../core/markings.js";
import { furnish, defaultFurnishSettings } from "../core/furnish.js";
import { buildRails, defaultRailSettings } from "../core/rail.js";
import { buildStructures, defaultStructureSettings } from "../core/structures.js";
import { buildBlocks, defaultBlockSettings } from "../core/blocks.js";
import { Traffic, defaultTrafficSettings } from "../core/traffic.js";
import { generateCity, defaultCitySettings } from "../core/city.js";
import { defaultStacks, bakeMaterial, bakeDecalAtlas, materialParams } from "../core/textures.js";
import { PROFILES, cloneProfile } from "../core/profile.js";
import { MeshBuilder } from "../core/mesh.js";
import { add, sub, mul, madd, norm, dist, distXZ, clamp } from "../core/math.js";
import { Scene, LAYERS } from "./scene.js";
import { el, icon, download, pickFile } from "./widgets.js";
import { Panels } from "./panels.js";

const $ = (s) => document.querySelector(s);
const nextFrame = () => new Promise((r) => requestAnimationFrame(() => setTimeout(r, 0)));

class App {
  constructor() {
    this.settings = {
      build: { step: 2.0, quality: "high" },
      markings: defaultMarkingSettings(), furnish: defaultFurnishSettings(), rail: defaultRailSettings(), structures: defaultStructureSettings(), blocks: defaultBlockSettings(),
      traffic: defaultTrafficSettings(), city: { ...defaultCitySettings(), preset: "demo" }, textureSize: 256,
      env: { azimuth: 52, elevation: 48, intensity: 3.2, exposure: 1.0, fog: 0.0012, ambient: 1.0 },
    };
    this.textures = defaultStacks();
    this.net = new Network();
    this.selection = null; // { kind: 'node'|'road'|'rail'|'layer'|'material', id }
    this.hover = 0;
    this.tool = "select";
    this.mode = "edit"; // edit | sim
    this.undo = []; this.redo = [];
    this.draw = null; // drawing state
    this.drag = null;
    this.buildVersion = 0; this.busy = false; this.dirty = false;
    this.drawProfile = "collector";
    this.keys = new Set();
  }

  async boot() {
    const canvas = $("#gl");
    const bar = $("#load-bar"), msg = $("#load-msg");
    const progress = (f, t) => { bar.style.width = (f * 100).toFixed(0) + "%"; msg.textContent = t; };
    try {
      progress(0.02, "Requesting WebGPU device…");
      this.renderer = await Renderer.create(canvas, { maxDpr: 1.5 });
    } catch (e) {
      $("#load-err").innerHTML = `WebGPU could not be initialised.<br>${e.message}<br><br>Use a current Chrome / Edge (desktop) or Safari 18+ with WebGPU enabled.`;
      throw e;
    }
    this.cam = new Camera(); this.cam.distance = 260; this.cam.pitch = 0.75; this.cam.yaw = -0.5;
    this.scene = new Scene(this.renderer);
    await this.bakeAllMaterials(progress);
    progress(0.9, "Baking decal atlas…"); await nextFrame();
    this.renderer.setDecalAtlas(bakeDecalAtlas(256));
    this.applyEnv();
    this.panels = new Panels(this);
    this.panels.mount();
    this.bindViewport(canvas);
    progress(0.95, "Generating showcase network…"); await nextFrame();
    this.net = generateCity(this.settings.city);
    await this.rebuild("initial");
    this.frameAll();
    $("#loading").remove();
    this.loop();
  }

  // ---------------------------------------------------------------------------------------------
  async bakeAllMaterials(progress) {
    const names = Object.keys(this.textures), size = this.settings.textureSize;
    const set = { names, size, albedo: [], normal: [], orm: [], params: [] };
    for (let i = 0; i < names.length; i++) {
      const st = this.textures[names[i]];
      const b = bakeMaterial(st, size);
      set.albedo.push(b.albedo); set.normal.push(b.normal); set.orm.push(b.orm); set.params.push(materialParams(st));
      progress?.(0.05 + 0.8 * (i + 1) / names.length, `Baking material ${names[i]} (${i + 1}/${names.length})`);
      if (i % 2 === 1) await nextFrame();
    }
    this.renderer.setMaterials(set);
    this.materialNames = names;
  }
  rebakeMaterial(name) {
    const i = this.materialNames.indexOf(name); if (i < 0) return;
    const st = this.textures[name];
    this.renderer.updateMaterial(i, bakeMaterial(st, this.settings.textureSize), materialParams(st));
  }
  applyEnv() {
    const e = this.settings.env, az = (e.azimuth * Math.PI) / 180, elv = (e.elevation * Math.PI) / 180;
    const env = this.renderer.env;
    env.sunDir = norm([Math.cos(elv) * Math.sin(az), Math.sin(elv), Math.cos(elv) * Math.cos(az)]);
    env.sunIntensity = e.intensity; env.exposure = e.exposure; env.fogDensity = e.fog; env.ambient = e.ambient;
    const warm = clamp(1 - e.elevation / 60, 0, 1);
    env.sunColor = [1.0, 0.95 - warm * 0.25, 0.88 - warm * 0.5];
  }

  // ---------------------------------------------------------------------------------------------
  setStatus(text, busy = false) { $("#status-text").textContent = text; $("#status-dot").classList.toggle("busy", busy); }
  setProgress(f) { const p = $("#progress"); p.classList.toggle("on", f > 0 && f < 1); p.style.width = (f * 100).toFixed(0) + "%"; }

  /** Full rebuild of every content layer from the network. */
  async rebuild(reason = "") {
    const version = ++this.buildVersion;
    if (this.busy) { this.dirty = true; return; }
    this.busy = true;
    const t0 = performance.now();
    try {
      const S = this.settings;
      this.setStatus("Building roads…", true); this.setProgress(0.1); await nextFrame();
      if (version !== this.buildVersion) return;
      const plan = buildRoadNetwork(this.net, { step: S.build.step });
      this.setStatus("Markings…", true); this.setProgress(0.35); await nextFrame();
      const mk = S.markings.enabled ? buildMarkings(plan, S.markings) : null;
      this.setStatus("Street furniture…", true); this.setProgress(0.5); await nextFrame();
      const F = furnish(plan, S.furnish);
      this.setStatus("Railways…", true); this.setProgress(0.65); await nextFrame();
      const rails = S.rail.enabled ? buildRails(this.net, plan, S.rail) : null;
      this.setStatus("Structures…", true); this.setProgress(0.8); await nextFrame();
      const st = buildStructures(plan, S.structures, rails ? rails.lines : []);
      const blocks = S.blocks.enabled ? buildBlocks(plan, S.blocks) : null;
      this.build = { net: this.net, plan, mk, F, rails, st, blocks, settings: S, audit: auditMesh(plan.mesh) };
      this.setStatus("Uploading…", true); this.setProgress(0.92); await nextFrame();
      this.scene.setBuild(this.build);
      this.traffic = S.traffic.enabled ? new Traffic(plan, F, rails ? rails.lines : [], S.traffic) : null;
      if (this.traffic) this.scene.updateTraffic(this.traffic);
      this.build.ms = performance.now() - t0;
      this.setStatus(`Built in ${this.build.ms.toFixed(0)} ms · ${plan.diagnostics.length ? plan.diagnostics.length + " notes" : "clean"}`);
      this.setProgress(1);
      this.panels.refresh();
    } catch (e) {
      console.error(e);
      this.setStatus("Build failed: " + e.message);
      this.setProgress(0);
    } finally {
      this.busy = false;
      if (this.dirty) { this.dirty = false; this.rebuild("queued"); }
    }
  }
  /** Rebuild only layers that depend on settings (not geometry of the network). */
  async rebuildFrom(stage) { await this.rebuild(stage); }

  pushUndo() { this.undo.push(JSON.stringify(this.net.toJSON())); if (this.undo.length > 50) this.undo.shift(); this.redo.length = 0; }
  doUndo() { if (!this.undo.length) return; this.redo.push(JSON.stringify(this.net.toJSON())); this.net = Network.fromJSON(JSON.parse(this.undo.pop())); this.select(null); this.rebuild("undo"); }
  doRedo() { if (!this.redo.length) return; this.undo.push(JSON.stringify(this.net.toJSON())); this.net = Network.fromJSON(JSON.parse(this.redo.pop())); this.select(null); this.rebuild("redo"); }

  select(sel) { this.selection = sel; this.panels.refresh(); this.updateOverlay(); }
  selectedNode() { return this.selection?.kind === "node" ? this.net.nodes.get(this.selection.id) : null; }
  selectedRoad() { return this.selection?.kind === "road" ? this.net.roads.get(this.selection.id) : null; }
  selectedRail() { return this.selection?.kind === "rail" ? this.net.rails.get(this.selection.id) : null; }
  pickIdOf(sel) { if (!sel || !["node", "road", "rail"].includes(sel.kind)) return 0; return this.scene.pickIds.get(sel.id) || 0; }

  generate() {
    this.pushUndo();
    this.net = generateCity(this.settings.city);
    this.select(null);
    this.rebuild("generate").then(() => this.frameAll());
  }
  clearAll() { this.pushUndo(); this.net = new Network(); this.select(null); this.rebuild("clear"); }
  deleteSelection() {
    const s = this.selection; if (!s) return;
    this.pushUndo();
    if (s.kind === "node") this.net.removeNode(s.id);
    else if (s.kind === "road") this.net.removeRoad(s.id);
    else if (s.kind === "rail") this.net.removeRail(s.id);
    else return;
    this.select(null); this.rebuild("delete");
  }
  frameAll() {
    const b = this.build?.plan?.mesh?.bounds?.(); if (b) this.cam.frame(b); else { this.cam.target = [0, 0, 0]; this.cam.distance = 200; }
  }
  frameSelection() {
    const n = this.selectedNode(); if (n) { this.cam.target = [...n.p]; this.cam.distance = Math.max(40, this.cam.distance * 0.4); return; }
    const r = this.selectedRoad(); if (r) { const R = this.build.plan.roads.get(r.id); if (R) { const F = R.frameAt(R.length / 2); this.cam.target = [...F.p]; this.cam.distance = clamp(R.length * 1.2, 40, 600); } return; }
    this.frameAll();
  }

  // ---------------------------------------------------------------------------------------------
  // Save / load / export
  saveJSON() { download("road-network.json", JSON.stringify({ version: 1, network: this.net.toJSON(), settings: this.settings, textures: this.textures }, null, 1), "application/json"); }
  loadJSON() {
    pickFile(".json", (txt) => {
      try {
        const j = JSON.parse(txt);
        this.pushUndo();
        this.net = Network.fromJSON(j.network || j);
        if (j.settings) this.settings = { ...this.settings, ...j.settings };
        if (j.textures) { this.textures = { ...this.textures, ...j.textures }; for (const n of Object.keys(j.textures)) this.rebakeMaterial(n); }
        this.applyEnv(); this.select(null); this.rebuild("load").then(() => this.frameAll());
      } catch (e) { alert("Could not load: " + e.message); }
    });
  }
  exportOBJ() {
    if (!this.build) return;
    const all = new MeshBuilder("export");
    const id = (p) => p;
    const parts = [this.build.plan.mesh, this.build.mk, this.build.F?.solid, this.build.F?.paint, this.build.rails?.solid, this.build.st?.solid, this.build.st?.ground];
    for (const p of parts) if (p) all.append(p, id);
    download("road-network.obj", all.toOBJ("road_network"), "text/plain");
  }

  // ---------------------------------------------------------------------------------------------
  // Viewport interaction
  bindViewport(canvas) {
    this.canvas = canvas;
    let last = null, button = -1, moved = 0;
    const pos = (e) => { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    canvas.addEventListener("pointerdown", (e) => {
      canvas.focus(); canvas.setPointerCapture(e.pointerId);
      last = pos(e); button = e.button; moved = 0;
      if (e.button === 0) this.onPrimaryDown(last, e);
    });
    canvas.addEventListener("pointermove", (e) => {
      const p = pos(e); this.mouse = p;
      if (last && button >= 0) {
        const dx = p[0] - last[0], dy = p[1] - last[1]; moved += Math.abs(dx) + Math.abs(dy);
        if (button === 2 || (button === 0 && e.altKey && !this.drag)) this.cam.orbit(dx, dy);
        else if (button === 1 || (button === 2 && e.shiftKey)) this.cam.pan(dx, dy);
        else if (button === 0 && this.drag) this.onDragMove(p, e);
        last = p;
      } else this.onHover(p);
      if (this.draw) this.updateOverlay();
    });
    canvas.addEventListener("pointerup", (e) => {
      const p = pos(e);
      if (e.button === 0) this.onPrimaryUp(p, e, moved < 4);
      if (e.button === 2 && moved < 4) this.onContext(p, e);
      last = null; button = -1;
    });
    canvas.addEventListener("dblclick", (e) => { if (this.draw) this.finishDraw(); });
    canvas.addEventListener("wheel", (e) => { e.preventDefault(); const p = pos(e); const hit = Camera.hitPlane(this.cam.ray(p[0], p[1], canvas.clientWidth, canvas.clientHeight), this.cam.target[1]); this.cam.dolly(e.deltaY, hit); }, { passive: false });
    window.addEventListener("keydown", (e) => {
      if (e.target.tagName === "INPUT" || e.target.tagName === "SELECT" || e.target.tagName === "TEXTAREA") return;
      this.keys.add(e.code);
      if (e.ctrlKey || e.metaKey) { if (e.code === "KeyZ") { e.preventDefault(); e.shiftKey ? this.doRedo() : this.doUndo(); } if (e.code === "KeyY") { e.preventDefault(); this.doRedo(); } if (e.code === "KeyS") { e.preventDefault(); this.saveJSON(); } return; }
      switch (e.code) {
        case "Digit1": case "KeyQ": this.setTool("select"); break;
        case "Digit2": case "KeyG": this.setTool("move"); break;
        case "Digit3": this.setTool("road"); break;
        case "Digit4": this.setTool("rail"); break;
        case "KeyF": this.frameSelection(); break;
        case "KeyX": this.setXray(!this.scene.xray); break;
        case "Space": e.preventDefault(); this.setMode(this.mode === "sim" ? "edit" : "sim"); break;
        case "Delete": case "Backspace": this.deleteSelection(); break;
        case "Escape": if (this.draw) this.cancelDraw(); else this.select(null); break;
        case "Enter": if (this.draw) this.finishDraw(); break;
      }
    });
    window.addEventListener("keyup", (e) => this.keys.delete(e.code));
    window.addEventListener("blur", () => this.keys.clear());
  }
  setTool(t) { this.tool = t; if (t !== "road" && t !== "rail") this.cancelDraw(); this.panels.refreshTools(); this.updateHint(); this.updateOverlay(); }
  setMode(m) { this.mode = m; this.panels.refreshHeader(); this.updateHint(); }
  setXray(on) { this.scene.setXray(on); this.cam.allowUnderground = on; this.panels.refreshHeader(); }
  updateHint() {
    const h = $("#vp-hint");
    const map = {
      select: "<span class=kbd>LMB</span> select node / road · <span class=kbd>RMB</span> orbit · <span class=kbd>MMB</span> pan · <span class=kbd>wheel</span> zoom · <span class=kbd>F</span> frame · <span class=kbd>Space</span> simulate",
      move: "Drag the gizmo axes to move the whole intersection (<span class=kbd>Y</span> axis raises it onto a bridge or sinks it into a trench). Release to rebuild.",
      road: "Click to place spline points · click an existing node or road to connect · <span class=kbd>Enter</span> / double-click to finish · <span class=kbd>Esc</span> cancel",
      rail: "Click to place track points · <span class=kbd>Enter</span> / double-click to finish · <span class=kbd>Esc</span> cancel",
    };
    h.innerHTML = map[this.tool] || "";
  }
  groundHit(p, y = 0) { return Camera.hitPlane(this.cam.ray(p[0], p[1], this.canvas.clientWidth, this.canvas.clientHeight), y); }
  async pickAt(p) { const id = await this.renderer.pick(p[0], p[1]); return this.scene.ownerOf(id); }
  async onHover(p) {
    if (this._hoverBusy) return; this._hoverBusy = true;
    const id = await this.renderer.pick(p[0], p[1]);
    this._hoverBusy = false;
    const owner = this.scene.ownerOf(id);
    this.hover = owner && (this.net.nodes.has(owner) || this.net.roads.has(owner) || this.net.rails.has(owner)) ? id : 0;
    this.hoverOwner = owner;
  }
  gizmoHandles() {
    const n = this.selectedNode(); if (!n) return null;
    const L = Math.max(6, this.cam.distance * 0.07);
    return { origin: n.p, L, axes: { x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] } };
  }
  gizmoHit(p) {
    const g = this.gizmoHandles(); if (!g) return null;
    const W = this.canvas.clientWidth, H = this.canvas.clientHeight;
    const o = this.cam.project(g.origin, W, H);
    if (Math.hypot(o[0] - p[0], o[1] - p[1]) < 10) return "xz";
    let best = null, bd = 9;
    for (const k of ["x", "y", "z"]) {
      const e = this.cam.project(madd(g.origin, g.axes[k], g.L), W, H);
      const d = pointSegDist(p, o, e);
      if (d < bd) { bd = d; best = k; }
    }
    return best;
  }
  async onPrimaryDown(p, e) {
    if (this.tool === "road" || this.tool === "rail") { await this.drawClick(p); return; }
    const n = this.selectedNode();
    if (n && (this.tool === "move" || this.tool === "select")) {
      const axis = this.gizmoHit(p);
      if (axis) { this.pushUndo(); this.drag = { axis, node: n, start: [...n.p], startMouse: p, startHit: this.groundHit(p, n.p[1]) }; return; }
    }
    this._downPos = p;
  }
  onDragMove(p, e) {
    const d = this.drag; if (!d) return;
    const n = d.node;
    if (d.axis === "xz") { const h = this.groundHit(p, d.start[1]); if (h) { n.p[0] = h[0]; n.p[2] = h[2]; } }
    else if (d.axis === "x" || d.axis === "z") {
      const h = this.groundHit(p, d.start[1]); if (h) { const k = d.axis === "x" ? 0 : 2; n.p[k] = h[k]; }
    } else if (d.axis === "y") {
      // vertical: closest point between the view ray and the vertical line through the node
      const ray = this.cam.ray(p[0], p[1], this.canvas.clientWidth, this.canvas.clientHeight);
      const t = closestOnLine(ray, [d.start[0], 0, d.start[2]], [0, 1, 0]);
      n.p[1] = clamp(Math.round(t * 10) / 10, -14, 14);
    }
    if (e.shiftKey) { n.p[0] = Math.round(n.p[0] / 5) * 5; n.p[2] = Math.round(n.p[2] / 5) * 5; }
    this.updateOverlay(); this.panels.refreshInspectorLight();
  }
  async onPrimaryUp(p, e, isClick) {
    if (this.drag) { const d = this.drag; this.drag = null; if (dist(d.node.p, d.start) > 1e-6) this.rebuild("move"); else this.undo.pop(); return; }
    if (this.tool === "road" || this.tool === "rail") return;
    if (!isClick) return;
    const owner = await this.pickAt(p);
    if (owner && this.net.nodes.has(owner)) this.select({ kind: "node", id: owner });
    else if (owner && this.net.roads.has(owner)) this.select({ kind: "road", id: owner });
    else if (owner && this.net.rails.has(owner)) this.select({ kind: "rail", id: owner });
    else if (owner && this.build?.blocks?.plots.some((b) => b.id === owner)) this.select({ kind: "block", id: owner });
    else this.select(null);
  }
  onContext(p, e) {
    const menu = el("div", { class: "ctx", style: { left: p[0] + "px", top: p[1] + "px" } });
    const close = () => menu.remove();
    const add = (t, fn) => menu.append(el("button", { text: t, onclick: () => { close(); fn(); } }));
    const n = this.selectedNode(), r = this.selectedRoad();
    if (n) { add("Frame intersection", () => this.frameSelection()); add("Delete intersection", () => this.deleteSelection()); }
    if (r) { add("Split road here", async () => { const R = this.build.plan.roads.get(r.id); const h = this.groundHit(p, 0); if (R && h) { const s = R.spline.closestStation(h).s; this.pushUndo(); const res = this.net.splitRoad(r.id, clamp(s, 10, R.length - 10)); this.select({ kind: "node", id: res.node.id }); this.rebuild("split"); } }); add("Reverse direction", () => { this.pushUndo(); const t = r.a; r.a = r.b; r.b = t; r.pts.reverse(); this.rebuild("reverse"); }); add("Delete road", () => this.deleteSelection()); }
    add("Add intersection here", () => { const h = this.groundHit(p, 0); if (h) { this.pushUndo(); const nd = this.net.addNode([h[0], 0, h[2]], { radius: 9 }); this.select({ kind: "node", id: nd.id }); this.setTool("road"); } });
    add("Frame all", () => this.frameAll());
    $("#viewport").append(menu);
    setTimeout(() => window.addEventListener("pointerdown", close, { once: true }), 0);
  }

  // ---- drawing roads / rails -------------------------------------------------------------------
  async drawClick(p) {
    const owner = await this.pickAt(p);
    const hit = this.groundHit(p, 0); if (!hit) return;
    if (!this.draw) this.draw = { kind: this.tool, pts: [], anchors: [] };
    const D = this.draw;
    let anchor = null;
    if (D.kind === "road") {
      if (owner && this.net.nodes.has(owner)) anchor = { node: owner };
      else if (owner && this.net.roads.has(owner)) { const R = this.build.plan.roads.get(owner); if (R) { const s = R.spline.closestStation(hit).s; anchor = { splitRoad: owner, s: clamp(s, 8, R.length - 8), p: R.spline.pointAt(clamp(s, 8, R.length - 8)) }; } }
    }
    D.pts.push(anchor ? (anchor.node ? [...this.net.nodes.get(anchor.node).p] : anchor.p) : [hit[0], 0, hit[2]]);
    D.anchors.push(anchor);
    if (anchor && D.pts.length > 1) this.finishDraw();
    this.updateOverlay();
  }
  cancelDraw() { this.draw = null; this.updateOverlay(); }
  finishDraw() {
    const D = this.draw; if (!D) return;
    this.draw = null;
    if (D.pts.length < 2) { this.updateOverlay(); return; }
    this.pushUndo();
    if (D.kind === "rail") {
      const r = this.net.addRail(D.pts.map((p) => [p[0], this.drawRailTunnel ? -13 : 0.03, p[2]]), { tunnel: !!this.drawRailTunnel, name: this.drawRailTunnel ? "Metro line" : "Branch line" });
      this.select({ kind: "rail", id: r.id });
    } else {
      const endNode = (i) => {
        const a = D.anchors[i];
        if (a?.node) return this.net.nodes.get(a.node);
        if (a?.splitRoad && this.net.roads.has(a.splitRoad)) { const res = this.net.splitRoad(a.splitRoad, a.s); return res.node; }
        const prof = PROFILES[this.drawProfile];
        return this.net.addNode(D.pts[i], { radius: prof.lanesF + prof.lanesB >= 4 ? 11 : 8 });
      };
      const A = endNode(0), B = endNode(D.pts.length - 1);
      if (A === B) { this.updateOverlay(); return; }
      const r = this.net.addRoad(A, B, { profile: this.drawProfile, pts: D.pts.slice(1, -1), name: PROFILES[this.drawProfile].name });
      this.select({ kind: "road", id: r.id });
    }
    this.rebuild("draw");
  }

  // ---- overlay lines (gizmo, drawing preview, selected spline) ---------------------------------
  updateOverlay() {
    const L = [];
    const seg = (a, b, c, alpha = 1) => L.push(a[0], a[1], a[2], c[0], c[1], c[2], alpha, b[0], b[1], b[2], c[0], c[1], c[2], alpha);
    const g = this.gizmoHandles();
    if (g && (this.tool === "move" || this.tool === "select")) {
      const o = g.origin;
      seg(o, madd(o, g.axes.x, g.L), [0.95, 0.3, 0.3]); seg(o, madd(o, g.axes.y, g.L), [0.4, 0.95, 0.4]); seg(o, madd(o, g.axes.z, g.L), [0.35, 0.55, 1]);
      // arrow heads
      for (const [k, c] of [["x", [0.95, 0.3, 0.3]], ["y", [0.4, 0.95, 0.4]], ["z", [0.35, 0.55, 1]]]) { const e = madd(o, g.axes[k], g.L), b = madd(o, g.axes[k], g.L * 0.85); const perp = k === "y" ? [1, 0, 0] : [0, 1, 0]; seg(e, madd(b, perp, g.L * 0.05), c); seg(e, madd(b, perp, -g.L * 0.05), c); }
      // centre square (plane handle)
      const s = g.L * 0.18; const q = [[-s, 0, -s], [s, 0, -s], [s, 0, s], [-s, 0, s]].map((v) => add(o, v));
      for (let i = 0; i < 4; i++) seg(q[i], q[(i + 1) % 4], [1, 1, 1], 0.9);
      // ring at the node radius
      const n = this.selectedNode(); const r = n.radius; const N = 48;
      for (let i = 0; i < N; i++) { const a0 = (i / N) * Math.PI * 2, a1 = ((i + 1) / N) * Math.PI * 2; seg([o[0] + Math.cos(a0) * r, o[1] + 0.05, o[2] + Math.sin(a0) * r], [o[0] + Math.cos(a1) * r, o[1] + 0.05, o[2] + Math.sin(a1) * r], [1, 1, 1], 0.35); }
      // connected road previews while dragging
      if (this.drag) for (const rd of this.net.roadsAt(n.id)) { const a = this.net.nodes.get(rd.a).p, b = this.net.nodes.get(rd.b).p; const pts = [a, ...rd.pts, b]; for (let i = 0; i + 1 < pts.length; i++) seg(pts[i], pts[i + 1], [1, 1, 1], 0.5); }
    }
    const r = this.selectedRoad();
    if (r && this.build) {
      const R = this.build.plan.roads.get(r.id);
      if (R) { const n = Math.max(8, Math.ceil(R.length / 3)); for (let i = 0; i < n; i++) { const a = R.spline.pointAt((i / n) * R.length), b = R.spline.pointAt(((i + 1) / n) * R.length); seg([a[0], a[1] + 0.08, a[2]], [b[0], b[1] + 0.08, b[2]], [0.31, 0.85, 0.88], 0.9); } }
      for (const p of r.pts) { const s = 1.2; seg([p[0] - s, p[1] + 0.1, p[2]], [p[0] + s, p[1] + 0.1, p[2]], [1, 1, 1]); seg([p[0], p[1] + 0.1, p[2] - s], [p[0], p[1] + 0.1, p[2] + s], [1, 1, 1]); }
    }
    const rl = this.selectedRail();
    if (rl) for (let i = 0; i + 1 < rl.pts.length; i++) seg([rl.pts[i][0], rl.pts[i][1] + 0.3, rl.pts[i][2]], [rl.pts[i + 1][0], rl.pts[i + 1][1] + 0.3, rl.pts[i + 1][2]], [0.31, 0.85, 0.88], 0.9);
    if (this.draw) {
      const D = this.draw, c = D.kind === "rail" ? [0.9, 0.7, 0.2] : [0.31, 0.85, 0.88];
      for (let i = 0; i + 1 < D.pts.length; i++) seg(madd(D.pts[i], [0, 0.2, 0], 1), madd(D.pts[i + 1], [0, 0.2, 0], 1), c);
      for (const p of D.pts) { const s = 1.5; seg([p[0] - s, p[1] + 0.2, p[2] - s], [p[0] + s, p[1] + 0.2, p[2] + s], c); seg([p[0] - s, p[1] + 0.2, p[2] + s], [p[0] + s, p[1] + 0.2, p[2] - s], c); }
      if (this.mouse && D.pts.length) { const h = this.groundHit(this.mouse, 0); if (h) seg(madd(D.pts[D.pts.length - 1], [0, 0.2, 0], 1), [h[0], 0.2, h[2]], c, 0.5); }
    }
    this.renderer.setLines("overlay", L.length ? new Float32Array(L) : null);
  }
  updateLabels() {
    const ov = $("#overlay"); ov.innerHTML = "";
    const n = this.selectedNode(); if (!n) return;
    const W = this.canvas.clientWidth, H = this.canvas.clientHeight;
    const s = this.cam.project(n.p, W, H); if (s[2] < 0 || s[2] > 1) return;
    const N = this.build?.plan.nodes.get(n.id);
    ov.append(el("div", { class: "label", style: { left: s[0] + "px", top: s[1] + "px" }, text: `${n.name || (N ? N.kind : "node")} · ${N ? N.arms.length : 0} arms · x ${n.p[0].toFixed(1)} y ${n.p[1].toFixed(1)} z ${n.p[2].toFixed(1)}` }));
  }

  // ---------------------------------------------------------------------------------------------
  loop() {
    let last = performance.now();
    this.running = true;
    const tick = (now) => {
      if (!this.running) return;
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      // fly keys
      const k = this.keys; const dir = [0, 0, 0];
      if (k.has("KeyW") || k.has("ArrowUp")) dir[2] += 1; if (k.has("KeyS") || k.has("ArrowDown")) dir[2] -= 1;
      if (k.has("KeyA") || k.has("ArrowLeft")) dir[0] -= 1; if (k.has("KeyD") || k.has("ArrowRight")) dir[0] += 1;
      if (k.has("KeyE")) dir[1] += 1; if (k.has("KeyC")) dir[1] -= 1;
      if (dir[0] || dir[1] || dir[2]) this.cam.fly(dir, dt, k.has("ShiftLeft") || k.has("ShiftRight"));
      this.cam.update(this.canvas.clientWidth / Math.max(1, this.canvas.clientHeight));
      if (this.mode === "sim" && this.traffic) { this.traffic.update(dt); this.scene.updateTraffic(this.traffic); this.renderer.time += dt; }
      if (this.selectedNode()) this.updateOverlay();
      const flags = (this.scene.xray ? 2 : 0);
      this.renderer.render(this.cam, { selected: this.pickIdOf(this.selection), hovered: this.hover, flags });
      this.updateLabels();
      if ((this._frame = (this._frame || 0) + 1) % 15 === 0) this.panels.refreshStats();
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }
}

function pointSegDist(p, a, b) {
  const vx = b[0] - a[0], vy = b[1] - a[1], wx = p[0] - a[0], wy = p[1] - a[1];
  const l2 = vx * vx + vy * vy; const t = l2 > 0 ? clamp((wx * vx + wy * vy) / l2, 0, 1) : 0;
  return Math.hypot(p[0] - (a[0] + vx * t), p[1] - (a[1] + vy * t));
}
/** Parameter t along line (o2 + t·d2) closest to ray. */
function closestOnLine(ray, o2, d2) {
  const r = sub(ray.o, o2); const a = 1, b = ray.d[0] * d2[0] + ray.d[1] * d2[1] + ray.d[2] * d2[2], c = 1;
  const d = ray.d[0] * r[0] + ray.d[1] * r[1] + ray.d[2] * r[2], e = d2[0] * r[0] + d2[1] * r[1] + d2[2] * r[2];
  const den = a * c - b * b; if (Math.abs(den) < 1e-6) return 0;
  return (a * e - b * d) / den;
}

window.app = new App();
window.app.boot().catch((e) => console.error(e));

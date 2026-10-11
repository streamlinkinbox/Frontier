// Panels: header controls, tool rail, Create/Generate panel (left), Layer stack + Inspector (right).
import { el, icon, slider, toggle, select, number, text, color, button, card, metric, chips, objHead, fmt } from "./widgets.js";
import { LAYERS } from "./scene.js";
import { PRESETS } from "../core/city.js";
import { PROFILES } from "../core/profile.js";
import { PAVING_PATTERNS } from "../core/textures.js";
import { BOLLARD_STYLES } from "../core/props.js";
import { MAX_VEHICLES } from "../core/traffic.js";

const $ = (s) => document.querySelector(s);
const TOOLS = [
  { id: "select", name: "Select", icon: "pointer", key: "1" },
  { id: "move", name: "Move intersection", icon: "move", key: "2" },
  { id: "road", name: "Draw road", icon: "road", key: "3" },
  { id: "rail", name: "Draw railway", icon: "rail", key: "4" },
];

export class Panels {
  constructor(app) { this.app = app; this.layerTab = "content"; this.search = ""; }
  mount() {
    this.refreshHeader(); this.refreshTools(); this.renderLeft(); this.refresh(); this.app.updateHint();
    $("#vp-top").append(
      el("button", { class: "hbtn", html: icon("frame", 13) + "Frame all", onclick: () => this.app.frameAll() }),
      el("button", { class: "hbtn", id: "btn-xray", html: icon("xray", 13) + "X-ray", onclick: () => this.app.setXray(!this.app.scene.xray) }),
    );
  }
  // ---- header ----
  refreshHeader() {
    const A = this.app;
    const t = $("#hdr-tools"); t.innerHTML = "";
    t.append(
      el("button", { class: "hbtn", html: icon("undo", 13), title: "Undo (Ctrl+Z)", onclick: () => A.doUndo() }),
      el("button", { class: "hbtn", html: icon("redo", 13), title: "Redo (Ctrl+Y)", onclick: () => A.doRedo() }),
      el("button", { class: "hbtn", html: icon("upload", 13) + "Open", onclick: () => A.loadJSON() }),
      el("button", { class: "hbtn", html: icon("download", 13) + "Save", onclick: () => A.saveJSON() }),
      el("button", { class: "hbtn", html: icon("download", 13) + "OBJ", title: "Export merged geometry as Wavefront OBJ", onclick: () => A.exportOBJ() }),
    );
    const m = $("#hdr-mode"); m.innerHTML = "";
    const pill = el("div", { class: "pill" }, [
      el("button", { class: A.mode === "edit" ? "on edit" : "", text: "Edit", onclick: () => A.setMode("edit") }),
      el("button", { class: A.mode === "sim" ? "on sim" : "", text: "Simulate", onclick: () => A.setMode("sim") }),
    ]);
    m.append(pill, el("button", { class: "hbtn" + (A.mode === "sim" ? " on" : ""), html: icon(A.mode === "sim" ? "pause" : "play", 12), title: "Space", onclick: () => A.setMode(A.mode === "sim" ? "edit" : "sim") }));
    $("#btn-xray")?.classList.toggle("on", !!A.scene?.xray);
  }
  refreshTools() {
    const r = $("#rail"); r.innerHTML = "";
    for (const t of TOOLS) r.append(el("button", { class: "tool" + (this.app.tool === t.id ? " on" : ""), title: `${t.name} (${t.key})`, html: icon(t.icon) + `<span class="key">${t.key}</span>`, onclick: () => this.app.setTool(t.id) }));
    r.append(el("div", { class: "rail-sep" }));
    r.append(el("button", { class: "tool", title: "Frame selection (F)", html: icon("frame"), onclick: () => this.app.frameSelection() }));
    r.append(el("button", { class: "tool", title: "Delete selection (Del)", html: icon("trash"), onclick: () => this.app.deleteSelection() }));
  }
  refreshStats() {
    const A = this.app, s = A.renderer.stats, b = A.build;
    $("#vp-stats").innerHTML = `${s.frameMs.toFixed(1)} ms · ${s.draws} draws · ${(s.triangles / 1000).toFixed(0)}k tris` + (b ? ` · ${b.audit.faces.toLocaleString()} faces · ${(b.audit.quadRatio * 100).toFixed(0)}% quads` : "") + (A.traffic && A.mode === "sim" ? ` · ${A.traffic.vehicles.length} vehicles` : "");
  }

  // ---- left: create / generate ----
  renderLeft() {
    const A = this.app, S = A.settings, L = $("#left"); L.innerHTML = "";
    L.append(el("div", { class: "panel-head" }, [el("span", { class: "panel-title", text: "Create" }), el("span", { class: "panel-sub", text: "network" })]));
    // presets
    const sec1 = el("div", { class: "section" });
    sec1.append(el("h3", { text: "Generator presets" }));
    for (const p of PRESETS) sec1.append(el("div", { class: "preset" + (S.city.preset === p.id ? " on" : ""), onclick: () => { S.city.preset = p.id; this.renderLeft(); } }, [el("div", { class: "t", text: p.name }), el("div", { class: "d", text: p.desc })]));
    sec1.append(slider("Seed", S.city.seed, 1, 99, 1, (v) => { S.city.seed = v; }));
    if (S.city.preset === "grid") {
      sec1.append(slider("Blocks across", S.city.blocksX, 2, 8, 1, (v) => { S.city.blocksX = v; }), slider("Blocks down", S.city.blocksY, 2, 6, 1, (v) => { S.city.blocksY = v; }), slider("Block width", S.city.blockW, 60, 180, 5, (v) => { S.city.blockW = v; }, { unit: " m" }), slider("Block depth", S.city.blockH, 50, 140, 5, (v) => { S.city.blockH = v; }, { unit: " m" }), slider("Curviness", S.city.curviness, 0, 1, 0.05, (v) => { S.city.curviness = v; }));
    }
    if (S.city.preset === "grid" || S.city.preset === "organic") sec1.append(toggle("Railway line", S.city.rail, (v) => { S.city.rail = v; }), toggle("Subway line", S.city.subway, (v) => { S.city.subway = v; }));
    sec1.append(el("div", { class: "btnrow" }, [button(icon("wand", 13) + "Generate", () => A.generate(), "btn primary"), button("Clear", () => A.clearAll(), "btn")]));
    L.append(sec1);
    // draw profile
    const sec2 = el("div", { class: "section" });
    sec2.append(el("h3", { text: "Draw road · profile" }));
    sec2.append(chips(Object.entries(PROFILES).map(([k, v]) => [k, v.name]), A.drawProfile, (v) => { A.drawProfile = v; }));
    sec2.append(el("div", { style: { height: "8px" } }), toggle("Railway as subway tunnel", !!A.drawRailTunnel, (v) => { A.drawRailTunnel = v; }));
    sec2.append(el("div", { class: "hint", style: { padding: "8px 0 0" }, html: "Select the <b>Draw road</b> tool (3), click to drop spline points, click an existing intersection or road to connect. Finish with Enter." }));
    L.append(sec2);
    // build quality
    const sec3 = el("div", { class: "section" });
    sec3.append(el("h3", { text: "Build quality" }));
    sec3.append(slider("Sweep step", S.build.step, 1, 4, 0.5, (v, fin) => { S.build.step = v; if (fin) A.rebuild("step"); }, { unit: " m" }));
    sec3.append(select("Texture resolution", String(S.textureSize), [["256", "256 px (fast)"], ["512", "512 px"], ["1024", "1024 px (slow bake)"]], async (v) => { S.textureSize = +v; A.setStatus("Re-baking materials…", true); await A.bakeAllMaterials(null); A.setStatus("Materials baked"); }));
    L.append(sec3);
    // environment
    const sec4 = el("div", { class: "section" });
    sec4.append(el("h3", { text: "Environment" }));
    const env = S.env;
    sec4.append(slider("Sun azimuth", env.azimuth, 0, 360, 1, (v) => { env.azimuth = v; A.applyEnv(); }, { unit: "°" }), slider("Sun elevation", env.elevation, 5, 85, 1, (v) => { env.elevation = v; A.applyEnv(); }, { unit: "°" }), slider("Sun intensity", env.intensity, 0.5, 6, 0.1, (v) => { env.intensity = v; A.applyEnv(); }), slider("Exposure", env.exposure, 0.3, 2.5, 0.05, (v) => { env.exposure = v; A.applyEnv(); }), slider("Fog", env.fog * 1000, 0, 5, 0.1, (v) => { env.fog = v / 1000; A.applyEnv(); }));
    L.append(sec4);
    L.append(el("div", { class: "footer-note", style: { marginTop: "auto", paddingTop: "14px" }, text: "Changes apply in real time" }));
  }

  // ---- right: layer stack ----
  refresh() { this.renderLayers(); this.renderInspector(); this.refreshHeader(); }
  refreshInspectorLight() { const n = this.app.selectedNode(); if (n && this._posFields) { this._posFields.x.value = n.p[0].toFixed(1); this._posFields.y.value = n.p[1].toFixed(1); this._posFields.z.value = n.p[2].toFixed(1); } }
  renderLayers() {
    const A = this.app, root = $("#layers"); root.innerHTML = "";
    root.append(el("div", { class: "panel-head" }, [el("span", { class: "panel-title", html: icon("layers", 12) + "&nbsp; Layer stack" }), el("span", { class: "panel-sub", text: A.build ? `${A.net.nodes.size} nodes · ${A.net.roads.size} roads` : "" })]));
    const tabs = el("div", { class: "tabs" }, [["content", "Content"], ["materials", "Textures"], ["objects", "Outliner"]].map(([id, t]) => el("button", { class: this.layerTab === id ? "on" : "", text: t, onclick: () => { this.layerTab = id; this.renderLayers(); } })));
    root.append(tabs);
    const tree = el("div", { class: "tree" });
    if (this.layerTab === "content") {
      for (const L of LAYERS) {
        const on = A.scene.vis[L.id];
        const sel = A.selection?.kind === "layer" && A.selection.id === L.id;
        const row = el("div", { class: "row" + (sel ? " sel" : "") + (on ? "" : " dim"), onclick: () => A.select({ kind: "layer", id: L.id }) }, [
          el("span", { class: "ico", html: icon(L.icon, 15) }), el("span", { class: "name", text: L.name }),
          el("button", { class: "eye" + (on ? "" : " off"), html: icon(on ? "eye" : "eyeOff", 14), onclick: (e) => { e.stopPropagation(); A.scene.setLayer(L.id, !on); this.renderLayers(); } }),
        ]);
        tree.append(row);
      }
    } else if (this.layerTab === "materials") {
      const search = el("div", { class: "search" }, [el("span", { html: icon("search", 13) }), el("input", { placeholder: "Filter materials…", value: this.search, oninput: (e) => { this.search = e.target.value; this.renderLayers(); } })]);
      tree.append(search);
      for (const name of Object.keys(A.textures)) {
        if (this.search && !name.toLowerCase().includes(this.search.toLowerCase())) continue;
        const st = A.textures[name];
        const sel = A.selection?.kind === "material" && A.selection.id === name;
        tree.append(el("div", { class: "row" + (sel ? " sel" : ""), onclick: () => A.select({ kind: "material", id: name }) }, [el("span", { class: "ico", html: icon("texture", 15) }), el("span", { class: "name", text: name }), el("span", { class: "meta", text: `${st.layers.filter((l) => l.on).length}/${st.layers.length} · ${st.kind}` })]));
      }
    } else {
      const search = el("div", { class: "search" }, [el("span", { html: icon("search", 13) }), el("input", { placeholder: "Find roads / intersections…", value: this.search, oninput: (e) => { this.search = e.target.value; this.renderLayers(); } })]);
      tree.append(search);
      const q = this.search.toLowerCase();
      const plan = A.build?.plan;
      tree.append(el("div", { class: "row dim", style: { height: "26px" } }, [el("span", { class: "meta", text: "INTERSECTIONS" })]));
      for (const n of A.net.nodes.values()) {
        const N = plan?.nodes.get(n.id); if (!N || N.kind === "end") continue;
        const label = n.name || `${N.kind === "junction" ? (N.arms.length === 3 ? "T junction" : N.arms.length === 4 ? "Crossroads" : N.arms.length + "-way") : "Bend"} ${n.id.slice(-4)}`;
        if (q && !label.toLowerCase().includes(q)) continue;
        tree.append(el("div", { class: "row child" + (A.selection?.kind === "node" && A.selection.id === n.id ? " sel" : ""), onclick: () => { A.select({ kind: "node", id: n.id }); }, ondblclick: () => A.frameSelection() }, [el("span", { class: "ico", html: icon("node", 14) }), el("span", { class: "name", text: label }), el("span", { class: "meta", text: `${N.arms.length} arms` })]));
      }
      tree.append(el("div", { class: "row dim", style: { height: "26px" } }, [el("span", { class: "meta", text: "ROADS" })]));
      for (const r of A.net.roads.values()) {
        const label = r.name || r.profile.name;
        if (q && !label.toLowerCase().includes(q)) continue;
        const R = plan?.roads.get(r.id);
        tree.append(el("div", { class: "row child" + (A.selection?.kind === "road" && A.selection.id === r.id ? " sel" : ""), onclick: () => A.select({ kind: "road", id: r.id }), ondblclick: () => A.frameSelection() }, [el("span", { class: "ico", html: icon("road", 14) }), el("span", { class: "name", text: label }), el("span", { class: "meta", text: R ? `${R.length.toFixed(0)} m` : "" })]));
      }
      if (A.net.rails.size) tree.append(el("div", { class: "row dim", style: { height: "26px" } }, [el("span", { class: "meta", text: "RAIL" })]));
      for (const rl of A.net.rails.values()) tree.append(el("div", { class: "row child" + (A.selection?.kind === "rail" && A.selection.id === rl.id ? " sel" : ""), onclick: () => A.select({ kind: "rail", id: rl.id }) }, [el("span", { class: "ico", html: icon("rail", 14) }), el("span", { class: "name", text: rl.name || "Rail line" }), el("span", { class: "meta", text: rl.tunnel ? "tunnel" : "surface" })]));
    }
    root.append(tree);
  }

  // ---- right: inspector ----
  renderInspector() {
    const A = this.app, root = $("#inspector"); root.innerHTML = ""; this._posFields = null;
    const sel = A.selection;
    if (!sel) return this.inspectProject(root);
    if (sel.kind === "node") return this.inspectNode(root, A.net.nodes.get(sel.id));
    if (sel.kind === "road") return this.inspectRoad(root, A.net.roads.get(sel.id));
    if (sel.kind === "rail") return this.inspectRail(root, A.net.rails.get(sel.id));
    if (sel.kind === "layer") return this.inspectLayer(root, sel.id);
    if (sel.kind === "material") return this.inspectMaterial(root, sel.id);
  }
  inspectProject(root) {
    const A = this.app, b = A.build;
    root.append(objHead("Project", "Road network", "road"));
    const cards = el("div", { class: "cards" });
    if (b) {
      cards.append(card("Geometry", "grid", [metric(b.audit.faces.toLocaleString(), "", "faces in the merged road mesh"), el("div", { class: "cap", text: `${(b.audit.quadRatio * 100).toFixed(1)} % quads · ${b.audit.nonManifoldEdges} non-manifold edges` })]));
      cards.append(card("Network", "node", [metric(A.net.nodes.size, "", "intersections & ends"), el("div", { class: "cap", text: `${A.net.roads.size} roads · ${A.net.rails.size} rail lines · built in ${b.ms?.toFixed(0)} ms` })]));
      const diags = b.plan.diagnostics || [];
      if (diags.length) cards.append(card("Build notes", "info", diags.slice(0, 8).map((d) => el("div", { class: "diag", text: d.message })), true));
    }
    cards.append(card("Workflow", "wand", [el("div", { class: "hint", style: { padding: 0 }, html: "1 · Generate a preset or draw roads with splines.<br>2 · Click an intersection to move it with the gizmo — the merged junction mesh, markings and furniture rebuild.<br>3 · Texture through the <b>Textures</b> layer stack.<br>4 · Press <span class=kbd>Space</span> to run AI traffic and trains." })], true));
    root.append(cards);
  }
  inspectNode(root, n) {
    const A = this.app; if (!n) return;
    const N = A.build?.plan.nodes.get(n.id), J = A.build?.plan.junctions?.find((j) => j.node.id === n.id);
    const kind = N ? (N.kind === "junction" ? (N.arms.length === 3 ? "T junction" : N.arms.length === 4 ? "Crossroads" : `${N.arms.length}-way junction`) : N.kind === "joint" ? "Bend / transition" : "Road end") : "Intersection";
    root.append(objHead(kind, n.name || n.id, "node", (v) => { n.name = v; this.renderLayers(); }));
    const cards = el("div", { class: "cards" });
    const rebuild = () => { A.rebuild("node"); };
    const pos = el("div", {});
    const mk = (k, i) => { const inp = el("input", { type: "number", class: "num", step: 0.5, value: n.p[i].toFixed(1), onchange: (e) => { A.pushUndo(); n.p[i] = parseFloat(e.target.value) || 0; rebuild(); } }); return el("div", { class: "field" }, [el("label", { text: k }), inp]); };
    const fx = mk("X", 0), fy = mk("Elevation (Y)", 1), fz = mk("Z", 2);
    this._posFields = { x: fx.lastChild, y: fy.lastChild, z: fz.lastChild };
    pos.append(fx, fy, fz);
    cards.append(card("Position", "move", [pos, el("div", { class: "cap", text: "Raise above +6.5 m for a viaduct, lower below −1.2 m for a trench / −6.5 m for a tunnel." })]));
    cards.append(card("Geometry", "node", [metric(N ? N.arms.length : 0, "", "arms"), slider("Corner radius", n.radius, 3, 22, 0.5, (v, fin) => { n.radius = v; if (fin) { A.pushUndo(); rebuild(); } }, { unit: " m" })]));
    cards.append(card("Control", "lamp", [select("Signals", n.control || "auto", [["auto", "Automatic"], ["signals", "Traffic lights"], ["signs", "Stop / yield signs"], ["none", "Uncontrolled"]], (v) => { A.pushUndo(); n.control = v; rebuild(); }), select("Crosswalks", n.crosswalks || "auto", [["auto", "Automatic"], ["all", "All arms"], ["none", "None"]], (v) => { A.pushUndo(); n.crosswalks = v; rebuild(); })]));
    if (J) cards.append(card("Topology", "grid", [el("div", { class: "kv" }, [el("span", { text: "type" }), el("b", { text: J.type }), el("span", { text: "centre" }), el("b", { text: J.centre?.kind || "coons" }), el("span", { text: "corners" }), el("b", { text: J.corners.map((c) => c.kind).join(", ") })])]));
    const roads = A.net.roadsAt(n.id);
    cards.append(card("Connected roads", "road", roads.map((r) => el("div", { class: "row child", style: { paddingLeft: "6px" }, onclick: () => A.select({ kind: "road", id: r.id }) }, [el("span", { class: "ico", html: icon("road", 13) }), el("span", { class: "name", text: r.name || r.profile.name }), el("span", { class: "meta", text: r.profile.id })])), true));
    cards.append(el("div", { class: "btnrow", style: { gridColumn: "1 / -1" } }, [button(icon("frame", 13) + "Frame", () => A.frameSelection()), button(icon("trash", 13) + "Delete intersection", () => A.deleteSelection(), "btn danger")]));
    root.append(cards);
    root.append(el("div", { class: "footer-note", text: "Drag the gizmo in the viewport · rebuilds on release" }));
  }
  inspectRoad(root, r) {
    const A = this.app; if (!r) return;
    const R = A.build?.plan.roads.get(r.id), P = r.profile;
    root.append(objHead(P.name, r.name || P.name, "road", (v) => { r.name = v; this.renderLayers(); }));
    const cards = el("div", { class: "cards" });
    const rebuild = () => { A.rebuild("road"); };
    const set = (k) => (v, fin = true) => { if (!fin) return; A.pushUndo(); P[k] = v; r.oneWay = P.lanesB === 0; rebuild(); };
    cards.append(card("Length", "ruler", [metric(R ? R.length.toFixed(0) : "–", "m", `${P.lanesF + P.lanesB} lanes · ${(R ? R.section.widthL + R.section.widthR : 0).toFixed(1)} m carriageway`)]));
    cards.append(card("Profile", "road", [select("Preset", P.id, Object.entries(PROFILES).map(([k, v]) => [k, v.name]), (v) => { A.pushUndo(); const np = { ...PROFILES[v], id: v }; Object.assign(P, np); r.oneWay = P.lanesB === 0; rebuild(); }), slider("Speed limit", P.speed, 20, 130, 10, set("speed"), { unit: " km/h" })]));
    cards.append(card("Lanes", "marking", [slider("Forward lanes", P.lanesF, 0, 4, 1, set("lanesF")), slider("Backward lanes", P.lanesB, 0, 4, 1, set("lanesB")), slider("Lane width", P.laneWidth, 2.75, 3.75, 0.05, set("laneWidth"), { unit: " m" }), slider("Median", P.median, 0, 6, 0.2, set("median"), { unit: " m" }), select("Median type", P.medianType, [["none", "None"], ["paint", "Painted"], ["raised", "Raised kerb"], ["barrier", "Concrete barrier"]], set("medianType"))], true));
    cards.append(card("Edges", "grid", [slider("Parking left", P.parkingL, 0, 2.6, 0.1, set("parkingL"), { unit: " m" }), slider("Parking right", P.parkingR, 0, 2.6, 0.1, set("parkingR"), { unit: " m" }), slider("Bike lane", P.bike, 0, 2.5, 0.1, set("bike"), { unit: " m" }), slider("Shoulder", P.shoulder, 0, 3.5, 0.25, set("shoulder"), { unit: " m" })]));
    cards.append(card("Footway", "ground", [select("Edge", P.edge, [["curb", "Kerb + footway"], ["shoulder", "Shoulder + verge"], ["barrier", "Barrier"]], set("edge")), slider("Footway width", P.walk, 0, 6, 0.2, set("walk"), { unit: " m" }), select("Surface", P.walkType, [["paving", "Paving"], ["concrete", "Concrete"], ["asphalt", "Asphalt"]], set("walkType")), select("Back strip", P.backType, [["planting", "Planting"], ["concrete", "Concrete"], ["grass", "Grass"]], set("backType"))]));
    cards.append(card("Features", "cone", [
      toggle("Roadworks zone", !!r.construction, (v) => { A.pushUndo(); r.construction = v ? { from: 0.3, to: 0.6 } : null; rebuild(); }),
      toggle("Billboards", !!r.billboards, (v) => { A.pushUndo(); r.billboards = v; rebuild(); }),
      select("Guardrail", r.guardrail || "auto", [["auto", "Automatic"], ["none", "None"], ["wbeam", "W-beam"], ["concrete", "Jersey barrier"], ["box", "Box beam"]], (v) => { A.pushUndo(); r.guardrail = v; rebuild(); }),
      select("Bridge", r.bridge || "auto", [["auto", "Automatic"], ["always", "Always deck"], ["never", "Never (embankment)"]], (v) => { A.pushUndo(); r.bridge = v; rebuild(); }),
      slider("Surface wear", r.wear ?? 0.5, 0, 1, 0.05, (v, fin) => { if (fin) { A.pushUndo(); r.wear = v; rebuild(); } }),
    ], true));
    if (R) cards.append(card("Control points", "split", [el("div", { class: "cap", text: `${r.pts.length} interior spline points · right-click the road for split / reverse` })]));
    cards.append(el("div", { class: "btnrow", style: { gridColumn: "1 / -1" } }, [button(icon("frame", 13) + "Frame", () => A.frameSelection()), button(icon("reverse", 13) + "Reverse", () => { A.pushUndo(); const t = r.a; r.a = r.b; r.b = t; r.pts.reverse(); rebuild(); }), button(icon("trash", 13) + "Delete", () => A.deleteSelection(), "btn danger")]));
    root.append(cards);
  }
  inspectRail(root, rl) {
    const A = this.app; if (!rl) return;
    root.append(objHead(rl.tunnel ? "Subway line" : "Railway", rl.name || rl.id, "rail", (v) => { rl.name = v; this.renderLayers(); }));
    const cards = el("div", { class: "cards" });
    const rebuild = () => A.rebuild("rail");
    cards.append(card("Line", "rail", [toggle("Bored tunnel (subway)", rl.tunnel, (v) => { A.pushUndo(); rl.tunnel = v; rl.pts.forEach((p) => { p[1] = v ? -13 : 0.03; }); rebuild(); }), toggle("Run trains", rl.trains !== false, (v) => { rl.trains = v; rebuild(); }), slider("Depth", -rl.pts[0][1], -2, 30, 0.5, (v, fin) => { if (!fin) return; A.pushUndo(); rl.pts.forEach((p) => { p[1] = -v; }); rebuild(); }, { unit: " m" })], true));
    cards.append(card("Points", "split", [el("div", { class: "cap", text: `${rl.pts.length} spline points` })], true));
    cards.append(el("div", { class: "btnrow", style: { gridColumn: "1 / -1" } }, [button(icon("trash", 13) + "Delete line", () => A.deleteSelection(), "btn danger")]));
    root.append(cards);
  }
  inspectLayer(root, id) {
    const A = this.app, S = A.settings, L = LAYERS.find((l) => l.id === id);
    root.append(objHead("Content layer", L.name, L.icon));
    const cards = el("div", { class: "cards" });
    const rb = (fin = true) => { if (fin) A.rebuild("layer:" + id); };
    const vis = toggle("Visible", A.scene.vis[id], (v) => { A.scene.setLayer(id, v); this.renderLayers(); });
    cards.append(card("Layer", L.icon, [vis, el("div", { class: "cap", text: L.desc })], true));
    const M = S.markings, F = S.furnish, ST = S.structures, RL = S.rail, T = S.traffic;
    switch (id) {
      case "ground": cards.append(card("Terrain", "ground", [slider("Extent beyond roads", ST.groundExtent, 40, 600, 10, (v, f) => { ST.groundExtent = v; rb(f); }, { unit: " m" }), slider("Cell size", ST.groundCell, 4, 30, 1, (v, f) => { ST.groundCell = v; rb(f); }, { unit: " m" })], true)); break;
      case "roads": cards.append(card("Sweep", "road", [slider("Station step", S.build.step, 1, 4, 0.5, (v, f) => { S.build.step = v; rb(f); }, { unit: " m" })], true)); break;
      case "markings": cards.append(card("Standard", "marking", [select("Scheme", M.standard, [["eu", "Europe (white centre)"], ["us", "US (yellow centre)"]], (v) => { M.standard = v; rb(); }), slider("Line width", M.lineWidth, 0.1, 0.2, 0.01, (v, f) => { M.lineWidth = v; rb(f); }, { unit: " m" }), slider("Dash length", M.dashLength, 1, 6, 0.5, (v, f) => { M.dashLength = v; rb(f); }, { unit: " m" }), slider("Dash gap", M.dashGap, 2, 12, 0.5, (v, f) => { M.dashGap = v; rb(f); }, { unit: " m" }), slider("Paint wear", M.wear, 0, 1, 0.05, (v, f) => { M.wear = v; rb(f); })]), card("Elements", "grid", [toggle("Stop lines", M.stopLine, (v) => { M.stopLine = v; rb(); }), toggle("Zebra crossings", M.zebra, (v) => { M.zebra = v; rb(); }), toggle("Turn arrows", M.arrows, (v) => { M.arrows = v; rb(); }), toggle("Bike symbols", M.bikeSymbols, (v) => { M.bikeSymbols = v; rb(); }), toggle("Parking bays", M.parkingBays, (v) => { M.parkingBays = v; rb(); }), toggle("Gore chevrons", M.goreChevrons, (v) => { M.goreChevrons = v; rb(); })])); break;
      case "furniture": cards.append(card("Surface features", "ruler", [toggle("Raised medians", F.medians, (v) => { F.medians = v; rb(); }), toggle("Speed bumps", F.speedBumps, (v) => { F.speedBumps = v; rb(); }), slider("Bump spacing", F.bumpSpacing, 40, 200, 10, (v, f) => { F.bumpSpacing = v; rb(f); }, { unit: " m" })], true)); break;
      case "damage": cards.append(card("Damage", "paint", [slider("Amount", F.damage, 0, 1, 0.05, (v, f) => { F.damage = v; rb(f); }), toggle("Potholes", F.potholes, (v) => { F.potholes = v; rb(); }), toggle("Crack webs", F.cracks, (v) => { F.cracks = v; rb(); }), toggle("Patches", F.patches, (v) => { F.patches = v; rb(); }), toggle("Manholes", F.manholes, (v) => { F.manholes = v; rb(); })], true)); break;
      case "signals": cards.append(card("Signals", "lamp", [toggle("Traffic lights", F.trafficLights, (v) => { F.trafficLights = v; rb(); }), el("div", { class: "cap", text: "Phases run in Simulate mode; lenses are emissive." })], true)); break;
      case "lighting": cards.append(card("Columns", "sun", [toggle("Streetlights", F.streetlights, (v) => { F.streetlights = v; rb(); }), slider("Spacing", F.lightSpacing, 18, 60, 2, (v, f) => { F.lightSpacing = v; rb(f); }, { unit: " m" })], true)); break;
      case "barriers": cards.append(card("Barriers", "cone", [toggle("Bollards", F.bollards, (v) => { F.bollards = v; rb(); }), select("Bollard design", F.bollardStyle, BOLLARD_STYLES, (v) => { F.bollardStyle = v; rb(); }), toggle("Pedestrian railings", F.railings, (v) => { F.railings = v; rb(); }), toggle("Guardrails", F.guardrails, (v) => { F.guardrails = v; rb(); })], true)); break;
      case "construction": cards.append(card("Roadworks", "cone", [toggle("Generate zones", F.construction, (v) => { F.construction = v; rb(); }), slider("Zones", F.constructionCount, 0, 6, 1, (v, f) => { F.constructionCount = v; rb(f); })], true)); break;
      case "billboards": cards.append(card("Billboards", "texture", [toggle("Enabled", F.billboards, (v) => { F.billboards = v; rb(); })], true)); break;
      case "parking": cards.append(card("Parking", "parking", [toggle("Parked cars", F.parking, (v) => { F.parking = v; rb(); }), slider("Occupancy", F.parkingOccupancy, 0, 1, 0.05, (v, f) => { F.parkingOccupancy = v; rb(f); })], true)); break;
      case "rail": cards.append(card("Track", "rail", [toggle("Railways", RL.enabled, (v) => { RL.enabled = v; rb(); }), toggle("Level crossings", RL.crossings, (v) => { RL.crossings = v; rb(); }), toggle("Crossing booms", RL.barriers, (v) => { RL.barriers = v; rb(); })], true)); break;
      case "subway": cards.append(card("Tunnels", "rail", [slider("Tube radius", RL.tunnelRadius, 2.4, 4.5, 0.1, (v, f) => { RL.tunnelRadius = v; rb(f); }, { unit: " m" }), toggle("X-ray ground", A.scene.xray, (v) => A.setXray(v))], true)); break;
      case "structures": cards.append(card("Bridges", "bridge", [toggle("Decks & piers", ST.bridges, (v) => { ST.bridges = v; rb(); }), slider("Pier spacing", ST.pierSpacing, 18, 50, 2, (v, f) => { ST.pierSpacing = v; rb(f); }, { unit: " m" }), toggle("Trenches & tunnels", ST.trenches, (v) => { ST.trenches = v; rb(); })], true)); break;
      case "traffic": cards.append(card("Vehicles", "car", [toggle("Enabled", T.enabled, (v) => { T.enabled = v; rb(); }), slider("Count", T.vehicles, 0, MAX_VEHICLES, 1, (v, f) => { T.vehicles = v; rb(f); }), slider("Speed scale", T.speedScale, 0.2, 2, 0.1, (v) => { T.speedScale = v; if (A.traffic) A.traffic.S.speedScale = v; }), toggle("Trains", T.trains, (v) => { T.trains = v; rb(); }), el("div", { class: "cap", text: `Hard cap ${MAX_VEHICLES} vehicles. Press Space to simulate.` })], true)); break;
    }
    root.append(cards);
  }
  inspectMaterial(root, name) {
    const A = this.app, st = A.textures[name];
    root.append(objHead("Texture layer stack", name, "texture"));
    const cards = el("div", { class: "cards" });
    let timer = null;
    const rebake = () => { clearTimeout(timer); timer = setTimeout(() => A.rebakeMaterial(name), 120); };
    cards.append(card("Mapping", "grid", [slider("UV scale", st.uvScale, 0.2, 12, 0.1, (v) => { st.uvScale = v; rebake(); }, { unit: " m" }), el("div", { class: "cap", text: `${st.kind} · ${A.settings.textureSize} px tileable` })], true));
    st.layers.forEach((layer, i) => {
      const fields = [];
      fields.push(toggle("Enabled", layer.on, (v) => { layer.on = v; rebake(); this.renderLayers(); }));
      if (layer.color) fields.push(color("Colour", layer.color, (v) => { layer.color = v; rebake(); }));
      if (layer.pattern !== undefined) fields.push(select("Pattern", layer.pattern, PAVING_PATTERNS, (v) => { layer.pattern = v; rebake(); }));
      for (const [k, min, max, step] of [["amount", 0, 1, 0.02], ["roughness", 0, 1, 0.02], ["variation", 0, 1, 0.02], ["depth", 0, 1, 0.02], ["size", 0.1, 2, 0.05], ["unit", 0.05, 1.2, 0.01], ["ratio", 1, 4, 0.5], ["jointWidth", 0.002, 0.15, 0.002], ["metal", 0, 1, 0.1], ["scale", 0.2, 5, 0.1], ["density", 0, 1, 0.02], ["strength", 0, 1, 0.02], ["spangle", 0, 1, 0.1]]) if (typeof layer[k] === "number") fields.push(slider(k, layer[k], min, max, step, (v) => { layer[k] = v; rebake(); }));
      if (typeof layer.seed === "number") fields.push(el("div", { class: "btnrow" }, [button("Reseed", () => { layer.seed = Math.floor(Math.random() * 1000); rebake(); }, "btn sm")]));
      cards.append(card(`${i + 1} · ${layer.name}`, "layers", fields, true));
    });
    cards.append(el("div", { class: "btnrow", style: { gridColumn: "1 / -1" } }, [button("Reset material", async () => { const { defaultStacks } = await import("../core/textures.js"); A.textures[name] = defaultStacks()[name]; A.rebakeMaterial(name); this.renderInspector(); }, "btn")]));
    root.append(cards);
    root.append(el("div", { class: "footer-note", text: "Stack edits re-bake this material live" }));
  }
}

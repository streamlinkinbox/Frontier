// Prop meshes (street furniture, vehicles) in a local frame: origin at ground, +Z forward, +X right, +Y up.
// Each builder returns a MeshBuilder; the app uploads it once and instances it with matrices.
import { MeshBuilder } from "./mesh.js";
import { lathe, sweep, frames, box, cylinder, loft, roundedRect, circle } from "./primitives.js";
import { add, mul, lerp } from "./math.js";

const cache = new Map();
export function prop(name, variant = "") { const key = name + "|" + variant; if (!cache.has(key)) cache.set(key, BUILDERS[name](variant)); return cache.get(key); }

// ---------------------------------------------------------------------------------------------
function streetlight(variant) {
  const mb = new MeshBuilder("streetlight");
  const steel = mb.material("steelGalv"), paint = mb.material("metalPainted"), lamp = mb.material("lamp");
  const H = variant === "tall" ? 12 : variant === "short" ? 6 : 9;
  // base flange + tapered octagonal column with a door section
  lathe(mb, steel, [{ r: 0.22, y: 0 }, { r: 0.22, y: 0.03 }, { r: 0.1, y: 0.03 }, { r: 0.1, y: 1.2 }, { r: 0.085, y: 1.25 }, { r: 0.085, y: H * 0.6 }, { r: 0.05, y: H }, { r: 0.045, y: H + 0.05 }, { r: 0, y: H + 0.05 }], 10, [0, 0, 0]);
  // outreach arm: curved bracket from the top of the column, 1.8 m out, rising 0.35 m
  const arm = []; for (let i = 0; i <= 8; i++) { const t = i / 8; arm.push([Math.sin(t * Math.PI / 2) * 1.8, H - 0.6 + t * 0.95 - (1 - Math.cos(t * Math.PI / 2)) * 0.25, 0]); }
  sweep(mb, steel, frames(arm), circle([0, 0, 0], 0.04, 8).map((p) => ({ x: p[0], y: p[2] })));
  // luminaire: flat cobra/LED housing with a lens underneath
  box(mb, paint, [1.95, H + 0.3, 0], [0.75, 0.13, 0.32], { bevel: 0.04 });
  box(mb, lamp, [2.0, H + 0.225, 0], [0.5, 0.02, 0.22]);
  return mb;
}
function trafficLight(variant) {
  const mb = new MeshBuilder("trafficlight");
  const steel = mb.material("steelGalv"), housing = mb.material("metalPainted"), off = mb.material("lampOff");
  const mast = variant === "mast";
  const poleH = mast ? 6.2 : 4.3;
  lathe(mb, steel, [{ r: 0.2, y: 0 }, { r: 0.2, y: 0.03 }, { r: 0.09, y: 0.03 }, { r: 0.09, y: 1.0 }, { r: 0.075, y: 1.05 }, { r: 0.07, y: poleH }, { r: 0, y: poleH + 0.03 }], 10);
  const buildHead = (x, y, z) => {
    const h = new MeshBuilder("head");
    const H = h.material("metalPainted"), O = h.material("lampOff");
    box(h, H, [0, 0, 0], [0.33, 1.05, 0.26], { bevel: 0.02 });
    box(h, H, [0, 0, -0.14], [0.62, 1.36, 0.02], { bevel: 0.01 });
    for (let k = 0; k < 3; k++) {
      const ly = 0.35 - k * 0.35;
      const vis = new MeshBuilder("v"); const VM = vis.material("metalPainted");
      lathe(vis, VM, [{ r: 0.105, y: 0 }, { r: 0.11, y: 0.22 }], 12, [0, 0, 0], { angle0: Math.PI, angle1: Math.PI * 2 });
      // visor axis is Y in lathe space → rotate so it points +Z (forward) and the open side faces down
      vis.transform((p) => [p[0], ly - p[2], 0.13 + p[1]]);
      h.append(vis);
      const lens = new MeshBuilder("l"); const LM = lens.material("lampOff");
      cylinder(lens, LM, 0, 0, 0, 0.01, 0.1, 0.1, 14, { capTop: true });
      lens.transform((p) => [p[0], ly - p[2], 0.13 + p[1]]);
      h.append(lens);
    }
    h.translate([x, y, z]);
    mb.append(h);
  };
  if (mast) {
    // mast arm 5.5 m over the carriageway (to the LEFT of the pole in local space → app orients it)
    const arm = [[0, poleH - 0.3, 0], [-0.6, poleH - 0.1, 0], [-2.0, poleH, 0], [-5.5, poleH, 0]];
    sweep(mb, steel, frames(arm), circle([0, 0, 0], 0.06, 8).map((p) => ({ x: p[0], y: p[2] })));
    buildHead(-4.6, poleH - 0.75, 0.0);
    buildHead(0.0, 3.0, 0.2);
  } else {
    buildHead(0.0, 3.3, 0.2);
    buildHead(0.0, 2.1, 0.2); // secondary (near-side) head
  }
  return mb;
}
/** Lit lens disc (emissive) — instanced separately per colour. */
function lens() { const mb = new MeshBuilder("lens"); cylinder(mb, mb.material("lamp"), 0, 0, 0, 0.012, 0.095, 0.095, 14, { capTop: true }); mb.transform((p) => [p[0], -p[2], p[1]]); return mb; }

function bollard(variant) {
  const mb = new MeshBuilder("bollard");
  if (variant === "steel") lathe(mb, mb.material("metalPainted"), [{ r: 0, y: 0 }, { r: 0.07, y: 0 }, { r: 0.07, y: 0.85 }, { r: 0.065, y: 0.9 }, { r: 0.04, y: 0.95 }, { r: 0, y: 0.97 }], 14);
  else if (variant === "cast") lathe(mb, mb.material("metalPainted"), [{ r: 0, y: 0 }, { r: 0.12, y: 0 }, { r: 0.12, y: 0.08 }, { r: 0.085, y: 0.12 }, { r: 0.08, y: 0.55 }, { r: 0.1, y: 0.6 }, { r: 0.1, y: 0.66 }, { r: 0.075, y: 0.7 }, { r: 0.075, y: 0.86 }, { r: 0.095, y: 0.9 }, { r: 0.06, y: 1.02 }, { r: 0, y: 1.06 }], 16);
  else if (variant === "concrete") { box(mb, mb.material("concrete"), [0, 0.45, 0], [0.28, 0.9, 0.28], { bevel: 0.03 }); }
  else if (variant === "flex") { lathe(mb, mb.material("plastic"), [{ r: 0, y: 0 }, { r: 0.1, y: 0 }, { r: 0.05, y: 0.05 }, { r: 0.04, y: 0.75 }, { r: 0, y: 0.76 }], 12); lathe(mb, mb.material("paint"), [{ r: 0.042, y: 0.55 }, { r: 0.042, y: 0.65 }], 12); }
  else { // removable with reflective band
    lathe(mb, mb.material("steelGalv"), [{ r: 0, y: 0 }, { r: 0.09, y: 0 }, { r: 0.09, y: 0.04 }, { r: 0.055, y: 0.04 }, { r: 0.055, y: 0.98 }, { r: 0.04, y: 1.02 }, { r: 0, y: 1.03 }], 14);
    lathe(mb, mb.material("paint"), [{ r: 0.057, y: 0.75 }, { r: 0.057, y: 0.88 }], 14);
  }
  return mb;
}
/** Pedestrian guardrail panel: 2 m long, 1.0 m high, vertical infill bars. Origin at the start post base, runs along +X. */
function guardrailPanel(variant) {
  const mb = new MeshBuilder("guardrail");
  const steel = mb.material(variant === "black" ? "metalPainted" : "steelGalv");
  const L = 2.0;
  cylinder(mb, steel, 0, 0, 0, 1.05, 0.03, 0.03, 8, { capTop: true });
  for (const y of [1.0, 0.15]) sweep(mb, steel, frames([[0, y, 0], [L, y, 0]]), roundedRect(0.05, 0.04, 0.012, 2));
  const n = 12; for (let i = 1; i < n; i++) { const x = (i / n) * L; sweep(mb, steel, frames([[x, 0.17, 0], [x, 0.98, 0]]), roundedRect(0.016, 0.016, 0.004, 1)); }
  return mb;
}
/** W-beam highway barrier: 4 m beam section (origin at start, +X along), post + blockout at the start. */
function wbeamSection() {
  const mb = new MeshBuilder("wbeam");
  const steel = mb.material("steelGalv");
  const prof = [{ x: 0, y: 0.5 }, { x: 0.04, y: 0.53 }, { x: 0.075, y: 0.6 }, { x: 0.04, y: 0.67 }, { x: 0, y: 0.69 }, { x: 0, y: 0.71 }, { x: 0.04, y: 0.73 }, { x: 0.075, y: 0.8 }, { x: 0.04, y: 0.87 }, { x: 0, y: 0.9 }];
  const rows = [];
  for (const x of [0, 4]) rows.push(prof.map((p) => mb.vertex([x, p.y, -p.x], [x, p.y], [0, 1, 0.5, 0])));
  mb.gridFacing(steel, rows, undefined, [0, 0, -1]);
  // back face (thin plate) so it reads from both sides
  const rows2 = []; for (const x of [0, 4]) rows2.push(prof.map((p) => mb.vertex([x, p.y, -p.x + 0.004], [x, p.y], [0, 1, 0.5, 0])));
  mb.gridFacing(steel, rows2, undefined, [0, 0, 1]);
  // post (C-section simplified to a box 0.15 × 0.1) and timber blockout
  box(mb, steel, [0.05, 0.4, -0.2], [0.1, 0.78, 0.15]);
  box(mb, mb.material("wood"), [0.05, 0.7, -0.1], [0.15, 0.36, 0.2]);
  return mb;
}
/** Concrete step (Jersey) barrier, 4 m element. */
function jerseyBarrier() {
  const mb = new MeshBuilder("jersey");
  const con = mb.material("concrete");
  const prof = [{ x: -0.3, y: 0 }, { x: -0.3, y: 0.075 }, { x: -0.17, y: 0.33 }, { x: -0.08, y: 0.81 }, { x: 0.08, y: 0.81 }, { x: 0.17, y: 0.33 }, { x: 0.3, y: 0.075 }, { x: 0.3, y: 0 }];
  const rows = []; for (const z of [0, 4]) rows.push(prof.map((p) => mb.vertex([p.x, p.y, z], [z, p.y], [0, 1, 0.5, 0])));
  mb.gridFacing(con, rows, undefined, [-1, 0, 0]);
  // end caps as 3 quads each (the profile is symmetric: pair points k and 7−k)
  for (const [ri, dir] of [[0, -1], [1, 1]]) { const r = rows[ri]; mb.gridFacing(con, [[r[0], r[1], r[2], r[3]], [r[7], r[6], r[5], r[4]]], undefined, [0, 0, dir]); }
  mb.gridFacing(con, [[rows[0][7], rows[0][0]], [rows[1][7], rows[1][0]]], undefined, [0, -1, 0]); // underside
  return mb;
}
function billboard() {
  const mb = new MeshBuilder("billboard");
  const steel = mb.material("metalPainted"), face = mb.material("billboard"), lamp = mb.material("lamp");
  const W = 12.2, Hp = 3.66, top = 12; // standard 12.2 × 3.66 m bulletin
  cylinder(mb, steel, 0, 0, 0, top - Hp - 0.6, 0.45, 0.4, 16);
  box(mb, steel, [0, top - Hp / 2, 0], [W + 0.4, Hp + 0.4, 0.5], { bevel: 0.02 });
  box(mb, face, [0, top - Hp / 2, 0.27], [W, Hp, 0.03]);
  box(mb, face, [0, top - Hp / 2, -0.27], [W, Hp, 0.03]);
  // catwalk & floodlights
  box(mb, steel, [0, top - Hp - 0.3, 0.5], [W, 0.06, 0.8]);
  for (let i = -2; i <= 2; i++) { box(mb, steel, [i * 2.6, top - Hp - 0.2, 1.1], [0.3, 0.2, 0.3], { bevel: 0.02 }); box(mb, lamp, [i * 2.6, top - Hp - 0.1, 1.0], [0.26, 0.04, 0.2]); }
  return mb;
}
function trafficCone() {
  const mb = new MeshBuilder("cone");
  lathe(mb, mb.material("plastic"), [{ r: 0, y: 0 }, { r: 0.2, y: 0 }, { r: 0.2, y: 0.03 }, { r: 0.13, y: 0.04 }, { r: 0.1, y: 0.25 }, { r: 0.1, y: 0.4 }, { r: 0.05, y: 0.72 }, { r: 0.02, y: 0.75 }, { r: 0, y: 0.75 }], 14);
  lathe(mb, mb.material("paint"), [{ r: 0.102, y: 0.26 }, { r: 0.098, y: 0.39 }], 14);
  return mb;
}
function waterBarrier() { // plastic water-filled 1.5 m module
  const mb = new MeshBuilder("waterbarrier");
  const pl = mb.material("plastic");
  const prof = [{ x: -0.28, y: 0 }, { x: -0.28, y: 0.1 }, { x: -0.2, y: 0.5 }, { x: -0.12, y: 0.8 }, { x: 0.12, y: 0.8 }, { x: 0.2, y: 0.5 }, { x: 0.28, y: 0.1 }, { x: 0.28, y: 0 }];
  const rings = [-0.72, -0.7, 0.7, 0.72].map((z, i) => prof.map((p) => [p.x * (i === 0 || i === 3 ? 0.85 : 1), p.y * (i === 0 || i === 3 ? 0.95 : 1), z]));
  loft(mb, pl, rings, { closed: false });
  return mb;
}
function barricade() { // striped A-frame / type III barricade
  const mb = new MeshBuilder("barricade");
  const steel = mb.material("steelGalv"), w = mb.material("paint"), o = mb.material("plastic");
  for (const x of [-0.9, 0.9]) { box(mb, steel, [x, 0.6, 0], [0.05, 1.2, 0.05]); box(mb, steel, [x, 0.03, 0], [0.05, 0.06, 0.9]); }
  for (const y of [0.4, 0.75, 1.1]) { for (let i = 0; i < 6; i++) box(mb, i % 2 ? w : o, [-0.75 + i * 0.3, y, 0], [0.3, 0.2, 0.025]); }
  return mb;
}
function roadworkSign() {
  const mb = new MeshBuilder("rwsign");
  cylinder(mb, mb.material("steelGalv"), 0, 0, 0, 2.2, 0.03, 0.03, 8);
  box(mb, mb.material("plastic"), [0, 2.0, 0.02], [0.9, 0.9, 0.02], { bevel: 0.03 });
  box(mb, mb.material("paint"), [0, 2.0, 0.035], [0.75, 0.75, 0.01], { bevel: 0.03 });
  return mb;
}
function signPost(variant) {
  const mb = new MeshBuilder("sign");
  cylinder(mb, mb.material("steelGalv"), 0, 0, 0, 2.4, 0.04, 0.04, 8);
  if (variant === "stop") { const o = new MeshBuilder("o"); lathe(o, o.material("plastic"), [{ r: 0, y: 0 }, { r: 0.38, y: 0 }, { r: 0.38, y: 0.02 }, { r: 0, y: 0.02 }], 8); o.transform((p) => [p[0], 2.1 - p[2], 0.03 + p[1]]); mb.append(o); }
  else if (variant === "yield") { const t = new MeshBuilder("t"); const ids = [[-0.4, 2.5, 0.03], [0.4, 2.5, 0.03], [0, 1.8, 0.03]].map((p) => t.vertex(p)); t.tri(t.material("paint"), ids[0], ids[1], ids[2]); mb.append(t); }
  else box(mb, mb.material("sign"), [0, 2.1, 0.03], [0.6, 0.6, 0.02], { bevel: 0.02 });
  return mb;
}
function manholeRing() { // slightly raised iron rim (decal provides the lid pattern)
  const mb = new MeshBuilder("manhole");
  lathe(mb, mb.material("metalPainted"), [{ r: 0.34, y: 0 }, { r: 0.36, y: 0.012 }, { r: 0.3, y: 0.014 }, { r: 0, y: 0.014 }], 20);
  return mb;
}
function treeStump() { return new MeshBuilder("none"); }

// ----- vehicles: lofted bodies with rounded sections ----------------------------------------------
function carBody(kind) {
  const mb = new MeshBuilder("car-" + kind);
  const paint = mb.material("carPaint"), glass = mb.material("glass"), rubber = mb.material("rubber"), chrome = mb.material("metal"), dark = mb.material("metalPainted"), lamp = mb.material("lamp"), red = mb.material("lampRed");
  // dimensions
  const D = { sedan: { L: 4.6, W: 1.82, H: 1.45, wheel: 0.33, wb: 2.75, roof: [0.24, 0.68], tail: 0.68 }, hatch: { L: 4.1, W: 1.76, H: 1.5, wheel: 0.31, wb: 2.6, roof: [0.12, 0.72], tail: 0.92 }, suv: { L: 4.75, W: 1.9, H: 1.75, wheel: 0.37, wb: 2.85, roof: [0.12, 0.7], tail: 0.95 }, van: { L: 5.2, W: 2.0, H: 2.2, wheel: 0.36, wb: 3.3, roof: [0.06, 0.86], tail: 0.98 }, bus: { L: 12, W: 2.55, H: 3.2, wheel: 0.5, wb: 6.0, roof: [0.1, 0.98] }, truck: { L: 8.5, W: 2.5, H: 3.4, wheel: 0.5, wb: 4.8, roof: [0.1, 0.4] } }[kind] || D.sedan;
  const { L, W, H, wheel, wb } = D;
  const ground = wheel * 0.55 + 0.1; // sill height
  // body side profile: stations along length (z from -L/2 rear to +L/2 front) → {yLow, yHigh(roofline), widthScale}
  const prof = [];
  const bus = kind === "bus" || kind === "truck" || kind === "van";
  const N = 22;
  for (let i = 0; i <= N; i++) {
    const t = i / N, z = -L / 2 + t * L;
    let top;
    if (bus) top = H * (t < 0.03 || t > 0.97 ? 0.92 : 1);
    else {
      // bonnet / cabin / boot silhouette
      const [a, b] = D.roof; // cabin span in t
      const hood = H * 0.62, roof = H, boot = H * (D.tail || 0.68);
      if (t < a) top = lerp(boot, roof, smooth01((t) / a) ** 1.6);
      else if (t < b) top = roof - 0.02 * Math.sin(((t - a) / (b - a)) * Math.PI);
      else top = lerp(roof, hood, smooth01((t - b) / (1 - b)) ** 0.7);
      if (t < 0.02) top = boot * 0.97; if (t > 0.985) top = hood * 0.9;
    }
    const width = W * (bus ? 1 : 0.86 + 0.14 * Math.sin(Math.PI * Math.min(1, t * 1.1)) ** 0.5);
    prof.push({ z, top, w: width, belt: ground + (bus ? H * 0.35 : H * 0.42) });
  }
  // rings: 14 points around a rounded cross-section (sill → belt → shoulder → roof)
  const ringOf = (p, inset = 0) => {
    const hw = p.w / 2 - inset, pts = [];
    const roofW = bus ? hw * 0.94 : hw * 0.72, shoulderY = p.belt + (p.top - p.belt) * 0.1;
    const spec = [[-hw * 0.9, ground], [-hw, ground + 0.08], [-hw, p.belt], [-hw * 0.98, shoulderY], [-roofW, p.top - 0.02], [-roofW * 0.7, p.top], [0, p.top + 0.005], [roofW * 0.7, p.top], [roofW, p.top - 0.02], [hw * 0.98, shoulderY], [hw, p.belt], [hw, ground + 0.08], [hw * 0.9, ground], [0, ground - 0.02]];
    for (const [x, y] of spec) pts.push([x, y, p.z]);
    return pts;
  };
  const rings = prof.map((p) => ringOf(p));
  // front/rear closed by scaling the end rings toward their centre (quads + small caps)
  const endRing = (p, k) => ringOf(p).map((q) => [q[0] * k, p.belt + (q[1] - p.belt) * k, q[2]]);
  const all = [endRing(prof[0], 0.35), ...rings, endRing(prof[prof.length - 1], 0.35)];
  loft(mb, paint, all, { caps: true });
  // glasshouse: inset ring band above the belt line between cabin stations, rendered as glass
  if (!bus) {
    const [a, b] = D.roof; const g = [];
    for (let i = 0; i <= N; i++) { const t = i / N; if (t < a - 0.08 || t > b + 0.12) continue; const p = prof[i]; const hw = p.w / 2 + 0.004; const roofW = hw * 0.72 + 0.004; const sy = p.belt + 0.02; g.push([[-hw * 0.985, sy], [-roofW, p.top - 0.03], [roofW, p.top - 0.03], [hw * 0.985, sy]].map(([x, y]) => [x, y, p.z])); }
    if (g.length > 1) { loft(mb, glass, g, { closed: false }); }
  } else {
    // windows band
    const g = []; for (let i = 0; i <= N; i++) { const t = i / N; if (t < 0.05 || t > 0.95) continue; const p = prof[i]; const hw = p.w / 2 + 0.004; g.push([[-hw, p.belt + 0.05], [-hw, p.top - 0.35], [hw, p.top - 0.35], [hw, p.belt + 0.05]].map(([x, y]) => [x, y, p.z])); }
    loft(mb, glass, g, { closed: false });
  }
  // wheels: tyre (lathe torus-ish) + rim
  const axles = kind === "bus" ? [-L / 2 + 1.6, -L / 2 + 1.6 + wb] : kind === "truck" ? [-L / 2 + 1.5, -L / 2 + 2.9, L / 2 - 1.6] : [-wb / 2, wb / 2];
  for (const z of axles) for (const side of [-1, 1]) {
    const x = side * (W / 2 - 0.08);
    const tyre = new MeshBuilder("t");
    const tw = wheel * 0.62;
    lathe(tyre, tyre.material("rubber"), [{ r: wheel * 0.6, y: -tw / 2 }, { r: wheel * 0.92, y: -tw / 2 }, { r: wheel, y: -tw / 2 + 0.03 }, { r: wheel, y: tw / 2 - 0.03 }, { r: wheel * 0.92, y: tw / 2 }, { r: wheel * 0.6, y: tw / 2 }], 20);
    lathe(tyre, tyre.material("metal"), [{ r: 0, y: side * tw * 0.42 }, { r: wheel * 0.3, y: side * tw * 0.47 }, { r: wheel * 0.58, y: side * tw * 0.38 }, { r: wheel * 0.62, y: -side * tw * 0.2 }], 20, [0, 0, 0], { facing: [0, side, 0] });
    tyre.transform((p) => [x + p[1], wheel + p[0], z - p[2]]);
    mb.append(tyre);
  }
  // lights, bumpers, mirrors
  const fz = L / 2, rz = -L / 2;
  for (const side of [-1, 1]) {
    box(mb, lamp, [side * (W / 2 - 0.3), ground + H * 0.35, fz - 0.01], [0.42, 0.14, 0.06], { bevel: 0.02 });
    box(mb, red, [side * (W / 2 - 0.3), ground + H * 0.4, rz + 0.01], [0.4, 0.12, 0.06], { bevel: 0.02 });
    if (!bus) box(mb, dark, [side * (W / 2 + 0.08), ground + H * 0.55, L / 2 - D.roof[1] * L - 0.1 + L * 0.02], [0.2, 0.12, 0.16], { bevel: 0.03 });
  }
  box(mb, dark, [0, ground + 0.06, fz - 0.02], [W * 0.98, 0.24, 0.12], { bevel: 0.03 });
  box(mb, dark, [0, ground + 0.06, rz + 0.02], [W * 0.98, 0.24, 0.12], { bevel: 0.03 });
  box(mb, dark, [0, ground + H * 0.15, fz - 0.02], [W * 0.55, 0.16, 0.05], { bevel: 0.02 }); // grille
  return mb;
}
const smooth01 = (t) => { t = Math.max(0, Math.min(1, t)); return t * t * (3 - 2 * t); };

function trainCar() {
  const mb = new MeshBuilder("train");
  const body = mb.material("carPaint"), glass = mb.material("glass"), dark = mb.material("metalPainted"), steel = mb.material("metal");
  const L = 20, W = 2.9, H = 3.6, floor = 1.1;
  const sec = roundedRect(W, H - floor, 0.35, 4).map((p) => ({ x: p.x, y: p.y + floor + (H - floor) / 2 }));
  const rings = [-L / 2 + 0.2, -L / 2, L / 2, L / 2 - 0.2].map((z, i) => sec.map((p) => [p.x * (i === 0 || i === 3 ? 0.9 : 1), floor + (p.y - floor) * (i === 0 || i === 3 ? 0.95 : 1), z]));
  loft(mb, body, [rings[1], rings[2]], { caps: true });
  // window band & doors
  for (let i = 0; i < 6; i++) { const z = -L / 2 + 2.2 + i * 3; for (const side of [-1, 1]) box(mb, glass, [side * (W / 2 + 0.005), floor + 1.7, z], [0.01, 1.0, 1.6]); }
  for (const z of [-L / 2 + 1, L / 2 - 1]) for (const side of [-1, 1]) box(mb, dark, [side * (W / 2 + 0.006), floor + 1.1, z], [0.01, 2.1, 1.3]);
  // bogies
  for (const z of [-L / 2 + 3, L / 2 - 3]) { box(mb, dark, [0, 0.6, z], [2.6, 0.5, 2.6], { bevel: 0.05 }); for (const dz of [-0.9, 0.9]) for (const side of [-1, 1]) { const w = new MeshBuilder("w"); lathe(w, w.material("metal"), [{ r: 0, y: -0.07 }, { r: 0.46, y: -0.07 }, { r: 0.46, y: 0.07 }, { r: 0, y: 0.07 }], 16); w.transform((p) => [side * 0.72 + p[1], 0.46 + p[0], z + dz - p[2]]); mb.append(w); } }
  return mb;
}

const BUILDERS = { streetlight, trafficLight, lens, bollard, guardrailPanel, wbeamSection, jerseyBarrier, billboard, trafficCone, waterBarrier, barricade, roadworkSign, signPost, manholeRing, treeStump, carBody, trainCar };
export const VEHICLE_KINDS = ["sedan", "hatch", "suv", "van", "bus", "truck"];
export const VEHICLE_LENGTH = { sedan: 4.6, hatch: 4.1, suv: 4.75, van: 5.2, bus: 12, truck: 8.5 };
export const BOLLARD_STYLES = ["steel", "cast", "concrete", "flex", "removable"];

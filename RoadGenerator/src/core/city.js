// City / network generators: grid, organic radial, highway with diamond interchange, cloverleaf, rail town.
// Interchange dimensions follow FHWA/Caltrans practice: carriageway offset 8.5 m, loop radius 60–75 m,
// ramp grade ≤ 6 %, deck elevation 7.2 m (5.2 m clearance + 1.6 m girder + surfacing), gore angle ≈ 10–12°.
import { Network } from "./network.js";
import { cloneProfile } from "./profile.js";
import { makeRng, lerp } from "./math.js";

export const PRESETS = [
  { id: "grid", name: "Grid city", desc: "Avenue/collector/local hierarchy on a jittered grid with parking streets and a rail line." },
  { id: "organic", name: "Radial town", desc: "Ring roads and radial spokes with curved splines." },
  { id: "diamond", name: "Motorway + diamond", desc: "Elevated dual carriageway over an avenue with four ramps." },
  { id: "cloverleaf", name: "Cloverleaf", desc: "Two motorways, four loop ramps and four outer connectors." },
  { id: "demo", name: "Showcase", desc: "A bit of everything: junctions, bend, bridge, trench, railway crossing, subway." },
];

export function defaultCitySettings() { return { preset: "grid", seed: 3, blocksX: 4, blocksY: 3, blockW: 110, blockH: 80, curviness: 0.25, rail: true, subway: true, parking: true }; }

export function generateCity(S = defaultCitySettings()) {
  S = { ...defaultCitySettings(), ...S };
  const g = { grid, organic, diamond, cloverleaf, demo }[S.preset] || grid;
  return g(S, makeRng(S.seed));
}

// ---------------------------------------------------------------------------------------------
function grid(S, rng) {
  const net = new Network();
  const nx = S.blocksX, ny = S.blocksY, W = S.blockW, H = S.blockH;
  const x0 = -(nx * W) / 2, z0 = -(ny * H) / 2;
  const nodes = [];
  for (let j = 0; j <= ny; j++) { nodes.push([]); for (let i = 0; i <= nx; i++) {
    const jit = S.curviness * 10;
    const edge = i === 0 || i === nx || j === 0 || j === ny;
    const p = [x0 + i * W + (edge ? 0 : (rng() - 0.5) * jit), 0, z0 + j * H + (edge ? 0 : (rng() - 0.5) * jit)];
    const major = (i % 3 === 1) || (j % 2 === 1);
    nodes[j].push(net.addNode(p, { radius: major ? 11 : 8, name: `N${i}${j}` }));
  } }
  const prof = (i, j, horizontal) => {
    if (horizontal) return j % 2 === 1 ? "avenue" : j === 0 || j === ny ? "collector" : (i % 2 ? "local" : "oneway");
    return i % 3 === 1 ? "avenue" : i === 0 || i === nx ? "collector" : "local";
  };
  // horizontal roads
  for (let j = 0; j <= ny; j++) for (let i = 0; i < nx; i++) {
    if (!(j === 0 || j === ny || j % 2 === 1) && rng() < 0.12) continue; // drop a few locals
    const a = nodes[j][i], b = nodes[j][i + 1];
    const pts = S.curviness > 0 && rng() < S.curviness ? [[(a.p[0] + b.p[0]) / 2, 0, (a.p[2] + b.p[2]) / 2 + (rng() - 0.5) * S.curviness * 30]] : [];
    net.addRoad(a, b, { profile: prof(i, j, true), pts, name: `${j % 2 ? "Avenue" : "Street"} ${j + 1}` });
  }
  for (let i = 0; i <= nx; i++) for (let j = 0; j < ny; j++) {
    if (!(i === 0 || i === nx || i % 3 === 1) && rng() < 0.12) continue;
    const a = nodes[j][i], b = nodes[j + 1][i];
    const pts = S.curviness > 0 && rng() < S.curviness ? [[(a.p[0] + b.p[0]) / 2 + (rng() - 0.5) * S.curviness * 30, 0, (a.p[2] + b.p[2]) / 2]] : [];
    net.addRoad(a, b, { profile: prof(i, j, false), pts, name: `${i % 3 === 1 ? "Avenue" : "Road"} ${String.fromCharCode(65 + i)}` });
  }
  // a diagonal connector to exercise Y / acute junctions
  if (nx >= 3 && ny >= 2) {
    const a = nodes[0][1], b = nodes[1][2];
    net.addRoad(a, b, { profile: "collector", name: "Diagonal" });
  }
  // railway along the south edge + level crossings on the north-south roads
  if (S.rail) {
    const z = z0 + ny * H + 45;
    net.addRail([[x0 - 150, 0.03, z + 20], [x0 + nx * W * 0.3, 0.03, z], [x0 + nx * W + 150, 0.03, z - 25]], { name: "Main line" });
    for (let i = 0; i <= nx; i++) if (i % 3 === 1 || i === 0 || i === nx) { const n = nodes[ny][i]; const e = net.addNode([n.p[0], 0, z + 60], { radius: 6 }); net.addRoad(n, e, { profile: i % 3 === 1 ? "avenue" : "local", name: "Crossing road" }); }
  }
  // subway under the first avenue
  if (S.subway) { const i = 1; const x = x0 + i * W; net.addRail([[x + 6, -14, z0 - 120], [x + 6, -14, z0 + ny * H + 120]], { tunnel: true, name: "Metro line 1" }); }
  net.nodes.forEach((n) => { n.p[1] = 0; });
  return net;
}

function organic(S, rng) {
  const net = new Network();
  const rings = [110, 230, 360], spokes = 8;
  const c = net.addNode([0, 0, 0], { radius: 14, name: "Centre" });
  const ringNodes = rings.map((r, k) => Array.from({ length: spokes }, (_, i) => { const a = (i / spokes) * Math.PI * 2 + (k % 2) * 0.12 + (rng() - 0.5) * 0.08; return net.addNode([Math.cos(a) * r, 0, Math.sin(a) * r], { radius: k === 2 ? 10 : 9, angle: a }); }));
  // spokes
  for (let i = 0; i < spokes; i++) {
    let prev = c;
    for (let k = 0; k < rings.length; k++) {
      if (k === 1 && i % 2 === 1) break; // minor spokes stop at the inner ring
      const n = ringNodes[k][i];
      if (k === 0 && i % 2 === 1) { prev = n; continue; }
      net.addRoad(prev, n, { profile: k === 0 ? (i % 2 ? "local" : "avenue") : i % 2 ? "collector" : "avenue", name: `Spoke ${i + 1}` });
      prev = n;
    }
  }
  // inner spokes for minor directions
  for (let i = 1; i < spokes; i += 2) net.addRoad(c, ringNodes[0][i], { profile: "local" });
  // rings (curved segments via arc midpoints)
  rings.forEach((r, k) => {
    for (let i = 0; i < spokes; i++) {
      const a = ringNodes[k][i], b = ringNodes[k][(i + 1) % spokes];
      const a0 = Math.atan2(a.p[2], a.p[0]), a1 = Math.atan2(b.p[2], b.p[0]);
      let d = a1 - a0; while (d <= 0) d += Math.PI * 2;
      const pts = [1 / 3, 2 / 3].map((t) => { const ang = a0 + d * t; return [Math.cos(ang) * r, 0, Math.sin(ang) * r]; });
      if (k === 2 && rng() < 0.3) continue;
      net.addRoad(a, b, { profile: k === 0 ? "oneway" : k === 1 ? "collector" : "avenue", pts, name: `Ring ${k + 1}` });
    }
  });
  if (S.rail) net.addRail([[-520, 0.03, 300], [-100, 0.03, 290], [300, 0.03, 310], [520, 0.03, 340]], { name: "Branch line" });
  return net;
}

function arcPts(cx, cz, r, a0, a1, n, yA = 0, yB = 0) { const o = []; for (let i = 1; i < n; i++) { const t = i / n, a = lerp(a0, a1, t); o.push([cx + Math.cos(a) * r, lerp(yA, yB, t), cz + Math.sin(a) * r]); } return o; }

/** Dual carriageway as two one-way roads with an 8.5 m centreline offset; returns [eastbound, westbound]. */
function dualCarriageway(net, x0, x1, z, y, name, offset = 8.5, elev = null) {
  const prof = "carriageway";
  const E = [net.addNode([x0, 0, z + offset], { radius: 2.5 }), net.addNode([x1, 0, z + offset], { radius: 2.5 })];
  const W = [net.addNode([x1, 0, z - offset], { radius: 2.5 }), net.addNode([x0, 0, z - offset], { radius: 2.5 })];
  const pts = elev ? elev.map(([x, yy]) => [x, yy, z + offset]) : [];
  const ptsW = elev ? [...elev].reverse().map(([x, yy]) => [x, yy, z - offset]) : [];
  const eb = net.addRoad(E[0], E[1], { profile: prof, pts, name: name + " EB" });
  const wb = net.addRoad(W[0], W[1], { profile: prof, pts: ptsW, name: name + " WB" });
  return { eb, wb, E, W };
}

function diamond(S, rng) {
  const net = new Network();
  const deck = 7.2;
  // elevated profile: flat at ground beyond ±420, up on a 4 % grade to the deck between ±150
  const elev = [[-420, 0], [-330, 2.8], [-240, 6.4], [-150, deck], [150, deck], [240, 6.4], [330, 2.8], [420, 0]];
  const hw = dualCarriageway(net, -700, 700, 0, 0, "M1", 8.5, elev);
  // the avenue underneath (north–south) with ramp terminals at z = ±95
  const a = net.addNode([0, 0, -420], { radius: 10 }), tN = net.addNode([0, 0, -95], { radius: 12, name: "North terminal" }), tS = net.addNode([0, 0, 95], { radius: 12, name: "South terminal" }), b = net.addNode([0, 0, 420], { radius: 10 });
  net.addRoad(a, tN, { profile: "avenue", name: "Avenue (N)" }); net.addRoad(tN, tS, { profile: "avenue", name: "Avenue (under)" }); net.addRoad(tS, b, { profile: "avenue", name: "Avenue (S)" });
  // ramps: split the carriageways at the ramp noses so each nose is a 3-arm junction with a 10° gore
  const splitAt = (roadId, s) => net.splitRoad(roadId, s);
  const yAt = (x) => { for (let i = 0; i + 1 < elev.length; i++) if (x >= elev[i][0] && x <= elev[i + 1][0]) return lerp(elev[i][1], elev[i + 1][1], (x - elev[i][0]) / (elev[i + 1][0] - elev[i][0])); return 0; };
  const ramp = (fromNode, toNode, via, name) => net.addRoad(fromNode, toNode, { profile: "ramp", pts: via, name });
  // eastbound exit (SW quadrant): nose at x=-360 on EB, terminal tS
  const ebNoseX = -360, ebEntryX = 360, wbNoseX = 360, wbEntryX = -360;
  const eb1 = splitAt(hw.eb.id, ebNoseX + 700).node; eb1.radius = 2.5; eb1.p[1] = yAt(ebNoseX);
  const eb2 = splitAt([...net.roads.values()].find((r) => r.a === eb1.id).id, ebEntryX - ebNoseX).node; eb2.radius = 2.5; eb2.p[1] = yAt(ebEntryX);
  const wbRoads = () => [...net.roads.values()].filter((r) => r.name.startsWith("M1 WB"));
  const wb1 = splitAt(hw.wb.id, 700 - wbNoseX).node; wb1.radius = 2.5; wb1.p[1] = yAt(wbNoseX);
  const wb2 = splitAt([...net.roads.values()].find((r) => r.a === wb1.id).id, wbNoseX - wbEntryX).node; wb2.radius = 2.5; wb2.p[1] = yAt(wbEntryX);
  // ramps peel away at ≈14° (parallel-type gore ≈ 60–80 m), drop/climb on ≤ 5 % and arrive square at the terminals
  const yE = yAt(ebNoseX);
  ramp(eb1, tS, [[-300, yE * 0.9, 26], [-215, yE * 0.5, 62], [-70, 0.3, 95]], "EB exit");
  ramp(tS, eb2, [[70, 0.3, 95], [215, yE * 0.5, 62], [300, yE * 0.9, 26]], "EB entry");
  ramp(wb1, tN, [[300, yE * 0.9, -26], [215, yE * 0.5, -62], [70, 0.3, -95]], "WB exit");
  ramp(tN, wb2, [[-70, 0.3, -95], [-215, yE * 0.5, -62], [-300, yE * 0.9, -26]], "WB entry");
  for (const r of net.roads.values()) if (r.profile.id === "carriageway") r.billboards = true;
  return net;
}

function cloverleaf(S, rng) {
  // Full cloverleaf: M1 (east–west) crosses over M2 (north–south) on a 7.2 m viaduct. One quadrant is designed
  // explicitly (loop EB→NB in the SE quadrant, outer connector EB→SB in the SW quadrant) and rotated 3× by 90°;
  // right-hand traffic makes the rotation map EB→SB→WB→NB consistently.
  const net = new Network();
  const deck = 7.2, off = 8.5;
  const elev = [[-440, 0], [-340, 2.6], [-250, 5.6], [-160, deck], [160, deck], [250, 5.6], [340, 2.6], [440, 0]];
  const yAt = (x) => { for (let i = 0; i + 1 < elev.length; i++) if (x >= elev[i][0] && x <= elev[i + 1][0]) return lerp(elev[i][1], elev[i + 1][1], (x - elev[i][0]) / (elev[i + 1][0] - elev[i][0])); return 0; };
  dualCarriageway(net, -760, 760, 0, 0, "M1", off, elev);
  const prof = "carriageway";
  const nb = { a: net.addNode([off, 0, 760], { radius: 2.5 }), b: net.addNode([off, 0, -760], { radius: 2.5 }) };
  const sb = { a: net.addNode([-off, 0, -760], { radius: 2.5 }), b: net.addNode([-off, 0, 760], { radius: 2.5 }) };
  net.addRoad(nb.a, nb.b, { profile: prof, name: "M2 NB" }); // travelling north (−z); right side is +x
  net.addRoad(sb.a, sb.b, { profile: prof, name: "M2 SB" });
  // split the carriageway piece that spans a world point (pieces keep the carriageway name)
  const pieceAt = (name, axis, v) => [...net.roads.values()].find((q) => q.name.startsWith(name) && Math.min(net.nodes.get(q.a).p[axis], net.nodes.get(q.b).p[axis]) < v && Math.max(net.nodes.get(q.a).p[axis], net.nodes.get(q.b).p[axis]) > v);
  const splitAt = (name, axis, v) => { const r = pieceAt(name, axis, v); if (!r) throw new Error(`no ${name} piece spans ${v}`); const A = net.nodes.get(r.a); const n = net.splitRoad(r.id, Math.abs(v - A.p[axis])).node; n.radius = 2.5; return n; };
  const nodeAt = (p) => {
    if (Math.abs(p[2] - off) < 0.1) { const n = splitAt("M1 EB", 0, p[0]); n.p[1] = yAt(p[0]); return n; }
    if (Math.abs(p[2] + off) < 0.1) { const n = splitAt("M1 WB", 0, p[0]); n.p[1] = yAt(p[0]); return n; }
    if (Math.abs(p[0] - off) < 0.1) return splitAt("M2 NB", 2, p[2]);
    return splitAt("M2 SB", 2, p[2]);
  };
  const rot = (p, k) => { let q = [p[0], p[1], p[2]]; for (let i = 0; i < k; i++) q = [-q[2], q[1], q[0]]; return q; };
  // --- quadrant template --------------------------------------------------------------------------------
  const alpha = (20 * Math.PI) / 180, ca = Math.cos(alpha), sa = Math.sin(alpha);
  // Loop: nose 30 m after the structure, 20° diverge, R = 55 m circle centred on the x = z diagonal (tangent to the nose line).
  const xn = 30, R = 55;
  const c = (R + off * ca - xn * sa) / (ca - sa);              // centre (c, c)
  const t = (c - xn) * ca + (c - off) * sa;                   // nose length to the tangent point
  const y0 = yAt(xn), yTan = Math.max(0.4, y0 - t * 0.03);
  const loopPts = arcPts(c, c, R, alpha - Math.PI / 2, Math.PI - alpha, 18, yTan, 0.4);
  const loopNodes = [[xn, y0, off], [off, 0, xn]];
  // Outer connector (right turn, before the structure): 60 m nose at 20°, then a gentle arc centred on x = −z.
  const xo = 450, noseL = 60;
  const P1 = [-xo + noseL * ca, 0, off + noseL * sa];
  const Rc = (-(P1[0]) - P1[2]) / (ca - sa);                   // centre on x = −z: (−P1x − sa·Rc) = −(P1z + ca·Rc)
  const cc = [P1[0] - sa * Rc, 0, P1[2] + ca * Rc];
  const connPts = [P1, ...arcPts(cc[0], cc[2], Rc, alpha - Math.PI / 2, -alpha, 12, 0, 0), [-P1[2], 0, -P1[0]]];
  const connNodes = [[-xo, 0, off], [-off, 0, xo]];
  const names = ["EB→NB", "SB→WB", "WB→SB", "NB→EB"], cnames = ["EB→SB", "SB→WB", "WB→NB", "NB→EB"];
  const quad = ["SE", "SW", "NW", "NE"], cquad = ["SW", "NW", "NE", "SE"];
  for (let k = 0; k < 4; k++) {
    const a = nodeAt(rot(loopNodes[0], k)), b = nodeAt(rot(loopNodes[1], k));
    const yA = a.p[1], yB = b.p[1];
    net.addRoad(a, b, { profile: "ramp", pts: loopPts.map((p, i) => { const q = rot(p, k); q[1] = lerp(Math.max(0.4, yA - t * 0.03), Math.max(0.4, yB - t * 0.03), (i + 1) / (loopPts.length + 1)); return q; }), name: `Loop ${quad[k]} (${names[k]})` });
  }
  for (let k = 0; k < 4; k++) {
    const a = nodeAt(rot(connNodes[0], k)), b = nodeAt(rot(connNodes[1], k));
    net.addRoad(a, b, { profile: "ramp", pts: connPts.map((p) => rot(p, k)), name: `Connector ${cquad[k]} (${cnames[k]})` });
  }
  for (const r of net.roads.values()) if (r.profile.id === "carriageway") r.billboards = true;
  return net;
}

function demo(S, rng) {
  const net = new Network();
  const c = net.addNode([0, 0, 0], { radius: 11, name: "Main cross" });
  const e = net.addNode([170, 0, 0], { radius: 10 }), w = net.addNode([-170, 0, 0], { radius: 9 }), n = net.addNode([0, 0, -160], { radius: 9 }), s = net.addNode([0, 0, 170], { radius: 8 });
  net.addRoad(w, c, { profile: "avenue", name: "High Street W" }); net.addRoad(c, e, { profile: "avenue", name: "High Street E" });
  net.addRoad(n, c, { profile: "collector", name: "Station Road" }); net.addRoad(c, s, { profile: "local", name: "Mill Lane", speedBumps: [] });
  // Y junction and a bend
  const y1 = net.addNode([170, 0, -150], { radius: 9 }), y2 = net.addNode([300, 0, -60], { radius: 8 });
  net.addRoad(e, y1, { profile: "collector", pts: [[200, 0, -80]], name: "Hill Road" }); net.addRoad(e, y2, { profile: "local", name: "Lane" }); net.addRoad(y1, y2, { profile: "local", pts: [[260, 0, -140]] });
  const b1 = net.addNode([-170, 0, 150], { radius: 8 }), b2 = net.addNode([-40, 0, 230], { radius: 8 });
  net.addRoad(w, b1, { profile: "local", name: "Bend in" }); net.addRoad(b1, b2, { profile: "collector", name: "Bend out" }); net.addRoad(s, b2, { profile: "local" });
  // T junction + trench underpass crossing under High Street E
  const t1 = net.addNode([90, 0, -200], { radius: 8 }), t2 = net.addNode([90, 0, 200], { radius: 8 });
  net.addRoad(t1, t2, { profile: "collector", pts: [[90, -6.8, -60], [90, -6.8, 60]], name: "Underpass" });
  // bridge: elevated link over Mill Lane
  const g1 = net.addNode([-120, 0, 90], { radius: 8 }), g2 = net.addNode([120, 0, 110], { radius: 8 });
  net.addRoad(g1, g2, { profile: "bridgeLink", pts: [[-40, 7.5, 95], [40, 7.5, 105]], name: "Viaduct" });
  net.addRoad(b1, g1, { profile: "local" }); net.addRoad(g2, e, { profile: "local", pts: [[170, 0, 70]] });
  // railway with a level crossing on Station Road, and a subway under High Street
  net.addRail([[-320, 0.03, -110], [-60, 0.03, -95], [250, 0.03, -100], [420, 0.03, -140]], { name: "Branch line" });
  net.addRail([[-330, -13, 8], [330, -13, 8]], { tunnel: true, name: "Metro" });
  return net;
}

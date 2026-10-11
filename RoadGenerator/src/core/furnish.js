// Street furniture, damage, construction, parking, barriers: placement rules → prop instances, decals,
// and extra solid/paint geometry (raised medians, speed bumps). All dimensions in metres.
import { MeshBuilder } from "./mesh.js";
import { roadPoint, roadFrame, lanes, trafficLanes, stationRange, approachSide, footprint } from "./query.js";
import { mat4, madd, mul, add, sub, norm, rightXZ, makeRng, lerp, clamp, rotXZ } from "./math.js";
import { VEHICLE_KINDS, VEHICLE_LENGTH } from "./props.js";
import { DECALS } from "./textures.js";

export function defaultFurnishSettings() {
  return {
    streetlights: true, lightSpacing: 32, trafficLights: true, bollards: true, bollardStyle: "steel", railings: true, guardrails: true,
    billboards: true, parking: true, parkingOccupancy: 0.6, speedBumps: true, bumpSpacing: 90, medians: true,
    damage: 0.5, potholes: true, cracks: true, patches: true, manholes: true, construction: true, constructionCount: 1, seed: 7,
  };
}

/** Right-handed prop basis: +Y up, +Z forward, +X = LEFT of forward (so the matrix has a positive determinant). */
export function basis(p, forward, scale = 1) {
  const f = norm([forward[0], 0, forward[2]]);
  const left = mul(rightXZ(f), -1);
  return mat4.fromBasis(mul(left, scale), [0, scale, 0], mul(f, scale), p);
}
/** Prop that runs along +X local (rails, barriers): forward = rightXZ(along). */
export function basisAlong(p, along, scale = 1) { return basis(p, rightXZ(along), scale); }
/** Decal box: unit cube → oriented box of size [w, h, l] centred at p with forward direction. */
export function decalBox(p, forward, w, l, h = 0.6) {
  const f = norm([forward[0], 0, forward[2]]); const r = rightXZ(f);
  return mat4.fromBasis(mul(r, w / 2), [0, h / 2, 0], mul(f, l / 2), p);
}

class Out {
  constructor() { this.instances = new Map(); this.decals = []; this.solid = new MeshBuilder("furnish"); this.paint = new MeshBuilder("furnish-paint"); this.signals = []; this.parked = []; this.zones = []; }
  add(prop, variant, matrix, group = "") { const key = prop + "|" + variant + "|" + group; let a = this.instances.get(key); if (!a) { a = { prop, variant, group, mats: [] }; this.instances.set(key, a); } a.mats.push(matrix); }
  decal(layerName, model, opts = {}) { this.decals.push({ layer: DECALS.indexOf(layerName), model, opacity: opts.opacity ?? 1, normalStrength: opts.normalStrength ?? 1, roughness: opts.roughness ?? 0.85 }); }
}

export function furnish(plan, S = defaultFurnishSettings()) {
  S = { ...defaultFurnishSettings(), ...S };
  const out = new Out();
  const rng = makeRng(S.seed);
  const roads = [...plan.roads.values()];
  // choose construction roads deterministically
  const constructionRoads = new Set();
  if (S.construction) { const candidates = roads.filter((R) => R.length - R.cutA - R.cutB > 70 && R.road.profile.edge === "curb"); for (let i = 0; i < S.constructionCount && candidates.length; i++) constructionRoads.add(candidates[Math.floor(rng() * candidates.length)]); }
  for (const R of roads) {
    const prof = R.road.profile, urban = prof.edge === "curb", fast = prof.speed >= 80;
    const { s0, s1 } = stationRange(R); if (s1 - s0 < 4) continue;
    const sec = R.section, fp = footprint(R);
    const NA = plan.nodes.get(R.road.a), NB = plan.nodes.get(R.road.b);
    const jA = NA?.kind === "junction", jB = NB?.kind === "junction";
    const ls = lanes(R);
    // ---- streetlights --------------------------------------------------------------------------
    if (S.streetlights && R.road.streetlights !== "none") {
      const spacing = fast ? 45 : S.lightSpacing;
      const bothSides = sec.widthL + sec.widthR > 14;
      const start = s0 + 10, end = s1 - 10;
      let k = 0;
      for (let s = start; s <= end; s += spacing, k++) {
        const sides = bothSides ? [1, -1] : [k % 2 === 0 ? 1 : -1];
        for (const side of sides) {
          const kit = side > 0 ? R.kitR : R.kitL;
          const off = urban ? (prof.curbWidth + Math.min(0.9, kit.width * 0.3)) : 1.5;
          const x = side * ((side > 0 ? sec.widthR : sec.widthL) + off);
          const ss = bothSides ? s + (side < 0 ? spacing / 2 : 0) : s;
          if (ss > end) continue;
          const F = roadFrame(R, ss, x);
          out.add("streetlight", fast ? "tall" : urban ? "" : "tall", basis(F.p, side > 0 ? F.d : mul(F.d, -1)));
        }
      }
    }
    // ---- barriers ------------------------------------------------------------------------------
    if (S.guardrails && R.road.guardrail !== "none") {
      const kind = R.road.guardrail === "auto" ? (fast || prof.edge === "flush" && prof.speed >= 70 ? "wbeam" : null) : R.road.guardrail;
      if (kind === "wbeam" || kind === "concrete") {
        for (const side of [1, -1]) {
          const x = side * ((side > 0 ? sec.widthR : sec.widthL) + 0.5);
          for (let s = s0 + 2; s + 4 <= s1 - 2; s += 4) {
            const a = roadPoint(R, s, x), b = roadPoint(R, s + 4, x);
            const along = side > 0 ? sub(b, a) : sub(a, b);
            if (kind === "wbeam") out.add("wbeamSection", "", basisAlong(side > 0 ? a : b, along));
            else out.add("jerseyBarrier", "", basis(a, sub(b, a)));
          }
        }
      }
      if (prof.medianType === "barrier" && sec.halfMedian > 0) {
        const xs = prof.median > 2.4 ? [-sec.halfMedian + 0.45, sec.halfMedian - 0.45] : [0];
        for (const x of xs) for (let s = s0 + 1; s + 4 <= s1 - 1; s += 4) { const a = roadPoint(R, s, x), b = roadPoint(R, s + 4, x); out.add("jerseyBarrier", "", basis(a, sub(b, a))); }
      }
    }
    // ---- raised medians (kerb + top surface + rounded noses) ---------------------------------
    if (S.medians && prof.medianType === "raised" && sec.halfMedian > 0.6) buildMedian(out.solid, R, s0 + (jA ? 7 : 1), s1 - (jB ? 7 : 1), prof);
    // ---- pedestrian railings near signalised junction corners ---------------------------------
    if (S.railings && urban && prof.speed >= 50) {
      for (const [isJ, atStart] of [[jA, true], [jB, false]]) {
        if (!isJ) continue;
        const from = atStart ? s0 + 5.5 : s1 - 5.5, dir = atStart ? 1 : -1;
        for (const side of [1, -1]) {
          const x = side * ((side > 0 ? sec.widthR : sec.widthL) + prof.curbWidth + 0.35);
          for (let k = 0; k < 6; k++) {
            const sA = from + dir * k * 2, sB = sA + dir * 2;
            if (sB < s0 + 1 || sB > s1 - 1) break;
            const a = roadPoint(R, sA, x), b = roadPoint(R, sB, x);
            out.add("guardrailPanel", "", basisAlong(a, sub(b, a)));
          }
        }
      }
    }
    // ---- billboards along fast roads -----------------------------------------------------------
    if (S.billboards && (R.road.billboards || fast) && s1 - s0 > 120) {
      for (let s = s0 + 60, k = 0; s < s1 - 60; s += 260, k++) {
        const side = k % 2 === 0 ? 1 : -1;
        const x = side * ((side > 0 ? fp.right : -fp.left) + 7);
        const F = roadFrame(R, s, x);
        // face oncoming traffic on the near carriageway
        out.add("billboard", "", basis(F.p, side > 0 ? mul(F.d, -1) : F.d));
      }
    }
    // ---- parking: parked cars ------------------------------------------------------------------
    if (S.parking) for (const l of ls.filter((l) => l.kind === "parking")) {
      const bay = 6.0, start = s0 + (jA ? 10 : 2), end = s1 - (jB ? 10 : 2);
      for (let s = start + 1; s + bay <= end; s += bay) {
        if (rng() > S.parkingOccupancy) continue;
        const kind = ["sedan", "sedan", "hatch", "suv", "van"][Math.floor(rng() * 5)];
        const len = VEHICLE_LENGTH[kind];
        const F = roadFrame(R, s + bay / 2 + (rng() - 0.5) * (bay - len - 0.4), l.xc + (rng() - 0.5) * 0.15);
        const fwd = l.side > 0 ? F.d : mul(F.d, -1);
        const colour = Math.floor(rng() * 6);
        out.add("carBody", kind, basis(F.p, rotXZ(fwd, (rng() - 0.5) * 0.02)), "c" + colour);
        out.parked.push({ R, s, x: l.xc });
      }
    }
    // ---- speed bumps on slow streets -----------------------------------------------------------
    const bumps = R.road.speedBumps.length ? R.road.speedBumps : (S.speedBumps && urban && prof.speed <= 40 && s1 - s0 > 60 ? rangeStations(s0 + 30, s1 - 30, S.bumpSpacing) : []);
    for (const s of bumps) buildSpeedBump(out, R, s, prof);
    // ---- damage decals -------------------------------------------------------------------------
    const dmg = clamp(S.damage * (R.road.potholes || 1) * R.road.wear * 2, 0, 1);
    const tl = trafficLanes(R);
    if (dmg > 0.02 && tl.length) {
      const L = s1 - s0, n = Math.floor(L / 25 * dmg * 2);
      for (let i = 0; i < n; i++) {
        const l = tl[Math.floor(rng() * tl.length)];
        const s = s0 + 4 + rng() * (L - 8), x = l.xc + (rng() - 0.5) * l.width * 0.6;
        const F = roadFrame(R, s, x, 0.0);
        const yaw = rng() * Math.PI * 2;
        const t = rng();
        if (t < 0.35 && S.potholes) { const d = 0.4 + rng() * 0.9; out.decal("pothole", decalBox(F.p, rotXZ(F.d, yaw), d, d * (0.8 + rng() * 0.4)), { roughness: 0.95 }); }
        else if (t < 0.7 && S.cracks) { const d = 2 + rng() * 3; out.decal("crackweb", decalBox(F.p, rotXZ(F.d, yaw), d, d), { opacity: 0.8 }); }
        else if (S.patches) { out.decal("patch", decalBox(F.p, F.d, 1.2 + rng() * 2, 2 + rng() * 4), { roughness: 0.7 }); }
      }
      // tyre marks & oil near stop lines
      for (const [isJ, atStart] of [[jA, true], [jB, false]]) {
        if (!isJ) continue;
        const nodeId = atStart ? R.road.a : R.road.b;
        const side = approachSide(R, nodeId);
        for (const l of tl.filter((l) => l.side === side)) {
          const s = atStart ? s0 + 9 : s1 - 9;
          const F = roadFrame(R, s, l.xc);
          out.decal("tyre", decalBox(F.p, F.d, l.width * 0.9, 6), { opacity: 0.6 });
          if (rng() < 0.5) out.decal("oil", decalBox(F.p, F.d, 1.2, 1.6), { roughness: 0.3 });
        }
      }
    }
    // ---- manholes & gullies -------------------------------------------------------------------
    if (S.manholes && urban) {
      for (let s = s0 + 12 + rng() * 10; s < s1 - 8; s += 38 + rng() * 10) {
        const x = (rng() < 0.5 ? -1 : 1) * (tl.length ? tl[0].xc * 0.4 : 0);
        const F = roadFrame(R, s, x); out.decal("manhole", decalBox(F.p, F.d, 0.7, 0.7), { roughness: 0.6 });
      }
      for (const side of [1, -1]) for (let s = s0 + 6; s < s1 - 4; s += 24) {
        const x = side * ((side > 0 ? sec.widthR : sec.widthL) - 0.3);
        const F = roadFrame(R, s, x); out.decal("grate", decalBox(F.p, F.d, 0.45, 0.75), { roughness: 0.6 });
      }
    }
    // ---- construction zone --------------------------------------------------------------------
    const cz = R.road.construction || (constructionRoads.has(R) ? { from: s0 + (s1 - s0) * 0.35, to: s0 + (s1 - s0) * 0.35 + 36 } : null);
    if (cz) buildConstruction(out, R, cz, rng);
  }
  // ---- junction furniture: traffic signals, bollards, stop signs --------------------------------
  for (const N of plan.nodes.values()) {
    if (N.kind !== "junction") continue;
    const urbanJ = N.arms.some((a) => a.R.road.profile.edge === "curb");
    const major = N.arms.some((a) => a.R.road.profile.speed >= 50) && N.arms.length >= 3;
    const ctl = N.node.control || "auto";
    const signalised = S.trafficLights && ctl !== "none" && ctl !== "signs" && (ctl === "signals" || (urbanJ && major));
    const sig = { nodeId: N.node.id, arms: [] };
    N.arms.forEach((arm, i) => {
      const R = arm.R, prof = R.road.profile;
      const cut = arm.cut, d = arm.d, r = arm.r;
      const nApp = trafficLanes(R).filter((l) => l.side === approachSide(R, N.node.id)).length;
      // pole on the drivers' right (−r side), 1 m past the cut on the footway
      if (signalised && nApp > 0) {
        const variant = nApp >= 2 ? "mast" : "";
        const base = add(madd(N.node.p, d, cut + 1.0), mul(r, -(arm.wLeft + prof.curbWidth + 0.45)));
        base[1] = roadPoint(R, arm.isStart ? R.cutA + 1 : R.length - R.cutB - 1, (arm.isStart ? -1 : 1) * (arm.wLeft + prof.curbWidth + 0.45))[1];
        out.add("trafficLight", variant, basis(base, d));
        sig.arms.push({ index: i, roadId: R.road.id, isStart: arm.isStart, pole: base, facing: d, mast: variant === "mast", lenses: lensPositions(base, d, r, variant) });
      } else if (urbanJ && nApp > 0 && S.bollards) {
        // give-way / stop sign on the minor arms
        const base = add(madd(N.node.p, d, cut + 1.2), mul(r, -(arm.wLeft + prof.curbWidth + 0.5)));
        base[1] = N.node.p[1] + (prof.curbHeight || 0);
        out.add("signPost", N.arms.length === 3 && i === 1 ? "yield" : "stop", basis(base, d));
      }
      // bollards flanking the crossing on both footways
      if (S.bollards && prof.edge === "curb" && N.arms.length >= 3) {
        for (const side of [1, -1]) {
          const w = side > 0 ? arm.wRight : arm.wLeft;
          for (const k of [0, 1]) {
            const p = add(madd(N.node.p, d, cut + 1.3 + k * 1.3), mul(r, side * (w + prof.curbWidth + 0.35)));
            p[1] = N.node.p[1] + (prof.curbHeight || 0);
            out.add("bollard", S.bollardStyle, basis(p, d));
          }
        }
      }
    });
    if (signalised && sig.arms.length) out.signals.push(sig);
  }
  out.solid.compact(); out.paint.compact();
  return out;
}

function rangeStations(a, b, step) { const o = []; for (let s = a; s <= b; s += step) o.push(s); return o; }

/** Lens centres for a signal head (world), for the emissive lit-lens instances: {red, amber, green} positions. */
function lensPositions(base, d, r, variant) {
  const heads = variant === "mast" ? [[-4.6, 6.2 - 0.75, 0], [0, 3.0, 0.2]] : [[0, 3.3, 0.2], [0, 2.1, 0.2]];
  const left = mul(r, -1) ; // local +X is left of forward
  const out = [];
  for (const [hx, hy, hz] of heads) {
    const c = add(add(add(base, mul(left, hx)), [0, hy, 0]), mul(d, hz + 0.13 + 0.012));
    out.push({ red: add(c, [0, 0.35, 0]), amber: c, green: add(c, [0, -0.35, 0]), facing: d });
  }
  return out;
}

function buildMedian(mb, R, s0, s1, prof) {
  if (s1 - s0 < 4) return;
  const hm = R.section.halfMedian, h = prof.curbHeight || 0.15, kw = Math.min(0.3, hm * 0.4);
  const curb = mb.material("curb"), top = mb.material(hm > 2 ? "grass" : "paving");
  // half section from the right kerb face inward: d measured from the median edge inward
  const face = [{ d: 0, h: 0, m: curb }, { d: 0.012, h: h * 0.85, m: curb }, { d: 0.03, h, m: curb }, { d: kw, h, m: top }];
  const cols = []; // full section x from +hm (right) to -hm (left)
  for (const v of face) cols.push({ x: hm - v.d, h: v.h, m: v.m });
  const inner = hm - kw;
  const nIn = Math.max(2, Math.round(inner / 1.2) * 2);
  for (let i = 1; i < nIn; i++) { const x = inner - (2 * inner * i) / nIn; cols.push({ x, h: h + (hm > 2 ? 0.04 * Math.sin(Math.PI * i / nIn) : 0), m: top }); }
  for (let i = face.length - 1; i >= 0; i--) cols.push({ x: -hm + face[i].d, h: face[i].h, m: i === face.length - 1 ? top : curb });
  const mats = cols.map((c) => c.m);
  const rowAt = (s, scale, centre) => cols.map((c) => mb.vertex(roadPoint(R, s, centre + c.x * scale, c.h), [s, c.x], [0, 1, 0.5, 0]));
  const rows = [];
  // nose A (semicircle): sweep the half section around the nose centre
  const noseR = hm, cA = s0 + noseR, cB = s1 - noseR;
  const nose = (centre, sign) => {
    const out = [];
    const K = 8;
    for (let k = 0; k <= K; k++) {
      const a = (k / K) * (Math.PI / 2); // from tip to full width
      const sc = Math.sin(a), ds = -Math.cos(a) * noseR * sign;
      out.push(cols.map((c) => mb.vertex(roadPoint(R, centre + ds, c.x * sc, c.h), [centre + ds, c.x * sc], [0, 1, 0.5, 0])));
    }
    return out;
  };
  const noseA = nose(cA, 1);
  rows.push(...noseA.slice(0, -1));
  const step = 3; const n = Math.max(1, Math.ceil((cB - cA) / step));
  for (let i = 0; i <= n; i++) rows.push(rowAt(lerp(cA, cB, i / n), 1, 0));
  const noseB = nose(cB, -1).reverse();
  rows.push(...noseB.slice(1));
  // tip rows are degenerate (all columns coincide) — gridAuto drops zero-area faces; neighbours form the fan
  mb.gridAuto((j) => (cols[j].x + cols[j + 1].x > 0 ? mats[j] : mats[j + 1]), rows, R.road.id);
}

function buildSpeedBump(out, R, s, prof) {
  const sec = R.section, len = 3.7, hgt = 0.075;
  const x0 = -sec.widthL + (prof.gutter || 0.3), x1 = sec.widthR - (prof.gutter || 0.3);
  const asphalt = out.solid.material("asphalt");
  const rows = [], N = 10, M = Math.max(4, Math.round((x1 - x0) / 1.5));
  for (let i = 0; i <= N; i++) {
    const t = i / N, ss = s - len / 2 + t * len, lift = hgt * Math.sin(Math.PI * t) ** 1.2;
    const row = []; for (let j = 0; j <= M; j++) { const x = lerp(x0, x1, j / M); row.push(out.solid.vertex(roadPoint(R, ss, x, lift + 0.001), [ss, x], [0.3, 1, 0.5, 0])); }
    rows.push(row);
  }
  out.solid.gridAuto(asphalt, rows, R.road.id);
  // painted triangles (white) on both approach faces
  const paint = out.paint.material("paint");
  for (let j = 0; j < M; j++) {
    if (j % 2) continue;
    const xa = lerp(x0, x1, j / M), xb = lerp(x0, x1, (j + 1) / M);
    for (const sign of [-1, 1]) {
      const tri = [[s + sign * (len / 2 - 0.1), xa], [s + sign * (len / 2 - 0.1), xb], [s + sign * 0.6, (xa + xb) / 2]];
      const ids = tri.map(([ss, x]) => { const t = (ss - (s - len / 2)) / len; const lift = hgt * Math.sin(Math.PI * clamp(t, 0, 1)) ** 1.2; return out.paint.vertex(roadPoint(R, ss, x, lift + 0.006), [ss, x], [0.3, 1, 0.5, 0]); });
      out.paint.tri(paint, ids[0], ids[1], ids[2], R.road.id);
    }
  }
}

function buildConstruction(out, R, cz, rng) {
  const sec = R.section;
  const tl = trafficLanes(R); if (!tl.length) return;
  // close the outermost right-hand lane between from..to with a 1:10 taper of cones, water barriers along the line
  const lane = tl.filter((l) => l.side > 0).sort((a, b) => b.xc - a.xc)[0] || tl[0];
  const from = cz.from, to = cz.to, taper = Math.min(25, (to - from) * 0.4);
  const xOpen = lane.x0 - 0.3, xClosed = lane.x1 - 0.2;
  const yaw = (p, q) => sub(q, p);
  for (let s = from; s <= from + taper; s += 2.5) { const t = (s - from) / taper; const x = lerp(xClosed, xOpen, t); const F = roadFrame(R, s, x); out.add("trafficCone", "", basis(F.p, F.d)); }
  for (let s = from + taper + 0.75; s + 1.5 <= to; s += 1.55) { const a = roadPoint(R, s, xOpen), b = roadPoint(R, s + 1.5, xOpen); out.add("waterBarrier", "", basis(a, yaw(a, b))); }
  for (let s = to; s >= to - 6; s -= 2) { const F = roadFrame(R, s, lerp(xOpen, xClosed, (to - s) / 6)); out.add("trafficCone", "", basis(F.p, F.d)); }
  // signs & barricade ahead of the taper, on the footway and in the closed lane
  const Fs = roadFrame(R, from - 20, sec.widthR + (R.road.profile.curbWidth || 0.3) + 0.6); out.add("roadworkSign", "", basis(Fs.p, mul(Fs.d, -1)));
  const Fb = roadFrame(R, from + taper + 2, lane.xc); out.add("barricade", "", basisAlong(Fb.p, Fb.r));
  const Fb2 = roadFrame(R, to - 1, lane.xc); out.add("barricade", "", basisAlong(Fb2.p, Fb2.r));
  // excavation: dark patch + gravel decals + crack web
  const mid = (from + taper + to) / 2, len = to - from - taper - 4;
  const Fm = roadFrame(R, mid, lane.xc);
  out.decal("patch", decalBox(Fm.p, Fm.d, lane.width * 0.9, Math.max(2, len)), { roughness: 0.9 });
  for (let i = 0; i < 3; i++) { const F = roadFrame(R, mid + (rng() - 0.5) * len * 0.8, lane.xc + (rng() - 0.5) * 1.5); out.decal("pothole", decalBox(F.p, F.d, 0.8, 1.0)); }
  out.zones.push({ roadId: R.road.id, from, to, lane: lane.index, side: lane.side });
}

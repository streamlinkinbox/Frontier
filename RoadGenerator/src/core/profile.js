// Road cross-section profiles. Real-world dimensions (metres):
//  lane 3.0–3.7, parking 2.2–2.5, cycle lane 1.5–2.0, kerb upstand 0.10–0.15, gutter 0.3–0.5,
//  footway 1.8–4.5, motorway shoulder 2.5–3.3, median barrier 0.6.
//
// A profile is converted to a *cross-section* — ordered vertex columns for the carriageway strip
// (one smooth quad strip shared by lanes, gutters, cycle and parking columns) plus two edge kits
// (kerb, footway, back strip) which are separate strips so kerb edges stay hard.

export const COLS_PER_LANE = 3; // vertex columns per lane: edge | wheel path | wheel path | edge → captures tyre wear

export const PROFILES = {
  local: { name: "Local street", lanesF: 1, lanesB: 1, laneWidth: 3.2, speed: 40, parkingL: 2.2, parkingR: 2.2, bike: 0, median: 0, medianType: "none", shoulder: 0, gutter: 0.35, curbHeight: 0.125, curbWidth: 0.3, walk: 2.4, walkType: "paving", back: 0.6, backType: "concrete", crown: 0.02, edge: "curb" },
  collector: { name: "Collector", lanesF: 1, lanesB: 1, laneWidth: 3.5, speed: 50, parkingL: 2.4, parkingR: 2.4, bike: 1.8, median: 0, medianType: "paint", shoulder: 0, gutter: 0.4, curbHeight: 0.14, curbWidth: 0.3, walk: 3.2, walkType: "paving", back: 0.8, backType: "planting", crown: 0.02, edge: "curb" },
  avenue: { name: "Avenue", lanesF: 2, lanesB: 2, laneWidth: 3.5, speed: 60, parkingL: 0, parkingR: 0, bike: 2.0, median: 2.4, medianType: "raised", shoulder: 0, gutter: 0.4, curbHeight: 0.15, curbWidth: 0.3, walk: 4.0, walkType: "paving", back: 1.2, backType: "planting", crown: 0.02, edge: "curb" },
  boulevard: { name: "Boulevard", lanesF: 3, lanesB: 3, laneWidth: 3.5, speed: 60, parkingL: 2.4, parkingR: 2.4, bike: 2.2, median: 5.0, medianType: "raised", shoulder: 0, gutter: 0.4, curbHeight: 0.15, curbWidth: 0.3, walk: 4.5, walkType: "paving", back: 1.5, backType: "planting", crown: 0.02, edge: "curb" },
  oneway: { name: "One-way street", lanesF: 2, lanesB: 0, laneWidth: 3.3, speed: 50, parkingL: 2.2, parkingR: 0, bike: 0, median: 0, medianType: "none", shoulder: 0, gutter: 0.35, curbHeight: 0.125, curbWidth: 0.3, walk: 2.6, walkType: "paving", back: 0.6, backType: "concrete", crown: 0.02, edge: "curb" },
  alley: { name: "Service alley", lanesF: 1, lanesB: 0, laneWidth: 3.6, speed: 20, parkingL: 0, parkingR: 0, bike: 0, median: 0, medianType: "none", shoulder: 0, gutter: 0.3, curbHeight: 0.06, curbWidth: 0.2, walk: 0.8, walkType: "concrete", back: 0.3, backType: "concrete", crown: -0.015, edge: "curb" },
  highway: { name: "Motorway (dual, 3+3)", lanesF: 3, lanesB: 3, laneWidth: 3.65, speed: 120, parkingL: 0, parkingR: 0, bike: 0, median: 3.6, medianType: "barrier", shoulder: 3.0, gutter: 0.0, curbHeight: 0.0, curbWidth: 0.5, walk: 3.0, walkType: "verge", back: 2.5, backType: "slope", crown: 0.025, edge: "flush" },
  highway2: { name: "Motorway (dual, 2+2)", lanesF: 2, lanesB: 2, laneWidth: 3.65, speed: 110, parkingL: 0, parkingR: 0, bike: 0, median: 3.0, medianType: "barrier", shoulder: 2.75, gutter: 0.0, curbHeight: 0.0, curbWidth: 0.5, walk: 2.5, walkType: "verge", back: 2.5, backType: "slope", crown: 0.025, edge: "flush" },
  carriageway: { name: "One-way carriageway (3)", lanesF: 3, lanesB: 0, laneWidth: 3.65, speed: 110, parkingL: 0, parkingR: 0, bike: 0, median: 0, medianType: "none", shoulder: 2.75, shoulderL: 1.0, gutter: 0.0, curbHeight: 0.0, curbWidth: 0.5, walk: 2.0, walkType: "verge", back: 2.5, backType: "slope", crown: 0.025, edge: "flush" },
  ramp: { name: "Interchange ramp", lanesF: 1, lanesB: 0, laneWidth: 4.5, speed: 60, parkingL: 0, parkingR: 0, bike: 0, median: 0, medianType: "none", shoulder: 1.8, shoulderL: 1.0, gutter: 0.0, curbHeight: 0.0, curbWidth: 0.5, walk: 1.5, walkType: "verge", back: 2.0, backType: "slope", crown: 0.025, edge: "flush" },
  rural: { name: "Rural two-lane", lanesF: 1, lanesB: 1, laneWidth: 3.5, speed: 90, parkingL: 0, parkingR: 0, bike: 0, median: 0, medianType: "none", shoulder: 1.5, gutter: 0.0, curbHeight: 0.0, curbWidth: 0.3, walk: 2.0, walkType: "verge", back: 2.5, backType: "slope", crown: 0.025, edge: "flush" },
  bridgeLink: { name: "Bridge approach", lanesF: 2, lanesB: 2, laneWidth: 3.5, speed: 70, parkingL: 0, parkingR: 0, bike: 0, median: 1.2, medianType: "barrier", shoulder: 0.5, gutter: 0.3, curbHeight: 0.15, curbWidth: 0.3, walk: 2.0, walkType: "concrete", back: 0.4, backType: "concrete", crown: 0.02, edge: "curb" },
};

export function cloneProfile(id, overrides = {}) {
  const base = PROFILES[id] || PROFILES.local;
  return { id, ...base, ...overrides };
}

export const MATERIALS = {
  asphalt: { kind: "asphalt" }, asphaltBike: { kind: "asphalt", tint: "red" }, asphaltBus: { kind: "asphalt", tint: "red" },
  concrete: { kind: "concrete" }, curb: { kind: "curb" }, paving: { kind: "paving" }, grass: { kind: "grass" }, soil: { kind: "soil" },
  gravel: { kind: "gravel" }, paint: { kind: "paint" }, paintYellow: { kind: "paintYellow" }, metal: { kind: "metal" }, metalPainted: { kind: "metalPainted" },
  steelGalv: { kind: "steelGalv" }, rubber: { kind: "rubber" }, tactile: { kind: "tactile" },
};

/** Total carriageway half widths (left/right of the centreline, metres) and lane layout. */
export function layout(profile) {
  const P = profile;
  const lw = P.laneWidth;
  const halfMedian = P.median / 2;
  // right side (forward direction, right-hand traffic): forward lanes, bike, parking, shoulder, gutter
  const right = [];
  const left = [];
  const pushLanes = (arr, count, dir) => { for (let i = 0; i < count; i++) arr.push({ kind: "lane", width: lw, dir }); };
  pushLanes(right, P.lanesF, 1);
  pushLanes(left, P.lanesB, -1);
  if (P.bike > 0) { right.push({ kind: "bike", width: P.bike, dir: 1 }); if (P.lanesB > 0) left.push({ kind: "bike", width: P.bike, dir: -1 }); }
  if (P.parkingR > 0) right.push({ kind: "parking", width: P.parkingR });
  if (P.parkingL > 0) left.push({ kind: "parking", width: P.parkingL });
  if (P.shoulder > 0) right.push({ kind: "shoulder", width: P.shoulder });
  const shL = P.shoulderL ?? P.shoulder;
  if (shL > 0) left.push({ kind: "shoulder", width: shL });
  if (P.gutter > 0) { right.push({ kind: "gutter", width: P.gutter }); left.push({ kind: "gutter", width: P.gutter }); }
  const sum = (arr) => arr.reduce((a, e) => a + e.width, 0);
  return { right, left, halfMedian, widthR: halfMedian + sum(right), widthL: halfMedian + sum(left) };
}

/**
 * Carriageway vertex columns from the LEFT kerb line (x = -widthL) to the RIGHT kerb line (x = +widthR).
 * Each column: { x, y, wear, mat, kind, laneIndex }. y is relative to the centreline datum (crown applied).
 */
export function crossSection(profile, opts = {}) {
  const { right, left, halfMedian, widthR, widthL } = layout(profile);
  const crown = opts.crown ?? profile.crown;
  const cols = []; // from left kerb to right kerb
  const heightAt = (x) => {
    const ax = Math.abs(x);
    let y = -ax * crown;
    // gutter dish: last `gutter` metres dip by 20mm towards the kerb
    const w = x < 0 ? widthL : widthR;
    if (profile.gutter > 0 && ax > w - profile.gutter) y -= 0.02 * ((ax - (w - profile.gutter)) / profile.gutter);
    return y;
  };
  const buildSide = (elems, sign) => {
    // returns columns from the median edge outward (x increasing in magnitude)
    const out = [];
    let x = halfMedian;
    if (halfMedian > 0 || elems.length) out.push({ x: sign * x, kind: "edge", wear: 0 });
    elems.forEach((e, idx) => {
      const start = x;
      if (e.kind === "lane") {
        // columns at 26%, 74% of the lane: wheel paths (real axle track ≈ 1.5–1.8 m)
        const fr = [0.26, 0.74, 1];
        for (const f of fr) out.push({ x: sign * (start + e.width * f), kind: e.kind, wear: f === 1 ? 0 : 1, mat: "asphalt", dir: e.dir, laneIndex: idx });
      } else if (e.kind === "parking") {
        out.push({ x: sign * (start + e.width * 0.5), kind: e.kind, wear: 0.15, mat: "asphalt" });
        out.push({ x: sign * (start + e.width), kind: e.kind, wear: 0, mat: "asphalt" });
      } else if (e.kind === "shoulder") {
        out.push({ x: sign * (start + e.width * 0.5), kind: e.kind, wear: 0, mat: "asphalt" });
        out.push({ x: sign * (start + e.width), kind: e.kind, wear: 0, mat: "asphalt" });
      } else {
        out.push({ x: sign * (start + e.width), kind: e.kind, wear: 0, mat: e.kind === "bike" ? "asphaltBike" : "asphalt", dir: e.dir });
      }
      x += e.width;
    });
    return out;
  };
  const R = buildSide(right, 1);
  const L = buildSide(left, -1);
  // median columns (flat; raised medians are separate geometry on top of the slab)
  const median = [];
  if (halfMedian > 0) {
    const n = profile.median > 3 ? 4 : 2;
    for (let i = 1; i < n; i++) median.push({ x: -halfMedian + (profile.median * i) / n, kind: "median", wear: 0 });
  }
  const all = [...L.reverse(), ...median, ...R];
  // Dedupe consecutive identical x (e.g. no median → both sides start at 0)
  const dedup = [];
  for (const c of all) { if (!dedup.length || Math.abs(dedup[dedup.length - 1].x - c.x) > 1e-6) dedup.push(c); else dedup[dedup.length - 1].wear = Math.max(dedup[dedup.length - 1].wear, c.wear); }
  for (const c of dedup) { c.y = heightAt(c.x); if (!c.mat) c.mat = "asphalt"; }
  // Material of a column segment [i,i+1] is taken from the column at its outer end; fix the centre.
  // Ensure an even number of segments (needed for all-quad junction centres)
  if ((dedup.length - 1) % 2 === 1) {
    // split the widest segment
    let best = 0, bw = 0;
    for (let i = 0; i + 1 < dedup.length; i++) { const w = dedup[i + 1].x - dedup[i].x; if (w > bw) { bw = w; best = i; } }
    const a = dedup[best], b = dedup[best + 1];
    dedup.splice(best + 1, 0, { x: (a.x + b.x) / 2, y: heightAt((a.x + b.x) / 2), wear: Math.max(a.wear, b.wear) * 0.5, mat: b.mat, kind: b.kind, dir: b.dir });
  }
  return { cols: dedup, widthL, widthR, halfMedian, layoutR: right, layoutL: left, segments: dedup.length - 1 };
}

/** Add `count` extra columns (×2 to keep the segment count even) by splitting the widest segments. */
export function refineSection(section, count) {
  for (let k = 0; k < count; k++) {
    const d = section.cols;
    let best = 0, bw = 0;
    for (let i = 0; i + 1 < d.length; i++) { const w = d[i + 1].x - d[i].x; if (w > bw) { bw = w; best = i; } }
    const a = d[best], b = d[best + 1];
    d.splice(best + 1, 0, { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, wear: (a.wear + b.wear) * 0.5, mat: b.mat, kind: b.kind, dir: b.dir });
  }
  section.segments = section.cols.length - 1;
  return section;
}

/**
 * Edge kit: strips outside the kerb line, each defined by vertices { d: distance from kerb line, h: height rel. gutter }.
 * Always four strips [kerb, walk, back, skirt] so corner sweeps can blend any two kits column-for-column.
 */
export function edgeKit(profile) {
  const P = profile;
  const h = P.edge === "curb" ? P.curbHeight : Math.min(P.curbHeight, 0.02);
  const kerbW = P.curbWidth;
  // kerb: face (slight batter), 12 mm chamfer, top
  // Fixed vertex counts per strip (kerb 4, walk 3, back 3) so any two kits blend column-for-column.
  const kerb = { kind: "kerb", material: "curb", verts: [{ d: 0, h: 0 }, { d: 0.012, h: h * 0.85 }, { d: 0.03, h: h }, { d: kerbW, h: h }] };
  if (P.edge !== "curb") kerb.verts = [{ d: 0, h: 0 }, { d: 0.02, h: h }, { d: kerbW * 0.5, h: h + 0.003 }, { d: kerbW, h: h + 0.005 }];
  const walkW = Math.max(0.3, P.walk);
  const walkCross = P.walkType === "verge" ? -0.04 : 0.02; // verge drains away, footway drains to kerb
  const walkMat = P.walkType === "paving" ? "paving" : P.walkType === "verge" ? "grass" : "concrete";
  const kerbTop = kerb.verts[3].h;
  const walk = { kind: "walk", material: walkMat, verts: [{ d: kerbW, h: kerbTop }, { d: kerbW + walkW * 0.5, h: kerbTop + walkW * 0.5 * walkCross }, { d: kerbW + walkW, h: kerbTop + walkW * walkCross }] };
  const backW = Math.max(0.2, P.back);
  const backMat = P.backType === "planting" ? "soil" : P.backType === "slope" ? "grass" : "concrete";
  const backTop = walk.verts[2].h;
  const back = { kind: "back", material: backMat, verts: [] };
  if (P.backType === "planting") back.verts = [{ d: kerbW + walkW, h: backTop }, { d: kerbW + walkW + 0.08, h: backTop - 0.08 }, { d: kerbW + walkW + backW, h: backTop - 0.08 }];
  else if (P.backType === "slope") back.verts = [{ d: kerbW + walkW, h: backTop }, { d: kerbW + walkW + backW * 0.5, h: backTop - backW * 0.2 }, { d: kerbW + walkW + backW, h: backTop - backW * 0.4 }];
  else back.verts = [{ d: kerbW + walkW, h: backTop }, { d: kerbW + walkW + backW * 0.5, h: backTop }, { d: kerbW + walkW + backW, h: backTop }];
  // skirt: a battered earth face dropping from the back edge into the terrain so no gap can open between kit and ground
  const bEnd = back.verts[back.verts.length - 1];
  const skirt = { kind: "skirt", material: backMat === "grass" ? "grass" : "soil", verts: [{ d: bEnd.d, h: bEnd.h }, { d: bEnd.d + 0.06, h: bEnd.h - 0.6 }] };
  return { strips: [kerb, walk, back, skirt], width: kerbW + walkW + backW, curbHeight: h, kerbTop };
}

/** Blend two kits column-for-column (kits always have the same strip/vertex structure). */
export function blendKits(a, b, t) {
  const strips = a.strips.map((sa, i) => {
    const sb = b.strips[i];
    const n = Math.max(sa.verts.length, sb.verts.length);
    const va = resampleVerts(sa.verts, n), vb = resampleVerts(sb.verts, n);
    return { kind: sa.kind, material: t < 0.5 ? sa.material : sb.material, verts: va.map((v, k) => ({ d: v.d + (vb[k].d - v.d) * t, h: v.h + (vb[k].h - v.h) * t })) };
  });
  return { strips, width: a.width + (b.width - a.width) * t, curbHeight: a.curbHeight + (b.curbHeight - a.curbHeight) * t };
}
function resampleVerts(verts, n) {
  if (verts.length === n) return verts;
  const out = [];
  for (let i = 0; i < n; i++) {
    const t = (i / (n - 1)) * (verts.length - 1), k = Math.min(verts.length - 2, Math.floor(t)), f = t - k;
    out.push({ d: verts[k].d + (verts[k + 1].d - verts[k].d) * f, h: verts[k].h + (verts[k + 1].h - verts[k].h) * f });
  }
  return out;
}
/** Normalise two kits so that every strip has equal vertex counts (needed before sweeping a corner). */
export function matchKits(a, b) {
  const sa = a.strips.map((s, i) => ({ ...s, verts: resampleVerts(s.verts, Math.max(s.verts.length, b.strips[i].verts.length)) }));
  const sb = b.strips.map((s, i) => ({ ...s, verts: resampleVerts(s.verts, Math.max(s.verts.length, a.strips[i].verts.length)) }));
  return [{ ...a, strips: sa }, { ...b, strips: sb }];
}

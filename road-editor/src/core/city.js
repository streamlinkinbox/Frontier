// Procedural city / countryside generator.
// Produces a regular JSON model: every road gets its own seed, lane widths are jittered, and
// bridges are chosen per river crossing, so the output is deliberately non-uniform.
import { mulberry32 } from './rng.js';
import { catmullRom, closestOnPolyline, cumulative, dist2 } from './geom.js';
import { createModel, makeRoad, makeProp, lanesFromPreset } from './model.js';
import { terrainY } from './terrain.js';
import { BRIDGE_TYPES, PRESETS } from './presets.js';

const WATER_CUT = -0.3;

export function generateCity({ seed = 1, half = 1200, grid = 280 } = {}) {
  const rnd = mulberry32(seed >>> 0);
  const pick = (arr) => arr[Math.floor(rnd() * arr.length)];
  const model = createModel(`Generated city #${seed}`);
  const pending = []; // {points, preset, kind?, name, props?}

  // ---- arterials: a regular grid with a gentle bend per line (so it isn't a perfect grid).
  const arterials = [];
  const lines = [];
  for (let k = -Math.floor(half / grid); k <= Math.floor(half / grid); k++) {
    const c = k * grid;
    const bend = 30 + rnd() * 30;
    const ph = rnd() * 6.28;
    // horizontal (along x)
    const h = [], v = [];
    for (let t = -half; t <= half + 1; t += 100) {
      h.push({ x: t, z: c + bend * Math.sin(t / 310 + ph) });
      v.push({ x: c + bend * Math.sin(t / 310 + ph), z: t });
    }
    arterials.push(h, v);
    lines.push({ pts: h, dir: 'x', k }, { pts: v, dir: 'z', k });
  }
  for (const pts of arterials) {
    const preset = pick(['arterial_bus', 'avenue_tram', 'boulevard_median', 'arterial_bus']);
    pending.push({ points: pts, preset, name: `Arterial ${pts.length}`, lamps: true, bridgeRiver: true });
  }

  // Projected shared list of arterial polylines for snapping locals onto them.
  const artPolys = arterials.map((pts) => ({ pts: catmullRom(pts, 4) }));
  for (const P of artPolys) P.s = cumulative(P.pts);

  // ---- local streets in the blocks, skipping some, ending on arterials.
  for (let k = -Math.floor(half / grid); k < Math.floor(half / grid); k++) {
    for (const off of [0.5]) {
      for (const dirX of [true, false]) {
        if (rnd() < 0.25) continue;
        const c = (k + off) * grid + (rnd() - 0.5) * 40;
        const seg = [];
        const from = -half + 40, to = half - 40;
        for (let t = from; t <= to; t += 60) seg.push(dirX ? { x: t, z: c + 14 * Math.sin(t / 150 + k) } : { x: c + 14 * Math.sin(t / 150 + k), z: t });
        const cut = snapEnds(seg, artPolys);
        pending.push({ points: cut, preset: pick(['residential', 'residential', 'cycle_path', 'footpath', 'country_road']), name: 'Local street', speed: true });
      }
    }
  }

  // ---- subway line under the town (underground, no merge with surface roads).
  const subway = [];
  for (let t = -half + 50; t <= half - 50; t += 60) subway.push({ x: t, z: -grid * 0.5 + 60 * Math.sin(t / 260) });
  pending.push({ points: subway, preset: 'avenue_tram', name: 'Metro line', layer: { mode: 'subway', depth: 9 } });

  // ---- railway: a diagonal line with level crossings and a river bridge.
  const rail = [];
  for (let t = -half; t <= half; t += 120) rail.push({ x: t, z: -half * 0.45 + t * 0.2 });
  pending.push({ points: rail, preset: 'railway', kind: 'rail', name: 'Main railway', rail: true });

  // ---- highway: long curve across the countryside crossing the river (cable-stayed bridge).
  const hw = [];
  for (let t = -half; t <= half; t += 100) hw.push({ x: t, z: 380 + 120 * Math.sin(t / 500) });
  pending.push({ points: hw, preset: 'highway', name: 'Highway A1', highway: true });

  // ---- countryside roads from outer edges into the town grid, and off-road trails.
  for (let i = 0; i < 6; i++) {
    const x0 = -half + rnd() * 2 * half;
    const pts = [{ x: x0, z: -half }];
    for (let t = 1; t <= 6; t++) pts.push({ x: x0 + (rnd() - 0.5) * 300 * t / 6, z: -half + (2 * half * t) / 6 });
    pending.push({ points: snapEnds(pts, artPolys), preset: 'country_road', name: 'Country road' });
  }
  for (let i = 0; i < 4; i++) {
    const z0 = -half + rnd() * 2 * half;
    const pts = [];
    for (let t = 0; t <= 8; t++) pts.push({ x: -half + (2 * half * t) / 8, z: z0 + 220 * Math.sin(t * 0.9 + i) });
    pending.push({ points: pts, preset: rnd() < 0.5 ? 'offroad' : 'gravel_track', name: 'Trail' });
  }

  // ---- materialise: split every road at water crossings and make bridges.
  for (const item of pending) {
    if (item.highway) item.bridgeRiver = true;
    const parts = item.bridgeRiver || item.rail || item.layer ? splitAtWater(item.points, item) : [{ points: item.points, bridge: false }];
    parts.forEach((part, idx) => {
      const preset = PRESETS[item.preset] ? item.preset : 'residential';
      let layer = item.layer ? { ...item.layer } : { mode: 'ground' };
      let name = item.name;
      if (part.bridge) {
        const bridge = pick(Object.keys(BRIDGE_TYPES));
        layer = { mode: 'bridge', bridge, clearance: 8 + Math.floor(rnd() * 5), rampLength: 30 };
        name = `${bridgeLabel(bridge)} bridge ${idx + 1}`;
      } else if (item.rail) {
        layer = { mode: 'ground' };
      }
      const lanes = jitterLanes(lanesFromPreset(preset), rnd);
      const road = makeRoad({ name, preset, points: part.points, layer, lanes, kind: item.kind, seed: Math.floor(rnd() * 2 ** 31) });
      if (item.kind === 'rail') road.kind = 'rail';
      model.roads.push(road);
    });
  }

  // ---- props: street lamps along arterials, speed bumps on local streets, bollards along cycle boulevards.
  for (const road of model.roads) {
    if (road.kind === 'rail' || road.layer.mode !== 'ground') continue;
    const pts = catmullRom(road.points, 4);
    const s = cumulative(pts);
    const L = s[s.length - 1];
    const W = road.lanes.reduce((a, l) => a + l.width, 0);
    if (road.preset.startsWith('arterial') || road.preset.startsWith('avenue') || road.preset.startsWith('boulevard')) {
      for (let d = 30, side = 1; d < L - 20; d += 40, side = -side) {
        const p = atDist(pts, s, d);
        model.props.push(makeProp('streetlamp', p.x + p.nx * side * (W / 2 + 0.6), p.z + p.nz * side * (W / 2 + 0.6), { angle: p.ang }));
      }
    }
    if (road.preset === 'residential' && L > 80) {
      const p = atDist(pts, s, L * 0.5);
      model.props.push(makeProp('speedbump', p.x, p.z, { angle: p.ang, width: W }));
    }
    if (road.preset === 'cycle_boulevard' && L > 60) {
      const p = atDist(pts, s, L * 0.3);
      model.props.push(makeProp('bollard', p.x + p.nx * (W / 2 - 1), p.z + p.nz * (W / 2 - 1), { angle: p.ang, length: Math.min(30, L * 0.4) }));
    }
  }

  return model;
}

function bridgeLabel(t) {
  return { girder: 'Girder', arch: 'Arch', suspension: 'Suspension', cable: 'Cable-stayed' }[t] || 'Bridge';
}

/** Random +-12% per lane width so every street has slightly different proportions. */
function jitterLanes(lanes, rnd) {
  return lanes.map((l) => ({ ...l, width: Math.round((l.width * (0.88 + rnd() * 0.24)) * 100) / 100 }));
}

/** Snap the ends of a polyline onto the nearest arterial (so local streets form T junctions). */
function snapEnds(pts, polys) {
  const out = pts.map((p) => ({ ...p }));
  for (const idx of [0, out.length - 1]) {
    let best = null;
    for (const P of polys) {
      const c = closestOnPolyline(P.pts, P.s, out[idx]);
      if (!best || c.dist < best.dist) best = c;
    }
    if (best && best.dist < 22) out[idx] = { x: best.x, z: best.z };
  }
  return out;
}

/**
 * Split a control polyline into surface runs and water crossings.
 * Water runs become bridge parts extended 24 m onto each bank.
 */
function splitAtWater(controls, item) {
  const pts = catmullRom(controls, 4);
  const s = cumulative(pts);
  const L = s[s.length - 1];
  const wet = pts.map((p) => terrainY(p.x, p.z) < WATER_CUT);
  const parts = [];
  let i = 0;
  if (!wet.some(Boolean)) return [{ points: controls, bridge: false }];
  while (i < pts.length) {
    const start = i;
    const isWet = wet[i];
    while (i < pts.length && wet[i] === isWet) i++;
    const end = i - 1;
    const sA = s[start], sB = s[end];
    if (isWet) {
      const a = Math.max(0, sA - 24), b = Math.min(L, sB + 24);
      parts.push({ points: sliceByS(pts, s, a, b), bridge: true });
    } else if (end - start >= 1) {
      parts.push({ points: sliceByS(pts, s, sA, sB), bridge: false });
    }
  }
  return parts.map((p) => ({ ...p, points: thin(p.points) })).filter((p) => p.points.length >= 2 && dist2(p.points[0], p.points[p.points.length - 1]) > 4);
}

function sliceByS(pts, s, a, b) {
  const out = [];
  const pa = atDist(pts, s, a), pb = atDist(pts, s, b);
  out.push({ x: pa.x, z: pa.z });
  for (let k = 0; k < pts.length; k++) if (s[k] > a + 0.5 && s[k] < b - 0.5) out.push({ x: pts[k].x, z: pts[k].z });
  out.push({ x: pb.x, z: pb.z });
  return out;
}

/** Thin dense samples back to control-like points (~every 24 m) so the model stays compact. */
function thin(pts) {
  if (pts.length <= 12) return pts;
  const out = [pts[0]];
  let acc = 0;
  for (let i = 1; i < pts.length - 1; i++) {
    acc += dist2(pts[i - 1], pts[i]);
    if (acc >= 24) { out.push(pts[i]); acc = 0; }
  }
  out.push(pts[pts.length - 1]);
  return out.map((p) => ({ x: Math.round(p.x * 100) / 100, z: Math.round(p.z * 100) / 100 }));
}

/** Point, unit tangent, unit left normal and heading at arc length d. */
function atDist(pts, s, d) {
  const L = s[s.length - 1];
  d = Math.max(0, Math.min(L, d));
  let i = 0;
  while (i < s.length - 2 && s[i + 1] < d) i++;
  const seg = s[i + 1] - s[i] || 1e-6;
  const t = (d - s[i]) / seg;
  const a = pts[i], b = pts[i + 1];
  const tx = (b.x - a.x) / seg, tz = (b.z - a.z) / seg;
  return { x: a.x + (b.x - a.x) * t, z: a.z + (b.z - a.z) * t, tx, tz, nx: -tz, nz: tx, ang: Math.atan2(tz, tx) };
}

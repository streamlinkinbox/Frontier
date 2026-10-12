// Procedural map generator. Produces raw roads (same shape the editor stores),
// so generated content is fully editable afterwards.
import { makeRng } from './geometry.js';
import { BRIDGE_TYPES } from './network.js';

const BRIDGE_KEYS = Object.keys(BRIDGE_TYPES);

export function generateMap(seed = 1) {
  const rnd = makeRng(seed);
  const pick = (arr) => arr[Math.floor(rnd() * arr.length)];
  const between = (a, b) => a + (b - a) * rnd();
  const roads = [];
  let n = 0;
  const add = (ctrl, profile, extra = {}) => {
    const id = `road-${(++n).toString().padStart(4, '0')}`;
    roads.push({ id, ctrl, profile, seed: Math.floor(rnd() * 2 ** 31), spans: [], ...extra });
    return roads[roads.length - 1];
  };
  // Waypoints with jitter so no two streets are straight rulers.
  const wobble = (a, b, step, amp) => {
    const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
    const k = Math.max(1, Math.round(len / step));
    const out = [a];
    for (let i = 1; i < k; i++) {
      const t = i / k;
      const nx = -(b[1] - a[1]) / len;
      const nz = (b[0] - a[0]) / len;
      const o = (rnd() * 2 - 1) * amp;
      out.push([a[0] + (b[0] - a[0]) * t + nx * o, a[1] + (b[1] - a[1]) * t + nz * o]);
    }
    out.push(b);
    return out;
  };

  // --- City grid -----------------------------------------------------------
  const HALF = 1100;
  const gridX = [];
  for (let x = -HALF; x <= HALF; ) {
    gridX.push(x);
    x += between(120, 190);
  }
  const gridZ = [];
  for (let z = -HALF; z <= HALF; ) {
    gridZ.push(z);
    z += between(120, 190);
  }
  const streetFor = (i, kind) => {
    if (i % 5 === 0) return 'avenue';
    if (i % 7 === 3) return 'boulevard';
    if (i % 4 === 2) return 'cycle_street';
    if (i % 9 === 4) return 'bus_rapid';
    return kind === 'x' ? 'residential' : pick(['residential', 'residential', 'avenue']);
  };
  gridX.forEach((x, i) => {
    // Streets are cut short at random so the grid has T-junction dead-ends.
    const z0 = -HALF + (rnd() < 0.2 ? between(0, 500) : 0);
    const z1 = HALF - (rnd() < 0.2 ? between(0, 500) : 0);
    add(wobble([x, z0], [x, z1], 150, 9), streetFor(i, 'x'));
  });
  gridZ.forEach((z, i) => {
    const x0 = -HALF + (rnd() < 0.2 ? between(0, 500) : 0);
    const x1 = HALF - (rnd() < 0.2 ? between(0, 500) : 0);
    add(wobble([x0, z], [x1, z], 150, 9), streetFor(i + 1, 'z'));
  });

  // --- Highways crossing the city with grade-separated spans ----------------
  const hwyZ = between(-300, 300);
  const hwy = add(wobble([-HALF * 2.2, hwyZ], [HALF * 2.2, hwyZ + 140], 300, 15), 'highway');
  const hwyLen = Math.hypot(HALF * 4.4, 140);
  for (let k = 0; k < 3; k++) {
    const sTarget = (k + 1) * (hwyLen / 4) + between(-60, 60);
    hwy.spans.push({ s: sTarget, len: 46, type: pick(BRIDGE_KEYS) });
  }
  const hwy2 = add(wobble([hwyZ - 300, -HALF * 2.2], [hwyZ + 300, HALF * 2.2], 300, 10), 'highway');
  const hw2Len = Math.hypot(600, HALF * 4.4);
  hwy2.spans.push({ s: hw2Len * 0.45, len: 60, type: 'cable' });

  // --- Rail + subway (surface, crosses grid -> rail crossings) -------------
  add(wobble([-HALF * 1.3, -HALF * 0.9], [HALF * 1.3, HALF * 0.7], 220, 40), 'railway');
  add(wobble([HALF * 0.2, -HALF], [-HALF * 0.25, HALF], 220, 30), 'subway');
  add(wobble([-HALF, 500], [HALF * 0.6, 420], 200, 25), 'subway');

  // --- Countryside: roads radiating outwards to the map edge ----------------
  const outer = [];
  for (let k = 0; k < 10; k++) {
    const a = (k / 10) * Math.PI * 2 + between(-0.2, 0.2);
    const r0 = HALF * 1.05;
    const r1 = HALF * 4.5;
    outer.push(
      add(wobble([Math.cos(a) * r0, Math.sin(a) * r0], [Math.cos(a) * r1, Math.sin(a) * r1], 260, 60), 'country', {
        ...(rnd() < 0.5 ? {} : {}),
      }),
    );
  }
  // Bridge a river valley on one country road
  if (outer[2]) outer[2].spans.push({ s: 0, len: 0, type: 'arch' });
  outer.forEach((r) => {
    if (r.spans.length && r.spans[0].len === 0) r.spans = [];
  });
  if (outer[4]) {
    const L = Math.hypot(...outer[4].ctrl[outer[4].ctrl.length - 1]) - HALF;
    outer[4].spans.push({ s: L * 0.5, len: 40, type: pick(BRIDGE_KEYS) });
  }

  // --- Offroad tracks wandering in the wilderness --------------------------
  for (let k = 0; k < 8; k++) {
    const ctrl = [];
    let x = between(-HALF * 4, HALF * 4);
    let z = between(-HALF * 4, HALF * 4);
    let h = rnd() * Math.PI * 2;
    for (let i = 0; i < 9; i++) {
      ctrl.push([x, z]);
      h += between(-0.9, 0.9);
      x += Math.cos(h) * between(200, 400);
      z += Math.sin(h) * between(200, 400);
    }
    add(ctrl, 'offroad');
  }
  // Footpaths through the park-like space near the city
  add(wobble([-HALF * 0.5, HALF * 0.9], [HALF * 0.5, HALF * 0.9], 120, 25), 'footpath');

  // --- Per-road props toggles ---------------------------------------------
  for (const r of roads) r.props = {};
  return roads;
}

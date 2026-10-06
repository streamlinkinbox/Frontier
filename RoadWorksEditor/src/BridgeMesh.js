//============================================================================================================================================
//                                                              BRIDGEMESH.JS
//============================================================================================================================================
// Procedural bridges: deck soffit, superstructure and substructure, all generated from the same corridor cross-sections
// the road uses, so a bridge is just a corridor whose family is "bridge" — it curves, cambers and carries pavements and
// curbs exactly like the roads it joins.
//
// Everything below the running surface is parametric and swappable:
//   deck        · cantilevered slab with a chamfered fascia and an inset soffit (no more flat extruded slab)
//   super       · beam / box girder / deck arch / Warren truss / suspension / cable-stayed
//   sub         · wall, twin column, hammerhead or V-piers on a spacing, plus abutments at both ends
//   furniture   · concrete parapet, steel rail or Jersey barrier
//
// Members are built from a generic swept-box / tube kit so new superstructure or pier families only need a recipe.

import { add, addScaled, clamp, cross, dist, len, lerp, norm, sub, vec } from './Vec.js?v=4';
import { cumulativeLengths } from './Polyline.js?v=4';
import { MeshSpec } from './MeshSpec.js?v=4';

export const BRIDGE_DEFAULTS = {
  type: 'beam',
  deckThickness: 0.85,
  soffitInset: 0.9, // cantilever: how far the soffit is pulled in from the deck edge
  girderCount: 4,
  girderDepth: 1.3,
  pierType: 'column',
  pierSpacing: 28,
  pierWidth: 1.6,
  towerHeight: 18,
  archRise: 6.5,
  trussHeight: 4.2,
  cableCount: 8,
  railing: 'parapet',
  railHeight: 1.1,
  groundZ: 0,
  abutments: true,
};

export function resolveBridge(settings = {}) {
  return { ...BRIDGE_DEFAULTS, ...(settings || {}) };
}

// ── station helpers ───────────────────────────────────────────────────────────────────────────────────────────────

// A station is a light frame: base point, left vector, miter scale. `at(lateral, height)` maps profile space to world.
function station(section) {
  const base = section.base;
  const left = section.frame.left;
  const miter = section.miter;
  return {
    base,
    left,
    tangent: section.frame.tangent,
    distance: section.distance,
    at(lateral, height) {
      return { ...addScaled(base, left, lateral * miter), z: base.z + height };
    },
  };
}

function stationsFrom(sections) {
  return sections.map(station);
}

function stationAtDistance(stations, d) {
  if (d <= stations[0].distance) return stations[0];
  const last = stations[stations.length - 1];
  if (d >= last.distance) return last;
  for (let i = 0; i < stations.length - 1; i++) {
    const a = stations[i];
    const b = stations[i + 1];
    if (d >= a.distance && d <= b.distance) {
      const t = (d - a.distance) / Math.max(b.distance - a.distance, 1e-6);
      const base = lerp(a.base, b.base, t);
      const left = norm(lerp(a.left, b.left, t), a.left);
      const tangent = norm(lerp(a.tangent, b.tangent, t), a.tangent);
      const miter = 1;
      return {
        base,
        left,
        tangent,
        distance: d,
        at(lateral, height) {
          return { ...addScaled(base, left, lateral * miter), z: base.z + height };
        },
      };
    }
  }
  return last;
}

// ── primitive kit ─────────────────────────────────────────────────────────────────────────────────────────────────

// Sweeps a rectangular section (width across `left`, height along Z) along the stations between two distances.
function sweptBox(spec, stations, d0, d1, lateral, top, bottom, width, { steps = 0 } = {}) {
  const list = stationsBetween(stations, d0, d1, steps);
  const rings = list.map((s) => {
    const hw = width * 0.5;
    return [s.at(lateral - hw, bottom), s.at(lateral + hw, bottom), s.at(lateral + hw, top), s.at(lateral - hw, top)];
  });
  spec.addLoftClosed(rings, { capStart: true, capEnd: true });
}

function stationsBetween(stations, d0, d1, extraSteps = 0) {
  const out = [stationAtDistance(stations, d0)];
  for (const s of stations) if (s.distance > d0 + 1e-4 && s.distance < d1 - 1e-4) out.push(s);
  out.push(stationAtDistance(stations, d1));
  if (extraSteps > 0 && out.length < extraSteps) {
    const dense = [];
    for (let i = 0; i <= extraSteps; i++) dense.push(stationAtDistance(stations, d0 + ((d1 - d0) * i) / extraSteps));
    return dense;
  }
  return out;
}

// Straight structural member between two world points with a rectangular section.
export function addMember(spec, a, b, width, height) {
  const dir = sub(b, a);
  const l = len(dir);
  if (l < 1e-4) return;
  const t = norm(dir);
  let upRef = vec(0, 0, 1);
  if (Math.abs(t.z) > 0.95) upRef = vec(0, 1, 0);
  const side = norm(cross(upRef, t), vec(0, 1, 0));
  const up = norm(cross(t, side), vec(0, 0, 1));
  const hw = width * 0.5;
  const hh = height * 0.5;
  const ring = (p) => [
    add(add(p, mulv(side, -hw)), mulv(up, -hh)),
    add(add(p, mulv(side, hw)), mulv(up, -hh)),
    add(add(p, mulv(side, hw)), mulv(up, hh)),
    add(add(p, mulv(side, -hw)), mulv(up, hh)),
  ];
  spec.addLoftClosed([ring(a), ring(b)], { capStart: true, capEnd: true });
}

const mulv = (v, s) => ({ x: v.x * s, y: v.y * s, z: v.z * s });

// Polygonal tube along a path — cables, round columns, handrails.
export function addTube(spec, path, radius, sides = 8) {
  if (path.length < 2) return;
  const rings = [];
  for (let i = 0; i < path.length; i++) {
    const t = norm(sub(path[Math.min(i + 1, path.length - 1)], path[Math.max(i - 1, 0)]), vec(0, 0, 1));
    let upRef = vec(0, 0, 1);
    if (Math.abs(t.z) > 0.95) upRef = vec(0, 1, 0);
    const side = norm(cross(upRef, t), vec(0, 1, 0));
    const up = norm(cross(t, side), vec(0, 0, 1));
    const ring = [];
    for (let k = 0; k < sides; k++) {
      const a = (k / sides) * Math.PI * 2;
      ring.push(add(path[i], add(mulv(side, Math.cos(a) * radius), mulv(up, Math.sin(a) * radius))));
    }
    rings.push(ring);
  }
  spec.addLoftClosed(rings, { capStart: true, capEnd: true });
}

// ── main entry ────────────────────────────────────────────────────────────────────────────────────────────────────

export function buildBridgeMesh(edge, sections, out, settings = {}) {
  if (!sections || sections.length < 2) return null;
  const cfg = resolveBridge({ ...(edge.bridge || {}), ...settings });
  const profile = edge.profile;
  const stations = stationsFrom(sections);
  const total = stations[stations.length - 1].distance;
  if (total < 1) return null;

  const deck = out.deck || (out.deck = new MeshSpec('deck'));
  const structure = out.structure || (out.structure = new MeshSpec('structure'));
  const piers = out.piers || (out.piers = new MeshSpec('piers'));
  const railing = out.railing || (out.railing = new MeshSpec('railing'));
  const cables = out.cables || (out.cables = new MeshSpec('cables'));

  const halfL = profile.leftTotalHalf;
  const halfR = profile.rightTotalHalf;
  const deckTopL = profile.curbHeight + profile.pavementLeft * 0.02;
  const deckTopR = profile.curbHeight + profile.pavementRight * 0.02;
  const thickness = cfg.deckThickness;
  const inset = clamp(cfg.soffitInset, 0, Math.min(halfL, halfR) * 0.6);
  const soffitZ = -thickness;
  const fasciaZ = -thickness * 0.35;

  // ── deck: fascia + chamfer + soffit ──
  for (let i = 0; i < stations.length - 1; i++) {
    const a = stations[i];
    const b = stations[i + 1];

    // left fascia (vertical) then chamfer in to the soffit
    deck.addFace([a.at(halfL, deckTopL), b.at(halfL, deckTopL), b.at(halfL, fasciaZ), a.at(halfL, fasciaZ)]);
    deck.addFace([a.at(halfL, fasciaZ), b.at(halfL, fasciaZ), b.at(halfL - inset, soffitZ), a.at(halfL - inset, soffitZ)]);
    // right side mirrored
    deck.addFace([a.at(-halfR, fasciaZ), b.at(-halfR, fasciaZ), b.at(-halfR, deckTopR), a.at(-halfR, deckTopR)]);
    deck.addFace([a.at(-halfR + inset, soffitZ), b.at(-halfR + inset, soffitZ), b.at(-halfR, fasciaZ), a.at(-halfR, fasciaZ)]);
    // soffit
    deck.addFace([a.at(-halfR + inset, soffitZ), b.at(-halfR + inset, soffitZ), b.at(halfL - inset, soffitZ), a.at(halfL - inset, soffitZ)]);
  }

  // end diaphragms so the deck reads as a solid at the abutments
  for (const [s, flip] of [[stations[0], true], [stations[stations.length - 1], false]]) {
    const ring = [
      s.at(halfL, deckTopL),
      s.at(halfL, fasciaZ),
      s.at(halfL - inset, soffitZ),
      s.at(-halfR + inset, soffitZ),
      s.at(-halfR, fasciaZ),
      s.at(-halfR, deckTopR),
    ];
    deck.addPolygon(flip ? ring.slice().reverse() : ring);
  }

  // ── superstructure ──
  const ctx = { cfg, profile, stations, total, halfL, halfR, soffitZ, thickness, inset, structure, cables, piers };
  switch (cfg.type) {
    case 'box':
      buildBoxGirder(ctx);
      break;
    case 'slab':
      buildSlab(ctx);
      break;
    case 'cantilever':
      buildCantilever(ctx);
      break;
    case 'tiedarch':
      buildTiedArch(ctx);
      break;
    case 'throughtruss':
      buildThroughTruss(ctx);
      break;
    case 'masonry':
      buildMasonryArches(ctx);
      break;
    case 'arch':
      buildArch(ctx);
      break;
    case 'truss':
      buildTruss(ctx);
      break;
    case 'suspension':
      buildSuspension(ctx);
      break;
    case 'cablestay':
      buildCableStayed(ctx);
      break;
    case 'beam':
    default:
      buildBeams(ctx);
      break;
  }

  // ── substructure ──
  const bearingZ = soffitZ - girderDepthFor(cfg);
  buildSupports({ ...ctx, bearingZ });

  // ── furniture ──
  buildRailings({ cfg, profile, stations, railing, halfL, halfR, deckTopL, deckTopR });

  return { stations, total };
}

function girderDepthFor(cfg) {
  if (cfg.type === 'beam' || cfg.type === 'box') return cfg.girderDepth;
  if (cfg.type === 'slab') return 0.25;
  if (cfg.type === 'cantilever') return cfg.girderDepth * 2.6; // haunched: deepest over the piers
  return 0.15;
}

// Spans used by the families that care where the piers land, so the structure and the substructure agree.
function spanLayout(cfg, total) {
  const spacing = Math.max(8, cfg.pierSpacing);
  const count = Math.max(0, Math.floor(total / spacing));
  const piers = [];
  for (let i = 1; i <= count; i++) piers.push((total * i) / (count + 1));
  return { piers, spans: [0, ...piers, total] };
}

// Elastomeric bearing pads under a girder line at a support — small, but their absence is what makes a bridge read
// as one extruded lump instead of a structure sitting on something.
function addBearings(spec, stations, d, laterals, z, size = 0.5) {
  const s = stationAtDistance(stations, d);
  for (const lat of laterals) {
    const h = 0.12;
    const f = (lateral, along, zz) => ({ ...addScaled(addScaled(s.base, s.left, lateral), s.tangent, along), z: zz });
    spec.addBox([
      f(lat - size * 0.5, -size * 0.4, z - h), f(lat + size * 0.5, -size * 0.4, z - h),
      f(lat + size * 0.5, size * 0.4, z - h), f(lat - size * 0.5, size * 0.4, z - h),
      f(lat - size * 0.5, -size * 0.4, z), f(lat + size * 0.5, -size * 0.4, z),
      f(lat + size * 0.5, size * 0.4, z), f(lat - size * 0.5, size * 0.4, z),
    ]);
  }
}

// ── superstructure families ───────────────────────────────────────────────────────────────────────────────────────

function buildBeams({ cfg, stations, total, halfL, halfR, inset, soffitZ, structure }) {
  const count = clamp(Math.round(cfg.girderCount), 1, 12);
  const usableL = halfL - inset - 0.25;
  const usableR = -halfR + inset + 0.25;
  const depth = cfg.girderDepth;
  const webThickness = 0.22;
  const flangeWidth = clamp(depth * 0.55, 0.4, 1.1);

  for (let i = 0; i < count; i++) {
    const t = count === 1 ? 0.5 : i / (count - 1);
    const lateral = usableR + (usableL - usableR) * t;
    // I-beam: web + bottom flange + top flange tucked under the slab
    sweptBox(structure, stations, 0, total, lateral, soffitZ - 0.06, soffitZ - depth + 0.14, webThickness);
    sweptBox(structure, stations, 0, total, lateral, soffitZ - depth + 0.14, soffitZ - depth, flangeWidth);
    sweptBox(structure, stations, 0, total, lateral, soffitZ, soffitZ - 0.12, flangeWidth * 0.75);
  }

  // cross bracing every ~8 m
  const spacing = 8;
  for (let d = spacing; d < total - 1; d += spacing) {
    const s = stationAtDistance(stations, d);
    for (let i = 0; i < count - 1; i++) {
      const t0 = count === 1 ? 0.5 : i / (count - 1);
      const t1 = (i + 1) / (count - 1);
      const l0 = usableR + (usableL - usableR) * t0;
      const l1 = usableR + (usableL - usableR) * t1;
      addMember(structure, s.at(l0, soffitZ - 0.2), s.at(l1, soffitZ - depth + 0.2), 0.12, 0.12);
      addMember(structure, s.at(l0, soffitZ - depth + 0.2), s.at(l1, soffitZ - 0.2), 0.12, 0.12);
    }
  }
}

function buildBoxGirder({ cfg, stations, total, halfL, halfR, inset, soffitZ, structure }) {
  const depth = cfg.girderDepth * 1.35;
  const topL = (halfL - inset) * 0.82;
  const topR = (-halfR + inset) * 0.82;
  const botL = topL * 0.58;
  const botR = topR * 0.58;

  const rings = stations.map((s) => [
    s.at(topR, soffitZ),
    s.at(botR, soffitZ - depth),
    s.at(botL, soffitZ - depth),
    s.at(topL, soffitZ),
  ]);
  structure.addLoftClosed(rings, { capStart: true, capEnd: true });

  // haunches where the box meets the slab
  for (let i = 0; i < stations.length - 1; i++) {
    const a = stations[i];
    const b = stations[i + 1];
    structure.addFace([a.at(topL, soffitZ), b.at(topL, soffitZ), b.at(topL + 0.6, soffitZ), a.at(topL + 0.6, soffitZ)]);
    structure.addFace([a.at(topR - 0.6, soffitZ), b.at(topR - 0.6, soffitZ), b.at(topR, soffitZ), a.at(topR, soffitZ)]);
  }
  void total;
}

// Deck arch: the ribs spring from ground level at both abutments and crown just beneath the deck soffit at midspan,
// with spandrel columns of varying height carrying the deck. `archRise` biases how far the crown sits below the deck,
// so the same recipe reads as a shallow concrete arch or a tall steel one.
function buildArch({ cfg, stations, total, halfL, halfR, soffitZ, structure }) {
  const groundZ = cfg.groundZ ?? 0;
  const ribOffset = Math.min(halfL, halfR) * 0.58;
  const steps = clamp(Math.round(total / 1.5), 16, 110);
  const thickness = clamp(total * 0.014, 0.35, 1.2);

  const midSoffit = stationAtDistance(stations, total * 0.5).base.z + soffitZ;
  const clearance = clamp(cfg.archRise * 0.12, 0.2, 2.0);
  const crownZ = Math.max(groundZ + 1.0, midSoffit - clearance);
  const springZ = Math.min(groundZ, crownZ - 1.0);
  const archZ = (t) => springZ + (crownZ - springZ) * (1 - Math.pow(2 * t - 1, 2));

  for (const side of [ribOffset, -ribOffset]) {
    const path = [];
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const s = stationAtDistance(stations, t * total);
      path.push(s.at(side, archZ(t) - s.base.z));
    }
    structure.addLoftClosed(ribRings(path, thickness), { capStart: true, capEnd: true });

    // spandrel columns from the extrados up to the soffit
    const columns = clamp(Math.round(total / 7), 3, 18);
    for (let i = 1; i < columns; i++) {
      const t = i / columns;
      const s = stationAtDistance(stations, t * total);
      const top = s.at(side, soffitZ);
      const bottom = s.at(side, archZ(t) + thickness * 0.5 - s.base.z);
      if (top.z - bottom.z > 0.5) addMember(structure, bottom, top, 0.42, 0.42);
    }
  }

  // transverse bracing between the two ribs
  const ties = clamp(Math.round(total / 9), 2, 12);
  for (let i = 1; i < ties; i++) {
    const t = i / ties;
    const s = stationAtDistance(stations, t * total);
    const z = archZ(t) - s.base.z;
    addMember(structure, s.at(ribOffset, z), s.at(-ribOffset, z), 0.3, 0.3);
  }
}

// Builds square rings around a path so a curved rib can be lofted as a solid.
function ribRings(path, thickness) {
  return path.map((p, i) => {
    const t = norm(sub(path[Math.min(i + 1, path.length - 1)], path[Math.max(i - 1, 0)]), vec(1, 0, 0));
    let upRef = vec(0, 0, 1);
    if (Math.abs(t.z) > 0.95) upRef = vec(0, 1, 0);
    const sideV = norm(cross(upRef, t), vec(0, 1, 0));
    const up = norm(cross(t, sideV), vec(0, 0, 1));
    const hw = thickness * 0.7;
    const hh = thickness * 0.5;
    return [
      add(add(p, mulv(sideV, -hw)), mulv(up, -hh)),
      add(add(p, mulv(sideV, hw)), mulv(up, -hh)),
      add(add(p, mulv(sideV, hw)), mulv(up, hh)),
      add(add(p, mulv(sideV, -hw)), mulv(up, hh)),
    ];
  });
}

function buildTruss({ cfg, stations, total, halfL, halfR, soffitZ, structure }) {
  const height = Math.max(2.0, cfg.trussHeight);
  const panels = clamp(Math.round(total / 6), 3, 24);
  const chord = 0.32;
  const sides = [halfL - 0.15, -(halfR - 0.15)];

  for (const side of sides) {
    const bottomPts = [];
    const topPts = [];
    for (let i = 0; i <= panels; i++) {
      const s = stationAtDistance(stations, (i / panels) * total);
      bottomPts.push(s.at(side, soffitZ + 0.1));
      topPts.push(s.at(side, height));
    }
    for (let i = 0; i < panels; i++) {
      addMember(structure, bottomPts[i], bottomPts[i + 1], chord, chord);
      addMember(structure, topPts[i], topPts[i + 1], chord, chord);
      // Warren diagonals with verticals at the panel points
      if (i % 2 === 0) addMember(structure, bottomPts[i], topPts[i + 1], chord * 0.7, chord * 0.7);
      else addMember(structure, topPts[i], bottomPts[i + 1], chord * 0.7, chord * 0.7);
      addMember(structure, bottomPts[i], topPts[i], chord * 0.6, chord * 0.6);
    }
    addMember(structure, bottomPts[panels], topPts[panels], chord * 0.6, chord * 0.6);
  }

  // portal / sway bracing overhead
  for (let i = 0; i <= panels; i += 2) {
    const s = stationAtDistance(stations, (i / panels) * total);
    addMember(structure, s.at(sides[0], height), s.at(sides[1], height), chord * 0.8, chord * 0.8);
  }
}

function buildSuspension({ cfg, stations, total, halfL, halfR, soffitZ, structure, cables }) {
  const towerH = Math.max(6, cfg.towerHeight);
  const sides = [halfL - 0.4, -(halfR - 0.4)];
  const towerD = [total * 0.22, total * 0.78];
  const sag = towerH * 0.62;

  for (const d of towerD) {
    const s = stationAtDistance(stations, d);
    for (const side of sides) {
      addMember(structure, s.at(side, soffitZ - 1.2), s.at(side, towerH), 0.9, 0.9);
    }
    addMember(structure, s.at(sides[0], towerH - 1.2), s.at(sides[1], towerH - 1.2), 0.6, 0.6);
    addMember(structure, s.at(sides[0], towerH * 0.45), s.at(sides[1], towerH * 0.45), 0.5, 0.5);
  }

  for (const side of sides) {
    const path = [];
    const steps = 48;
    for (let i = 0; i <= steps; i++) {
      const d = (i / steps) * total;
      const s = stationAtDistance(stations, d);
      path.push(s.at(side, mainCableHeight(d, total, towerD, towerH, sag)));
    }
    addTube(cables, path, 0.16, 8);

    const hangers = clamp(cfg.cableCount * 2, 6, 48);
    for (let i = 1; i < hangers; i++) {
      const d = (i / hangers) * total;
      if (d < towerD[0] * 0.35 || (d > towerD[0] + 0.3 && d < towerD[1] - 0.3 ? false : false)) continue;
      const s = stationAtDistance(stations, d);
      const topZ = mainCableHeight(d, total, towerD, towerH, sag);
      const top = s.at(side, topZ);
      const bottom = s.at(side, 0.4);
      if (top.z - bottom.z > 0.6) addTube(cables, [bottom, top], 0.055, 6);
    }
  }
}

function mainCableHeight(d, total, towerD, towerH, sag) {
  const [d0, d1] = towerD;
  if (d <= d0) {
    const t = d / Math.max(d0, 1e-3);
    return lerpNum(1.2, towerH, t * t * 0.9 + t * 0.1);
  }
  if (d >= d1) {
    const t = (total - d) / Math.max(total - d1, 1e-3);
    return lerpNum(1.2, towerH, t * t * 0.9 + t * 0.1);
  }
  const t = (d - d0) / Math.max(d1 - d0, 1e-3);
  return towerH - 4 * sag * t * (1 - t);
}

const lerpNum = (a, b, t) => a + (b - a) * t;

function buildCableStayed({ cfg, stations, total, halfL, halfR, soffitZ, structure, cables }) {
  const towerH = Math.max(8, cfg.towerHeight);
  const towerD = total > 60 ? [total * 0.3, total * 0.7] : [total * 0.5];
  const sides = [halfL - 0.4, -(halfR - 0.4)];
  const fan = clamp(Math.round(cfg.cableCount), 3, 16);

  for (const d of towerD) {
    const s = stationAtDistance(stations, d);
    // A-frame pylon
    for (const side of sides) {
      addMember(structure, s.at(side, soffitZ - 1.5), s.at(side * 0.25, towerH), 1.1, 1.1);
    }
    addMember(structure, s.at(sides[0] * 0.25, towerH), s.at(sides[1] * 0.25, towerH), 0.8, 0.8);
    addMember(structure, s.at(sides[0] * 0.6, towerH * 0.5), s.at(sides[1] * 0.6, towerH * 0.5), 0.6, 0.6);

    const reach = Math.min(total * 0.45, Math.max(towerH * 1.8, 20));
    for (const side of sides) {
      for (let i = 1; i <= fan; i++) {
        const t = i / fan;
        const anchorHeight = towerH - t * towerH * 0.45;
        for (const dir of [-1, 1]) {
          const ad = clamp(d + dir * reach * t, 1.0, total - 1.0);
          const anchorStation = stationAtDistance(stations, ad);
          const top = s.at(side * 0.25, anchorHeight);
          const bottom = anchorStation.at(side, 0.35);
          if (dist(top, bottom) > 2) addTube(cables, [bottom, top], 0.07, 6);
        }
      }
    }
  }
}

// Solid slab: no girders at all, just a thickened deck with a chamfered soffit edge and a modest haunch over each
// pier. The right answer for short spans, and the cheapest thing to look at.
function buildSlab({ cfg, stations, total, halfL, halfR, inset, soffitZ, structure, piers }) {
  const extra = clamp(cfg.girderDepth * 0.35, 0.18, 0.6);
  const left = halfL - inset;
  const right = -halfR + inset;
  const { piers: pierDs } = spanLayout(cfg, total);

  // depth swells smoothly to `extra` over each support
  const depthAt = (d) => {
    let best = 0;
    for (const pd of [0, ...pierDs, total]) {
      const reach = Math.max(4, Math.min(12, total * 0.12));
      const t = clamp(1 - Math.abs(d - pd) / reach, 0, 1);
      best = Math.max(best, t * t * (3 - 2 * t));
    }
    return extra * best;
  };

  for (let i = 0; i < stations.length - 1; i++) {
    const a = stations[i];
    const b = stations[i + 1];
    const da = depthAt(a.distance);
    const db = depthAt(b.distance);
    if (da < 1e-3 && db < 1e-3) continue;
    structure.addFace([a.at(left, soffitZ), b.at(left, soffitZ), b.at(left, soffitZ - db), a.at(left, soffitZ - da)]);
    structure.addFace([a.at(right, soffitZ - da), b.at(right, soffitZ - db), b.at(right, soffitZ), a.at(right, soffitZ)]);
    structure.addFace([a.at(right, soffitZ - da), b.at(right, soffitZ - db), b.at(left, soffitZ - db), a.at(left, soffitZ - da)]);
  }
  for (const d of [0, ...pierDs, total]) {
    addBearings(piers, stations, d, [left * 0.55, right * 0.55], stationAtDistance(stations, d).base.z + soffitZ - depthAt(d), 0.6);
  }
}

// Balanced cantilever box girder: the soffit is a parabola between piers, deepest over each support and shallowest
// at midspan, with a visible casting-segment rhythm on the web.
function buildCantilever({ cfg, stations, total, halfL, halfR, inset, soffitZ, structure, piers }) {
  const midDepth = Math.max(0.8, cfg.girderDepth);
  const pierDepth = midDepth * 2.6;
  const { piers: pierDs, spans } = spanLayout(cfg, total);
  const topL = (halfL - inset) * 0.86;
  const topR = (-halfR + inset) * 0.86;
  const botL = topL * 0.56;
  const botR = topR * 0.56;

  // Depth profile: parabolic within each span, pinned deep at every support.
  const depthAt = (d) => {
    for (let i = 0; i < spans.length - 1; i++) {
      const a = spans[i];
      const b = spans[i + 1];
      if (d < a - 1e-6 || d > b + 1e-6) continue;
      const t = (d - a) / Math.max(b - a, 1e-6);
      const deepA = i === 0 ? midDepth : pierDepth;
      const deepB = i === spans.length - 2 ? midDepth : pierDepth;
      // two half-parabolas meeting at midspan
      const u = Math.abs(2 * t - 1);
      const deep = t < 0.5 ? deepA : deepB;
      return midDepth + (deep - midDepth) * u * u;
    }
    return midDepth;
  };

  const rings = stations.map((s) => {
    const dep = depthAt(s.distance);
    return [s.at(topR, soffitZ), s.at(botR, soffitZ - dep), s.at(botL, soffitZ - dep), s.at(topL, soffitZ)];
  });
  structure.addLoftClosed(rings, { capStart: true, capEnd: true });

  // segment joints: a shallow rib every ~4 m reads as the casting segments
  for (let d = 4; d < total - 1; d += 4) {
    const s = stationAtDistance(stations, d);
    const dep = depthAt(d);
    for (const lat of [topL, topR]) {
      addMember(structure, s.at(lat * 1.02, soffitZ - 0.15), s.at(lat * 0.6, soffitZ - dep + 0.15), 0.1, 0.1);
    }
  }
  for (const d of pierDs) {
    addBearings(piers, stations, d, [botL * 0.7, botR * 0.7], stationAtDistance(stations, d).base.z + soffitZ - depthAt(d), 0.8);
  }
}

// Tied (bowstring) arch: the arch rises *above* the deck, hangers drop to the deck edge, and a tie girder along the
// deck takes the thrust — so the whole thing can sit on two simple bearings instead of thrust blocks.
function buildTiedArch({ cfg, stations, total, halfL, halfR, soffitZ, structure, cables }) {
  const rise = Math.max(4, cfg.archRise * 1.6);
  const sides = [halfL - 0.55, -(halfR - 0.55)];
  const steps = clamp(Math.round(total / 1.5), 20, 120);
  const ribW = clamp(total * 0.012, 0.35, 0.9);
  const archZ = (t) => rise * (1 - Math.pow(2 * t - 1, 2));

  const ribPaths = [];
  for (const side of sides) {
    const path = [];
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      path.push(stationAtDistance(stations, t * total).at(side, archZ(t) + 0.2));
    }
    ribPaths.push(path);
    structure.addLoftClosed(ribRings(path, ribW), { capStart: true, capEnd: true });

    // tie girder running the length of the deck edge
    sweptBox(structure, stations, 0, total, side, soffitZ + 0.05, soffitZ - Math.max(0.6, cfg.girderDepth * 0.7), 0.45);
  }

  // hangers
  const hangers = clamp(Math.round(cfg.cableCount * 1.5), 4, 30);
  for (let i = 1; i < hangers; i++) {
    const t = i / hangers;
    const s = stationAtDistance(stations, t * total);
    const z = archZ(t) + 0.2;
    if (z < 1.8) continue;
    for (const side of sides) addTube(cables, [s.at(side, 0.3), s.at(side, z - ribW * 0.5)], 0.045, 6);
  }

  // cross bracing over the crown only, where there is headroom
  const braces = clamp(Math.round(total / 12), 1, 8);
  for (let i = 1; i < braces; i++) {
    const t = 0.5 + (i - braces / 2) / (braces * 1.6);
    if (t <= 0.2 || t >= 0.8) continue;
    const s = stationAtDistance(stations, t * total);
    const z = archZ(t) + 0.2;
    addMember(structure, s.at(sides[0], z), s.at(sides[1], z), 0.3, 0.26);
  }
}

// Pratt through truss: the deck runs *between* the trusses, diagonals slope down toward midspan, floor beams carry
// the deck under the bottom chord, and the end panels get inclined portal frames with knee bracing.
function buildThroughTruss({ cfg, stations, total, halfL, halfR, soffitZ, structure }) {
  const height = Math.max(4.5, cfg.trussHeight * 1.45);
  const panels = clamp(Math.round(total / 7), 4, 22);
  const chord = 0.36;
  const sides = [halfL - 0.2, -(halfR - 0.2)];
  const bottomZ = soffitZ + 0.05;

  const nodes = sides.map((side) => {
    const bottom = [];
    const top = [];
    for (let i = 0; i <= panels; i++) {
      const s = stationAtDistance(stations, (i / panels) * total);
      bottom.push(s.at(side, bottomZ));
      // end posts rake in, so the top chord stops one panel short at each end
      const t = i / panels;
      const h = t < 1 / panels || t > 1 - 1 / panels ? height * 0.55 : height;
      top.push(s.at(side, h));
    }
    return { bottom, top };
  });

  for (const { bottom, top } of nodes) {
    for (let i = 0; i < panels; i++) {
      addMember(structure, bottom[i], bottom[i + 1], chord, chord * 0.9);
      addMember(structure, top[i], top[i + 1], chord, chord * 0.9);
      // Pratt: verticals in compression, diagonals leaning toward midspan
      addMember(structure, bottom[i], top[i], chord * 0.6, chord * 0.6);
      const toward = i < panels / 2 ? [bottom[i], top[i + 1]] : [top[i], bottom[i + 1]];
      addMember(structure, toward[0], toward[1], chord * 0.55, chord * 0.55);
    }
    addMember(structure, bottom[panels], top[panels], chord * 0.6, chord * 0.6);
  }

  // floor beams under the deck at every panel point, plus lateral bracing in the bottom plane
  for (let i = 0; i <= panels; i++) {
    const s = stationAtDistance(stations, (i / panels) * total);
    addMember(structure, s.at(sides[0], bottomZ - 0.1), s.at(sides[1], bottomZ - 0.1), 0.26, 0.5);
    if (i < panels) {
      const n = stationAtDistance(stations, ((i + 1) / panels) * total);
      addMember(structure, s.at(sides[0], bottomZ - 0.15), n.at(sides[1], bottomZ - 0.15), 0.12, 0.12);
    }
  }

  // overhead sway frames between the full-height panels, and a portal at each end
  for (let i = 1; i < panels; i++) {
    const s = stationAtDistance(stations, (i / panels) * total);
    addMember(structure, s.at(sides[0], height), s.at(sides[1], height), 0.24, 0.24);
    if (i === 1 || i === panels - 1) {
      // portal knee braces
      for (const side of sides) {
        addMember(structure, s.at(side, height - 0.3), s.at(side * 0.45, height - 1.5), 0.2, 0.2);
      }
      addMember(structure, s.at(sides[0] * 0.45, height - 1.5), s.at(sides[1] * 0.45, height - 1.5), 0.22, 0.22);
    }
  }
}

// Masonry viaduct: a row of semicircular barrels between solid piers, with spandrel walls, a voussoir ring standing
// slightly proud of the spandrel, and a string course under the parapet.
function buildMasonryArches({ cfg, stations, total, halfL, halfR, soffitZ, structure, piers }) {
  const groundZ = cfg.groundZ ?? 0;
  const span = clamp(cfg.pierSpacing, 8, 60);
  const bays = Math.max(1, Math.round(total / span));
  const pierW = clamp(cfg.pierWidth * 1.6, 1.2, 5);
  const width = halfL + halfR;
  const ringProud = 0.22;
  const steps = 18;

  for (let b = 0; b < bays; b++) {
    const d0 = (total * b) / bays;
    const d1 = (total * (b + 1)) / bays;
    const mid = (d0 + d1) * 0.5;
    const clear = (d1 - d0) - pierW;
    if (clear < 2) continue;
    const radius = clear * 0.5;
    // The crown sits just under the deck soffit and the springing lands above ground. Where there is not enough
    // headroom for a semicircle the barrel flattens into a segmental (elliptical) arch instead of bursting through
    // the deck.
    const crownAbs = stationAtDistance(stations, mid).base.z + soffitZ - 0.35;
    const rise = Math.min(radius, crownAbs - (groundZ + 0.8));
    if (rise < 1.2) continue;
    const springAbs = crownAbs - rise;

    // barrel: the soffit swept across the full width
    const ring = [];
    for (let i = 0; i <= steps; i++) {
      const a = Math.PI * (i / steps);
      ring.push({ d: mid - Math.cos(a) * radius, z: springAbs + Math.sin(a) * rise });
    }
    for (let i = 0; i < ring.length - 1; i++) {
      const sa = stationAtDistance(stations, clamp(ring[i].d, 0, total));
      const sb = stationAtDistance(stations, clamp(ring[i + 1].d, 0, total));
      const za = ring[i].z - sa.base.z;
      const zb = ring[i + 1].z - sb.base.z;
      structure.addFace([sa.at(halfL, za), sb.at(halfL, zb), sb.at(-halfR, zb), sa.at(-halfR, za)]);
      // voussoir ring, proud of the spandrel face on both elevations
      for (const side of [halfL + ringProud, -(halfR + ringProud)]) {
        const inner = side > 0 ? halfL : -halfR;
        structure.addFace([sa.at(inner, za), sb.at(inner, zb), sb.at(side, zb), sa.at(side, za)]);
      }
    }

    // spandrel walls: the solid between the extrados and the deck soffit
    for (let i = 0; i < ring.length - 1; i++) {
      const sa = stationAtDistance(stations, clamp(ring[i].d, 0, total));
      const sb = stationAtDistance(stations, clamp(ring[i + 1].d, 0, total));
      for (const side of [halfL, -halfR]) {
        structure.addFace([
          sa.at(side, ring[i].z - sa.base.z),
          sb.at(side, ring[i + 1].z - sb.base.z),
          sb.at(side, soffitZ),
          sa.at(side, soffitZ),
        ]);
      }
    }
  }

  // piers between the bays, down to grade
  for (let b = 1; b < bays; b++) {
    const d = (total * b) / bays;
    const s = stationAtDistance(stations, d);
    const topAbs = s.base.z + soffitZ;
    const f = (lat, along, z) => ({ ...addScaled(addScaled(s.base, s.left, lat), s.tangent, along), z });
    const hw = width * 0.5 + 0.1;
    const lat0 = (halfL - halfR) * 0.5;
    piers.addBox([
      f(lat0 - hw, -pierW * 0.5, groundZ), f(lat0 + hw, -pierW * 0.5, groundZ),
      f(lat0 + hw, pierW * 0.5, groundZ), f(lat0 - hw, pierW * 0.5, groundZ),
      f(lat0 - hw, -pierW * 0.5, topAbs), f(lat0 + hw, -pierW * 0.5, topAbs),
      f(lat0 + hw, pierW * 0.5, topAbs), f(lat0 - hw, pierW * 0.5, topAbs),
    ]);
  }

  // string course under the parapet
  for (const side of [halfL, -halfR]) {
    const out = side > 0 ? 0.3 : -0.3;
    for (let i = 0; i < stations.length - 1; i++) {
      const a = stations[i];
      const b = stations[i + 1];
      structure.addFace([a.at(side + out, soffitZ + 0.05), b.at(side + out, soffitZ + 0.05), b.at(side + out, soffitZ - 0.3), a.at(side + out, soffitZ - 0.3)]);
      structure.addFace([a.at(side, soffitZ + 0.05), b.at(side, soffitZ + 0.05), b.at(side + out, soffitZ + 0.05), a.at(side + out, soffitZ + 0.05)]);
      structure.addFace([a.at(side, soffitZ - 0.3), b.at(side, soffitZ - 0.3), b.at(side + out, soffitZ - 0.3), a.at(side + out, soffitZ - 0.3)]);
    }
  }
}

// ── substructure ──────────────────────────────────────────────────────────────────────────────────────────────────

function buildSupports({ cfg, stations, total, halfL, halfR, soffitZ, piers, bearingZ }) {
  const groundZ = cfg.groundZ ?? 0;

  if (cfg.abutments) {
    for (const d of [0, total]) {
      const s = stationAtDistance(stations, d);
      const inward = d === 0 ? 1 : -1;
      const w = (halfL + halfR) * 0.98;
      const centreLateral = (halfL - halfR) * 0.5;
      const front = s.at(centreLateral, soffitZ - 0.05);
      const topAbs = front.z; // absolute bearing level, not the profile-relative height
      const depth = Math.max(1.2, cfg.pierWidth);
      const back = addScaled(front, s.tangent, inward * -depth);
      // abutment block: bearing shelf at the deck end carried down to the ground
      const hw = w * 0.5;
      const leftV = s.left;
      const corner = (p, lat, z) => ({ ...addScaled(p, leftV, lat), z });
      const groundLevel = Math.min(groundZ, topAbs - 0.4);
      piers.addBox([
        corner(back, -hw, groundLevel),
        corner(back, hw, groundLevel),
        corner(front, hw, groundLevel),
        corner(front, -hw, groundLevel),
        corner(back, -hw, topAbs),
        corner(back, hw, topAbs),
        corner(front, hw, topAbs),
        corner(front, -hw, topAbs),
      ]);
    }
  }

  if (cfg.pierType === 'none') return;
  // Structures that span the whole opening carry themselves; dropping columns under them would be nonsense.
  if (cfg.type === 'masonry') return; // the arcade builds its own piers between the barrels
  if (cfg.type === 'tiedarch' || cfg.type === 'throughtruss') return;

  const spacing = Math.max(8, cfg.pierSpacing);
  const count = Math.max(0, Math.floor(total / spacing) - 0);
  if (count <= 0) return;

  for (let i = 1; i <= count; i++) {
    const d = (total * i) / (count + 1);
    const s = stationAtDistance(stations, d);
    const deckZ = s.base.z;
    const capZ = deckZ + bearingZ;
    if (capZ - (cfg.groundZ ?? 0) < 0.8) continue; // deck is already on grade — no pier needed
    buildPier(piers, s, cfg, capZ, cfg.groundZ ?? 0, halfL, halfR);
  }
}

function buildPier(spec, s, cfg, capZ, groundZ, halfL, halfR) {
  const width = Math.max(0.6, cfg.pierWidth);
  const spanHalf = (halfL + halfR) * 0.5;
  const topH = capZ - s.base.z;
  const botH = groundZ - s.base.z;
  const height = capZ - groundZ;

  const box = (lat, halfWidth, topZ, botZ, thick) => {
    const t = thick * 0.5;
    const f = (lateral, z, along) => ({ ...addScaled(addScaled(s.base, s.left, lateral), s.tangent, along), z });
    spec.addBox([
      f(lat - halfWidth, s.base.z + botZ, -t),
      f(lat + halfWidth, s.base.z + botZ, -t),
      f(lat + halfWidth, s.base.z + botZ, t),
      f(lat - halfWidth, s.base.z + botZ, t),
      f(lat - halfWidth, s.base.z + topZ, -t),
      f(lat + halfWidth, s.base.z + topZ, -t),
      f(lat + halfWidth, s.base.z + topZ, t),
      f(lat - halfWidth, s.base.z + topZ, t),
    ]);
  };

  switch (cfg.pierType) {
    case 'wall': {
      box(0, spanHalf * 0.78, topH, botH, width * 0.8);
      // footing
      box(0, spanHalf * 0.85, botH + 0.6, botH - 0.4, width * 1.6);
      break;
    }
    case 'hammerhead': {
      // cap beam
      box(0, spanHalf * 0.9, topH, topH - Math.min(1.2, height * 0.25), width * 1.1);
      // tapered shaft
      const shaftTop = topH - Math.min(1.2, height * 0.25);
      const steps = 6;
      for (let i = 0; i < steps; i++) {
        const z0 = shaftTop + ((botH - shaftTop) * i) / steps;
        const z1 = shaftTop + ((botH - shaftTop) * (i + 1)) / steps;
        const w0 = width * (1 + 0.5 * (i / steps));
        box(0, w0 * 0.9, z0, z1, w0);
      }
      box(0, width * 2.0, botH + 0.5, botH - 0.5, width * 3.0);
      break;
    }
    case 'vpier': {
      const spread = spanHalf * 0.55;
      const legTop = topH;
      const legBottom = botH;
      for (const side of [1, -1]) {
        const top = { ...addScaled(s.base, s.left, side * spread), z: s.base.z + legTop };
        const bottom = { ...addScaled(s.base, s.left, side * spread * 0.1), z: s.base.z + legBottom };
        addMember(spec, bottom, top, width * 1.1, width * 1.1);
      }
      box(0, spanHalf * 0.9, topH, topH - 0.7, width * 1.1);
      box(0, width * 1.8, botH + 0.5, botH - 0.5, width * 2.6);
      break;
    }
    case 'column':
    default: {
      const offset = spanHalf * 0.45;
      for (const side of [1, -1]) {
        const top = { ...addScaled(s.base, s.left, side * offset), z: capZ - 0.75 };
        const bottom = { ...addScaled(s.base, s.left, side * offset), z: groundZ };
        addTube(spec, [bottom, top], width * 0.5, 12);
      }
      box(0, spanHalf * 0.9, topH, topH - 0.75, width * 1.2);
      box(0, spanHalf * 0.75, botH + 0.45, botH - 0.35, width * 2.4);
      break;
    }
  }
}

// ── furniture ─────────────────────────────────────────────────────────────────────────────────────────────────────

function buildRailings({ cfg, stations, railing, halfL, halfR, deckTopL, deckTopR }) {
  if (cfg.railing === 'none') return;
  const h = Math.max(0.4, cfg.railHeight);
  const edges = [
    { lateral: halfL - 0.08, base: deckTopL, sign: 1 },
    { lateral: -(halfR - 0.08), base: deckTopR, sign: -1 },
  ];

  for (const edge of edges) {
    if (cfg.railing === 'parapet') {
      const t = 0.26;
      for (let i = 0; i < stations.length - 1; i++) {
        const a = stations[i];
        const b = stations[i + 1];
        const li = edge.lateral;
        const lo = edge.lateral - edge.sign * t;
        railing.addFace([a.at(li, edge.base), b.at(li, edge.base), b.at(li, edge.base + h), a.at(li, edge.base + h)].map(flipIf(edge.sign < 0)));
        railing.addFace([a.at(lo, edge.base + h), b.at(lo, edge.base + h), b.at(lo, edge.base), a.at(lo, edge.base)].map(flipIf(edge.sign < 0)));
        railing.addFace([a.at(li, edge.base + h), b.at(li, edge.base + h), b.at(lo, edge.base + h), a.at(lo, edge.base + h)].map(flipIf(edge.sign < 0)));
      }
    } else if (cfg.railing === 'jersey') {
      // 3-break New Jersey profile
      const prof = [
        { l: 0, z: 0 },
        { l: -0.38, z: 0 },
        { l: -0.33, z: 0.08 },
        { l: -0.17, z: 0.33 },
        { l: -0.12, z: h },
        { l: 0, z: h },
      ];
      const rings = stations.map((s) =>
        prof.map((p) => s.at(edge.lateral + edge.sign * p.l, edge.base + p.z)),
      );
      for (let i = 0; i < rings.length - 1; i++) {
        for (let k = 0; k < prof.length - 1; k++) {
          railing.addFace([rings[i][k], rings[i + 1][k], rings[i + 1][k + 1], rings[i][k + 1]].map(flipIf(edge.sign < 0)));
        }
      }
    } else if (cfg.railing === 'steel') {
      const total = stations[stations.length - 1].distance;
      const posts = clamp(Math.round(total / 2.4), 2, 200);
      for (let i = 0; i <= posts; i++) {
        const s = stationAtDistance(stations, (i / posts) * total);
        addMember(railing, s.at(edge.lateral - edge.sign * 0.08, edge.base), s.at(edge.lateral - edge.sign * 0.08, edge.base + h), 0.08, 0.08);
      }
      for (const frac of [0.55, 1.0]) {
        const path = stations.map((s) => s.at(edge.lateral - edge.sign * 0.08, edge.base + h * frac));
        addTube(railing, path, 0.045, 6);
      }
    }
  }
}

const flipIf = (flip) => (p, i, arr) => (flip ? arr[arr.length - 1 - i] : p);

export { stationAtDistance };

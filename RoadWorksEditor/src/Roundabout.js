//============================================================================================================================================
//                                                             ROUNDABOUT.JS
//============================================================================================================================================
// Roundabouts, built on top of the ordinary junction apron rather than beside it.
//
// The topology solver already knows how to trim every arm back from a node and fill the gap with a paved apron. A
// roundabout is that same apron with the trim radius forced out to the outer kerb line, and then three things laid
// over it: a kerbed central island, a mountable truck apron around the island, and a splitter island on each arm
// with its give-way line. Nothing about the merging logic changes — which is exactly why entries stay watertight
// on skewed and uneven arms.

import { MeshSpec } from './MeshSpec.js?v=10';
import { surfaceSampler } from './Markings.js?v=10';

export const ROUNDABOUT_DEFAULTS = {
  islandRadius: 9.0, // kerb line of the central island
  circulating: 7.5, // width of the circulating carriageway
  apron: 1.2, // mountable truck apron just outside the island kerb
  kerbHeight: 0.16,
  dome: 0.7, // how far the planted island rises above its kerb
  splitter: 16.0, // length of the approach splitter islands (0 disables)
  giveWay: true,
};

export function outerRadius(cfg) {
  return (cfg.islandRadius ?? 9) + (cfg.circulating ?? 7.5);
}

export function resolveRoundabout(overrides) {
  return { ...ROUNDABOUT_DEFAULTS, ...(overrides || {}) };
}

const group = (out, name) => out[name] || (out[name] = new MeshSpec(name));

// ── central island ────────────────────────────────────────────────────────────────────────────────────────────────

export function buildRoundabout(out, node, built, options = {}) {
  const cfg = resolveRoundabout(node.roundabout);
  const c = built.centre;
  const curb = group(out, 'curb');
  const island = group(out, options.islandGroup || 'earth');
  const paint = group(out, 'markings');
  const apronGroup = group(out, options.apronGroup || 'pavement');

  const Rk = Math.max(2, cfg.islandRadius);
  const steps = Math.max(24, Math.round(Rk * 3));
  const ring = (radius, z) => {
    const pts = [];
    for (let i = 0; i < steps; i++) {
      const a = (i / steps) * Math.PI * 2;
      pts.push({ x: c.x + Math.cos(a) * radius, y: c.y + Math.sin(a) * radius, z });
    }
    return pts;
  };

  // Mountable apron: a shallow ramp outside the kerb that a semitrailer can track over.
  if (cfg.apron > 0.05) {
    const outer = ring(Rk + cfg.apron, c.z + 0.015);
    const inner = ring(Rk, c.z + 0.1);
    for (let i = 0; i < steps; i++) {
      const j = (i + 1) % steps;
      apronGroup.addFace([outer[i], outer[j], inner[j], inner[i]]);
    }
  }

  // Kerb face, then the planted dome on top of it.
  const base = ring(Rk, c.z + (cfg.apron > 0.05 ? 0.1 : 0.0));
  const top = ring(Rk, c.z + cfg.kerbHeight + 0.1);
  for (let i = 0; i < steps; i++) {
    const j = (i + 1) % steps;
    curb.addFace([base[i], base[j], top[j], top[i]]);
  }
  const peak = { x: c.x, y: c.y, z: c.z + cfg.kerbHeight + 0.1 + Math.max(0, cfg.dome) };
  const rings = 4;
  let prev = top;
  for (let r = 1; r <= rings; r++) {
    const frac = r / rings;
    const radius = Rk * (1 - frac);
    const z = c.z + cfg.kerbHeight + 0.1 + Math.max(0, cfg.dome) * Math.sin((Math.PI / 2) * frac);
    const current = radius > 0.2 ? ring(radius, z) : null;
    for (let i = 0; i < steps; i++) {
      const j = (i + 1) % steps;
      if (current) island.addFace([prev[i], prev[j], current[j], current[i]]);
      else island.addTriangle(prev[i], prev[j], peak);
    }
    if (!current) break;
    prev = current;
  }

  // A dashed lane line around the circulating carriageway once it is wide enough for two lanes.
  if (cfg.circulating >= 9) {
    const mid = Rk + cfg.apron + (cfg.circulating - cfg.apron) * 0.5;
    const dashes = Math.max(12, Math.round(mid * 1.2));
    for (let i = 0; i < dashes; i++) {
      const a0 = ((i + 0.15) / dashes) * Math.PI * 2;
      const a1 = ((i + 0.7) / dashes) * Math.PI * 2;
      arcBand(paint, c, mid - 0.08, mid + 0.08, a0, a1, 0.016);
    }
  }

  if (cfg.giveWay) {
    for (const approach of built.approaches) paintGiveWay(paint, approach);
  }
  return true;
}

function arcBand(spec, c, r0, r1, a0, a1, lift) {
  const steps = Math.max(2, Math.ceil(((a1 - a0) * (r1 + r0) * 0.5) / 0.4));
  for (let i = 0; i < steps; i++) {
    const b0 = a0 + ((a1 - a0) * i) / steps;
    const b1 = a0 + ((a1 - a0) * (i + 1)) / steps;
    const p = (r, a) => ({ x: c.x + Math.cos(a) * r, y: c.y + Math.sin(a) * r, z: c.z + lift });
    spec.addFace([p(r0, b0), p(r0, b1), p(r1, b1), p(r1, b0)]);
  }
}

// Shark's-teeth give-way markings across the entry half of an arm, pointing back at the driver.
function paintGiveWay(spec, approach) {
  const left = approach.roadLeft;
  const right = approach.roadRight;
  const cx = approach.point;
  // `tangent` points into the junction; the entry side is to the right of it.
  const t = approach.tangent;
  const lx = -t.y;
  const ly = t.x;
  const halfWidth = Math.hypot(right.x - cx.x, right.y - cx.y);
  const teeth = Math.max(2, Math.floor(halfWidth / 0.9));
  const lift = 0.016;
  for (let i = 0; i < teeth; i++) {
    const o0 = 0.3 + i * 0.9;
    const o1 = o0 + 0.6;
    if (o1 > halfWidth - 0.1) break;
    const at = (lat, along) => ({
      x: cx.x - lx * lat - t.x * along,
      y: cx.y - ly * lat - t.y * along,
      z: (cx.z + left.z + right.z) / 3 + lift,
    });
    spec.addPolygon([at(o0, 0.4), at(o1, 0.4), at((o0 + o1) / 2, 1.3)]);
  }
  void left;
}

// ── splitter islands ──────────────────────────────────────────────────────────────────────────────────────────────

// A kerbed wedge in the middle of an approach, narrow at the far end and full width at the give-way line. Built
// from the corridor's own cross-sections so it follows a curving approach exactly.
export function buildSplitterIsland(out, sections, profile, atEnd, options = {}) {
  const cfg = { length: 16, width: 2.6, kerb: 0.14, ...options };
  const total = sections[sections.length - 1].distance;
  if (total < cfg.length + 8 || profile.roadWidth < 6) return false;
  const deck = surfaceSampler(sections, profile, cfg.kerb);
  const road = surfaceSampler(sections, profile, 0.004);
  const curb = group(out, 'curb');
  const top = group(out, options.topGroup || 'pavement');

  const d0 = atEnd ? total - cfg.length : 0;
  const d1 = atEnd ? total : cfg.length;
  const steps = Math.max(8, Math.round(cfg.length / 1.0));
  // Width ramps from a nose at the far end to full width at the junction.
  const widthAt = (d) => {
    const frac = atEnd ? (d - d0) / cfg.length : (d1 - d) / cfg.length;
    const eased = Math.sin((Math.PI / 2) * Math.max(0, Math.min(1, frac)));
    return Math.max(0.05, (cfg.width * 0.5) * eased);
  };
  for (let i = 0; i < steps; i++) {
    const da = d0 + ((d1 - d0) * i) / steps;
    const db = d0 + ((d1 - d0) * (i + 1)) / steps;
    const wa = widthAt(da);
    const wb = widthAt(db);
    top.addFace([deck(da, wa), deck(db, wb), deck(db, -wb), deck(da, -wa)]);
    for (const s of [1, -1]) {
      curb.addFace([road(da, s * wa), road(db, s * wb), deck(db, s * wb), deck(da, s * wa)]);
    }
  }
  return true;
}

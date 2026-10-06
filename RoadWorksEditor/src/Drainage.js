//============================================================================================================================================
//                                                              DRAINAGE.JS
//============================================================================================================================================
// The wet infrastructure: gully gratings in the gutter, manhole covers in the carriageway, and the carrier pipe
// buried under the kerb line that joins them up.
//
// Water runs to the edge of a cambered road and then downhill along the gutter, so gullies belong against the kerb
// face, spaced along the corridor and biased towards the low end of each run. Manholes sit over the pipe wherever
// it changes direction or runs too far without an access point. The pipe itself is real swept geometry a metre or
// so down, with a vertical shaft up to each cover — invisible under the road until the drainage layer is switched
// on, which is exactly how it should be.
//
// Everything is built from the corridor's own cross-sections, so the gutter line follows the kerb around curves and
// the pipe follows the road's own vertical profile rather than a flat plane.

import { MeshSpec } from './MeshSpec.js?v=9';

export const DRAINAGE_DEFAULTS = {
  enabled: false,
  gullies: true,
  gullySpacing: 26, // m between gully gratings along each kerb
  side: 'both', // both | left | right — which kerb gets gullies
  manholes: true,
  manholeSpacing: 48, // m between access covers
  pipes: true,
  invert: 1.35, // m from the road surface down to the pipe centre
  pipeRadius: 0.22,
  scupperSpacing: 18, // m between deck scuppers on a bridge
};

export function resolveDrainage(corridor) {
  const d = corridor?.drainage;
  if (!d) return { ...DRAINAGE_DEFAULTS };
  return { ...DRAINAGE_DEFAULTS, ...d, enabled: d.enabled !== false };
}

const group = (out, name) => out[name] || (out[name] = new MeshSpec(name));

// ── placement ─────────────────────────────────────────────────────────────────────────────────────────────────────

// Walks the sections and returns the interpolated station at a given chainage.
function stationAt(sections, distance) {
  const last = sections.length - 1;
  const total = sections[last].distance;
  const d = Math.max(0, Math.min(total, distance));
  let i = 0;
  while (i < last - 1 && sections[i + 1].distance < d) i++;
  const a = sections[i];
  const b = sections[Math.min(i + 1, last)];
  const span = b.distance - a.distance;
  const t = span > 1e-6 ? (d - a.distance) / span : 0;
  const mix = (pa, pb) => ({ x: pa.x + (pb.x - pa.x) * t, y: pa.y + (pb.y - pa.y) * t, z: pa.z + (pb.z - pa.z) * t });
  const left = {
    x: a.frame.left.x + (b.frame.left.x - a.frame.left.x) * t,
    y: a.frame.left.y + (b.frame.left.y - a.frame.left.y) * t,
    z: 0,
  };
  const len = Math.hypot(left.x, left.y) || 1;
  return {
    distance: d,
    base: mix(a.base, b.base),
    centre: mix(a.center, b.center),
    left: { x: left.x / len, y: left.y / len, z: 0 },
    tangent: { x: -left.y / len, y: left.x / len, z: 0 },
    miter: a.miter + (b.miter - a.miter) * t,
  };
}

// ── pieces ────────────────────────────────────────────────────────────────────────────────────────────────────────

// A gully grating: a recessed plate hard against the kerb face, with the kerb inlet slot above it.
function gully(out, station, profile, side) {
  const grate = group(out, 'drainGrate');
  const sign = side === 'left' ? 1 : -1;
  const gutter = (profile.roadHalf - 0.26) * station.miter;
  const halfAcross = 0.23;
  const halfAlong = 0.45;
  const at = (along, across, lift) => ({
    x: station.base.x + station.left.x * sign * (gutter + across) + station.tangent.x * along,
    y: station.base.y + station.left.y * sign * (gutter + across) + station.tangent.y * along,
    z: station.base.z + lift,
  });
  // The gutter is the low point of the camber, so the plate sits a few millimetres under the road surface.
  const sunk = -0.012;
  grate.addFace([at(-halfAlong, -halfAcross, sunk), at(halfAlong, -halfAcross, sunk), at(halfAlong, halfAcross, sunk), at(-halfAlong, halfAcross, sunk)]);
  // frame lip so the grating reads as a casting rather than a painted rectangle
  for (const [a0, a1, c0, c1] of [
    [-halfAlong - 0.06, -halfAlong, -halfAcross - 0.06, halfAcross + 0.06],
    [halfAlong, halfAlong + 0.06, -halfAcross - 0.06, halfAcross + 0.06],
    [-halfAlong, halfAlong, -halfAcross - 0.06, -halfAcross],
    [-halfAlong, halfAlong, halfAcross, halfAcross + 0.06],
  ]) {
    grate.addFace([at(a0, c0, 0.004), at(a1, c0, 0.004), at(a1, c1, 0.004), at(a0, c1, 0.004)]);
  }
  // Kerb inlet: the slot in the kerb face above the grating. A flat plate in the gutter is invisible from anywhere
  // but straight above, so this is the part that tells you there is a gully there when you look along the street.
  const kerbH = Math.max(0.06, profile.curbHeight ?? 0.18);
  const slotTop = Math.min(kerbH - 0.035, 0.145);
  const slotBottom = Math.min(slotTop - 0.02, 0.035);
  if (slotTop > slotBottom) {
    const face = (profile.roadHalf + 0.012) * station.miter - gutter; // relative to the gutter offset used by at()
    const sl = 0.46;
    grate.addFace([
      at(-sl, face, slotBottom), at(sl, face, slotBottom), at(sl, face, slotTop), at(-sl, face, slotTop),
    ]);
    // a shallow lintel over the slot so it is not a decal on the kerb
    grate.addFace([
      at(-sl - 0.05, face, slotTop), at(sl + 0.05, face, slotTop), at(sl + 0.05, face, slotTop + 0.03), at(-sl - 0.05, face, slotTop + 0.03),
    ]);
  }
  return at(0, 0, sunk);
}

// A manhole cover: a disc in the carriageway, offset from the crown so it does not sit under a wheel path.
function manhole(out, station, profile, lateral) {
  const cover = group(out, 'drainCover');
  const r = 0.33;
  const steps = 16;
  const off = lateral * station.miter;
  const crownU = Math.max(-1, Math.min(1, lateral / Math.max(profile.roadHalf, 1e-3)));
  const rise = (profile.crownRise || 0) * (1 - crownU * crownU) - 0.004;
  const centre = {
    x: station.base.x + station.left.x * off,
    y: station.base.y + station.left.y * off,
    z: station.base.z + rise,
  };
  const ring = (radius, lift) => {
    const pts = [];
    for (let i = 0; i < steps; i++) {
      const a = (i / steps) * Math.PI * 2;
      pts.push({
        x: centre.x + (station.tangent.x * Math.cos(a) + station.left.x * Math.sin(a)) * radius,
        y: centre.y + (station.tangent.y * Math.cos(a) + station.left.y * Math.sin(a)) * radius,
        z: centre.z + lift,
      });
    }
    return pts;
  };
  const outer = ring(r, 0);
  const inner = ring(r - 0.07, 0.006);
  for (let i = 0; i < steps; i++) {
    const j = (i + 1) % steps;
    cover.addFace([outer[i], outer[j], inner[j], inner[i]]);
  }
  cover.addPolygon(inner);
  return centre;
}

// Swept circular pipe along a polyline.
export function addTube(spec, points, radius, segments = 8) {
  if (points.length < 2) return;
  const rings = [];
  for (let i = 0; i < points.length; i++) {
    const a = points[Math.max(0, i - 1)];
    const b = points[Math.min(points.length - 1, i + 1)];
    let tx = b.x - a.x;
    let ty = b.y - a.y;
    let tz = b.z - a.z;
    const len = Math.hypot(tx, ty, tz) || 1;
    tx /= len;
    ty /= len;
    tz /= len;
    // Any two vectors perpendicular to the tangent will do for a pipe.
    let ux = -ty;
    let uy = tx;
    let uz = 0;
    // A vertical run (a manhole shaft) leaves that cross product at zero, so fall back to the X axis.
    if (Math.hypot(ux, uy) < 1e-6) {
      ux = 1;
      uy = 0;
    }
    const ul = Math.hypot(ux, uy, uz) || 1;
    ux /= ul;
    uy /= ul;
    const vx = ty * uz - tz * uy;
    const vy = tz * ux - tx * uz;
    const vz = tx * uy - ty * ux;
    const ring = [];
    for (let s = 0; s < segments; s++) {
      const ang = (s / segments) * Math.PI * 2;
      const c = Math.cos(ang) * radius;
      const d = Math.sin(ang) * radius;
      ring.push({ x: points[i].x + ux * c + vx * d, y: points[i].y + uy * c + vy * d, z: points[i].z + uz * c + vz * d });
    }
    rings.push(ring);
  }
  for (let i = 0; i < rings.length - 1; i++) {
    for (let s = 0; s < segments; s++) {
      const t = (s + 1) % segments;
      spec.addFace([rings[i][s], rings[i][t], rings[i + 1][t], rings[i + 1][s]]);
    }
  }
  spec.addPolygon([...rings[0]].reverse());
  spec.addPolygon(rings[rings.length - 1]);
}

// ── corridor driver ───────────────────────────────────────────────────────────────────────────────────────────────

// Builds the drainage for one corridor edge. Returns a count of the castings placed.
export function buildDrainage(edge, sections, out, cfg = {}) {
  const opts = { ...DRAINAGE_DEFAULTS, ...(edge.drainage || {}) };
  if (!opts.enabled || !sections || sections.length < 2) return 0;
  const profile = edge.profile;
  if (!profile || profile.roadWidth < 3) return 0;
  const total = sections[sections.length - 1].distance;
  if (total < 8) return 0;

  let placed = 0;
  const sides = opts.side === 'both' ? ['left', 'right'] : [opts.side];

  // Gullies: evenly spaced along each kerb, first one a few metres in from the junction mouth.
  const gullies = [];
  if (opts.gullies) {
    for (const side of sides) {
      const spacing = Math.max(8, opts.gullySpacing);
      const count = Math.max(1, Math.round((total - 6) / spacing));
      for (let i = 0; i <= count; i++) {
        const d = 3 + ((total - 6) * i) / count;
        const station = stationAt(sections, d);
        gullies.push({ station, centre: gully(out, station, profile, side), side });
        placed++;
      }
    }
  }

  // Manholes sit over the carrier pipe, which runs under the left kerb by convention. They are kept clear of the
  // junction mouths at either end so a cover never lands inside an apron.
  const covers = [];
  const inset = Math.min(8, total * 0.12);
  const run = total - inset * 2;
  if (opts.manholes && run > 4) {
    const spacing = Math.max(12, opts.manholeSpacing);
    const count = Math.max(1, Math.round(run / spacing));
    for (let i = 0; i <= count; i++) {
      const d = inset + (run * i) / count;
      const station = stationAt(sections, d);
      const lateral = (profile.roadHalf - 1.1) * (i % 2 === 0 ? 1 : -1);
      covers.push({ station, centre: manhole(out, station, profile, lateral), lateral });
      placed++;
    }
  }

  // Carrier pipe: under the gutter, following the road's own long section. Every cover gets a vertical shaft down
  // to it, and every gully a short lateral, so the buried view reads as a connected system.
  if (opts.pipes) {
    const pipe = group(out, 'drainPipe');
    const carrierOffset = profile.roadHalf - 0.5;
    const onCarrier = (station, depth = opts.invert) => {
      const off = carrierOffset * station.miter;
      return {
        x: station.base.x + station.left.x * off,
        y: station.base.y + station.left.y * off,
        z: station.base.z - depth,
      };
    };
    const path = [];
    const steps = Math.max(2, Math.round(total / 4));
    for (let i = 0; i <= steps; i++) path.push(onCarrier(stationAt(sections, (total * i) / steps)));
    addTube(pipe, path, opts.pipeRadius, 8);

    for (const cover of covers) {
      const foot = onCarrier(cover.station);
      const below = { x: cover.centre.x, y: cover.centre.y, z: foot.z };
      const top = { x: cover.centre.x, y: cover.centre.y, z: cover.centre.z - 0.1 };
      // elbow across to the shaft, then straight up under the cover
      if (Math.hypot(below.x - foot.x, below.y - foot.y) > 0.05) addTube(pipe, [foot, below], opts.pipeRadius, 8);
      addTube(pipe, [below, top], opts.pipeRadius * 1.5, 10);
    }

    for (const g of gullies) {
      const foot = onCarrier(g.station, opts.invert - 0.25);
      const pot = { x: g.centre.x, y: g.centre.y, z: g.centre.z - 0.12 };
      const below = { x: g.centre.x, y: g.centre.y, z: foot.z };
      addTube(pipe, [below, pot], opts.pipeRadius * 0.9, 8);
      if (Math.hypot(below.x - foot.x, below.y - foot.y) > 0.05) addTube(pipe, [below, foot], opts.pipeRadius * 0.7, 6);
    }
  }

  return placed;
}

// ── bridges ───────────────────────────────────────────────────────────────────────────────────────────────────────
// A deck cannot drain to a gully and a buried pipe: it drains through itself. Scuppers in the gutter take the water
// straight through the slab into a downpipe clipped under the edge of the deck, which is the detail you actually
// see when you stand under a viaduct.

export function buildBridgeDrainage(edge, sections, out, cfg = {}) {
  const opts = { ...DRAINAGE_DEFAULTS, ...(edge.drainage || {}) };
  if (!opts.enabled || !sections || sections.length < 2) return 0;
  const profile = edge.profile;
  const total = sections[sections.length - 1].distance;
  if (total < 12) return 0;

  const grate = group(out, 'drainGrate');
  const pipe = group(out, 'drainPipe');
  const bridge = edge.bridge || {};
  const deckDepth = (bridge.deckThickness ?? 0.85) + 0.15;
  const spacing = Math.max(8, opts.scupperSpacing);
  const count = Math.max(1, Math.round((total - 8) / spacing));
  const sides = opts.side === 'both' ? ['left', 'right'] : [opts.side];

  let placed = 0;
  for (const side of sides) {
    const sign = side === 'left' ? 1 : -1;
    for (let i = 0; i <= count; i++) {
      const station = stationAt(sections, 4 + ((total - 8) * i) / count);
      const lateral = (profile.roadHalf - 0.3) * sign * station.miter;
      const at = (along, across, lift) => ({
        x: station.base.x + station.left.x * (lateral + across * sign) + station.tangent.x * along,
        y: station.base.y + station.left.y * (lateral + across * sign) + station.tangent.y * along,
        z: station.base.z + lift,
      });
      // Scupper plate: a slotted inlet sunk into the deck gutter.
      const halfAlong = 0.3;
      const halfAcross = 0.14;
      grate.addFace([at(-halfAlong, -halfAcross, -0.01), at(halfAlong, -halfAcross, -0.01), at(halfAlong, halfAcross, -0.01), at(-halfAlong, halfAcross, -0.01)]);
      // Downpipe: through the slab, out past the fascia (otherwise it would be buried inside the box) and then a
      // drop in daylight, which is the bit you actually see from under a viaduct.
      const fascia = (side === 'left' ? profile.leftTotalHalf : profile.rightTotalHalf) - (profile.roadHalf - 0.3) + 0.3;
      const head = at(0, 0, -0.02);
      const knee = at(0, 0, -deckDepth + 0.25);
      const elbow = at(0, fascia, -deckDepth + 0.25);
      const foot = at(0, fascia, -deckDepth - 2.6);
      addTube(pipe, [head, knee, elbow, foot], opts.pipeRadius, 8);
      // Bracket collar where the pipe is clipped to the fascia.
      addTube(pipe, [at(0, fascia - 0.22, -deckDepth + 0.25), at(0, fascia + 0.1, -deckDepth + 0.25)], opts.pipeRadius * 1.25, 6);
      placed++;
    }
  }
  return placed;
}

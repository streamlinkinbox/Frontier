//============================================================================================================================================
//                                                             DRIVEWAYS.JS
//============================================================================================================================================
// Vehicle crossovers: the dropped kerb and apron where a street meets somebody's garage.
//
// A crossover is not a slab dumped on top of the footway. The kerb drops almost flush over the width of the
// crossing, ramps back up over a short flare at each end, and the footway tips down to meet it — which is why
// pedestrians feel the dip and why the detail is so recognisable from the air. So the crossing is driven into the
// cross-section generator itself: `kerbDropFn` returns a height factor per chainage and side, `buildCrossSections`
// applies it, and the swept kerb comes out with real dropped crossings in it rather than a decal.
//
// Beyond the footway each crossing gets its own apron slab running back to the property line, flared at the kerb
// so a car can actually turn into it.

import { MeshSpec } from './MeshSpec.js?v=7';

export const DRIVEWAY_DEFAULTS = {
  enabled: false,
  spacing: 16, // m between crossings — one per plot frontage
  width: 3.4, // m of dropped kerb per crossing
  flare: 1.0, // m of ramp at each end of the drop
  side: 'both', // both | left | right
  depth: 5.5, // m from the back of the footway to the garage door
  drop: 0.15, // fraction of the kerb height left standing at the crossing
};

const group = (out, name) => out[name] || (out[name] = new MeshSpec(name));

export function resolveDriveways(corridor) {
  const d = corridor?.driveways;
  if (!d) return { ...DRIVEWAY_DEFAULTS };
  return { ...DRIVEWAY_DEFAULTS, ...d, enabled: d.enabled !== false };
}

// Where the crossings fall along one edge. Kept away from the junction mouths so a crossover never lands inside
// an apron, and staggered side to side so opposite drives do not line up like a zip.
export function drivewayWindows(opts, total, profile) {
  const windows = [];
  if (!opts.enabled || total < 14) return windows;
  // No kerb, no crossover: an unsealed lane with a verge is already drivable off its edge.
  if (!profile || profile.curbHeight < 0.05) return windows;
  const sides = opts.side === 'both' ? ['left', 'right'] : [opts.side];
  const spacing = Math.max(8, opts.spacing);
  const margin = 6;
  const usable = total - margin * 2;
  if (usable < opts.width + 2) return windows;
  const count = Math.max(1, Math.round(usable / spacing));
  for (const side of sides) {
    if (side === 'left' && profile.pavementLeft < 0.4) continue;
    if (side === 'right' && profile.pavementRight < 0.4) continue;
    const stagger = side === 'right' ? spacing * 0.5 : 0;
    for (let i = 0; i <= count; i++) {
      const centre = margin + (usable * i) / count + stagger;
      if (centre < margin || centre > total - margin) continue;
      windows.push({ side, centre, from: centre - opts.width / 2, to: centre + opts.width / 2, flare: opts.flare });
    }
  }
  return windows;
}

// Height factor for the kerb at a chainage: 1 outside a crossing, `drop` inside it, linear across the flare.
export function kerbDropFn(windows, drop = DRIVEWAY_DEFAULTS.drop) {
  if (!windows.length) return null;
  const bySide = { left: [], right: [] };
  for (const w of windows) bySide[w.side].push(w);
  return (distance, side) => {
    let factor = 1;
    for (const w of bySide[side] || []) {
      if (distance < w.from - w.flare || distance > w.to + w.flare) continue;
      let local = 1;
      if (distance < w.from) local = drop + (1 - drop) * ((w.from - distance) / w.flare);
      else if (distance > w.to) local = drop + (1 - drop) * ((distance - w.to) / w.flare);
      else local = drop;
      factor = Math.min(factor, local);
    }
    return factor;
  };
}

// ── apron slabs ───────────────────────────────────────────────────────────────────────────────────────────────────

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
  const lx = a.frame.left.x + (b.frame.left.x - a.frame.left.x) * t;
  const ly = a.frame.left.y + (b.frame.left.y - a.frame.left.y) * t;
  const len = Math.hypot(lx, ly) || 1;
  return {
    base: mix(a.base, b.base),
    left: { x: lx / len, y: ly / len },
    tangent: { x: -ly / len, y: lx / len },
    miter: a.miter + (b.miter - a.miter) * t,
    paveLeft: mix(a.paveLeft, b.paveLeft),
    paveRight: mix(a.paveRight, b.paveRight),
  };
}

// The slab behind the footway: from the back of the pavement out to the garage, flared at the street end.
export function buildDriveways(edge, sections, windows, out, options = {}) {
  if (!windows.length) return 0;
  const profile = edge.profile;
  const slab = group(out, options.group || 'driveway');
  const opts = { ...DRIVEWAY_DEFAULTS, ...(edge.driveways || {}) };
  let built = 0;

  for (const w of windows) {
    const sign = w.side === 'left' ? 1 : -1;
    const footway = w.side === 'left' ? profile.pavementLeft : profile.pavementRight;
    const halfW = opts.width / 2;
    const depth = Math.max(1.5, opts.depth);
    // The crossing surface starts at the dropped kerb and runs across the footway to the property line, so the
    // apron reads as one continuous slab rather than a pad stranded behind the pavement.
    const kerbBack = (profile.roadHalf + profile.curbWidth) * sign;
    const paveBack = (profile.roadHalf + profile.curbWidth + footway) * sign;
    const outer = paveBack + sign * depth;
    const zKerb = profile.curbHeight * opts.drop + 0.006;
    const zPave = profile.curbHeight + footway * 0.02 + 0.006;
    const flare = Math.max(0.3, opts.flare);
    // (lateral, height, half width) along the crossing, from the gutter outwards.
    const ribs = [
      [kerbBack, zKerb, halfW + flare],
      [paveBack, zPave, halfW + flare * 0.35],
      [outer, zPave + 0.02, halfW],
    ];
    const corner = (along, lateral, lift) => {
      const st = stationAt(sections, w.centre + along);
      const off = lateral * st.miter;
      return { x: st.base.x + st.left.x * off, y: st.base.y + st.left.y * off, z: st.base.z + lift };
    };
    for (let i = 0; i < ribs.length - 1; i++) {
      const [lat0, z0, h0] = ribs[i];
      const [lat1, z1, h1] = ribs[i + 1];
      // sub-divide so the slab follows the street's curvature instead of cutting the corner
      const steps = 3;
      for (let k = 0; k < steps; k++) {
        const t0 = k / steps;
        const t1 = (k + 1) / steps;
        const la = lat0 + (lat1 - lat0) * t0;
        const lb = lat0 + (lat1 - lat0) * t1;
        const za = z0 + (z1 - z0) * t0;
        const zb = z0 + (z1 - z0) * t1;
        const ha = h0 + (h1 - h0) * t0;
        const hb = h0 + (h1 - h0) * t1;
        slab.addFace([corner(-ha, la, za), corner(ha, la, za), corner(hb, lb, zb), corner(-hb, lb, zb)]);
      }
    }
    // A low lip down each side so the slab is not a floating plane.
    for (const sgn of [-1, 1]) {
      for (let i = 0; i < ribs.length - 1; i++) {
        const [lat0, z0, h0] = ribs[i];
        const [lat1, z1, h1] = ribs[i + 1];
        slab.addFace([
          corner(h0 * sgn, lat0, z0),
          corner(h1 * sgn, lat1, z1),
          corner(h1 * sgn, lat1, z1 - 0.12),
          corner(h0 * sgn, lat0, z0 - 0.12),
        ]);
      }
    }
    built++;
  }
  return built;
}

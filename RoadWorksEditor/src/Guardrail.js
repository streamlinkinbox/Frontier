//============================================================================================================================================
//                                                             GUARDRAIL.JS
//============================================================================================================================================
// Roadside restraint systems. Bridges carry their own parapets (see BridgeMesh); this is the road-side equivalent,
// and the two deliberately use the same material groups so a barrier can run off a bridge and along the approach
// without changing appearance.
//
// Six families, each a real cross-section swept along the corridor stations:
//
//   wbeam       · W-beam steel rail: two corrugations, 312 mm deep, posts at 1.905 m with blockouts
//   thrie       · Thrie-beam: three corrugations, 500 mm deep, the heavier highway variant
//   cable       · four-rope wire barrier on slender line posts
//   jersey      · concrete New Jersey profile, 3-break
//   parapet     · solid concrete parapet with a chamfered coping
//   pedestrian  · tubular handrail: top rail, mid rail and verticals
//
// Placement follows the same miter-scaled station frames the curbs use, so a barrier tracks a curve exactly and
// never clips the pavement it stands on. By default barriers appear only where they would actually be warranted —
// on fill above `fillTrigger` — which is also what makes an embankment read as a road rather than a ramp.

import { MeshSpec } from './MeshSpec.js?v=4';
import { addMember, addTube } from './BridgeMesh.js?v=4';

export const GUARDRAIL_TYPES = {
  none: 'None',
  wbeam: 'W-beam steel',
  thrie: 'Thrie-beam steel',
  cable: 'Wire rope',
  jersey: 'Jersey barrier',
  parapet: 'Concrete parapet',
  pedestrian: 'Pedestrian handrail',
};

export const GUARDRAIL_DEFAULTS = {
  type: 'none',
  when: 'fill', // fill | always
  fillTrigger: 1.5, // metres of embankment before a barrier is warranted
  side: 'both', // both | left | right
  offset: 0.35, // inset from the outer pavement edge
  height: 0.78, // top of rail above the pavement (concrete families use their own proportions)
};

export function resolveGuardrail(corridor) {
  return { ...GUARDRAIL_DEFAULTS, ...(corridor?.guardrail || {}) };
}

// Concrete families go in their own group so they shade like concrete, not steel.
const CONCRETE = new Set(['jersey', 'parapet']);

// ── cross-sections ────────────────────────────────────────────────────────────────────────────────────────────────
// Profiles are given as [outward, up] in metres, outward being away from the carriageway. The sweep mirrors them
// for the right-hand side.

// Corrugated beam faces: lobes bulge toward the traffic (negative outward).
function corrugation(lobes, depth, height) {
  const pts = [];
  const lobeH = height / lobes;
  for (let i = 0; i < lobes; i++) {
    const z0 = i * lobeH;
    pts.push([0, z0 + lobeH * 0.02]);
    pts.push([-depth, z0 + lobeH * 0.22]);
    pts.push([-depth, z0 + lobeH * 0.78]);
    pts.push([0, z0 + lobeH * 0.98]);
  }
  return pts;
}

const JERSEY = (h) => [
  [0, 0],
  [-0.38, 0],
  [-0.33, 0.08],
  [-0.17, 0.33],
  [-0.12, h],
  [0, h],
];

const PARAPET = (h) => [
  [0, 0],
  [-0.26, 0],
  [-0.26, h - 0.08],
  [-0.21, h],
  [0, h],
  [0, 0],
];

// ── build ─────────────────────────────────────────────────────────────────────────────────────────────────────────

export function buildGuardrail(edge, sections, out, cfg = {}) {
  if (!sections || sections.length < 2) return null;
  const g = edge.guardrail || GUARDRAIL_DEFAULTS;
  if (!g.type || g.type === 'none') return null;

  const profile = edge.profile;
  const groundZ = cfg.groundZ ?? 0;
  const spec = CONCRETE.has(g.type)
    ? out.barrier || (out.barrier = new MeshSpec('barrier'))
    : out.railing || (out.railing = new MeshSpec('railing'));

  const sides = [];
  if (g.side !== 'right') sides.push(1);
  if (g.side !== 'left') sides.push(-1);

  let built = 0;
  for (const side of sides) {
    // Runs: the stretches where this barrier is warranted. 'always' is one run; 'fill' follows the embankment.
    const want = sections.map((s) => (g.when === 'always' ? true : s.base.z - groundZ >= g.fillTrigger));
    let from = -1;
    for (let i = 0; i <= sections.length; i++) {
      const on = i < sections.length && want[i];
      if (on && from < 0) from = i;
      if (!on && from >= 0) {
        if (i - from >= 2) built += emitRun(spec, sections, from, i - 1, side, g, profile);
        from = -1;
      }
    }
  }
  return built ? { type: g.type, runs: built } : null;
}

// The rail itself is a smooth extrusion, so it does not need a ring at every 2 m cross-section. Thinning it to
// ~3 m is invisible and roughly halves the triangles a long barrier costs.
function thin(run, spacing = 3.0) {
  if (run.length < 3) return run;
  const out = [run[0]];
  let last = run[0].distance;
  for (let i = 1; i < run.length - 1; i++) {
    if (run[i].distance - last < spacing) continue;
    out.push(run[i]);
    last = run[i].distance;
  }
  out.push(run[run.length - 1]);
  return out;
}

function emitRun(spec, sections, from, to, side, g, profile) {
  const edgeHalf = side > 0 ? profile.leftTotalHalf : profile.rightTotalHalf;
  const pavement = side > 0 ? profile.pavementLeft : profile.pavementRight;
  const lateral = Math.max(profile.roadHalf + profile.curbWidth * 0.5, edgeHalf - g.offset);
  const standZ = profile.curbHeight + Math.max(0, pavement - g.offset) * 0.02;

  // `out` is measured outward from the carriageway; the left side runs along +left, the right side along −left.
  const place = (section, out, up) => {
    const f = section.frame.left;
    const off = side * (lateral + out) * section.miter;
    return { x: section.base.x + f.x * off, y: section.base.y + f.y * off, z: section.base.z + standZ + up };
  };

  const run = thin(sections.slice(from, to + 1));
  const h = Math.max(0.45, g.height);

  if (g.type === 'jersey' || g.type === 'parapet') {
    const shape = g.type === 'jersey' ? JERSEY(Math.max(0.81, h)) : PARAPET(Math.max(0.9, h));
    sweepProfile(spec, run, shape, place, side);
    return 1;
  }

  if (g.type === 'pedestrian') {
    const posts = Math.max(2, Math.round(length(run) / 2.0));
    railPosts(spec, run, posts, (s) => [place(s, 0, 0), place(s, 0, h)], 0.055, 0.055);
    for (const frac of [0.52, 1.0]) addTube(spec, run.map((s) => place(s, 0, h * frac)), 0.032, 8);
    // kick rail just above the pavement
    addTube(spec, run.map((s) => place(s, 0, 0.14)), 0.022, 6);
    return 1;
  }

  if (g.type === 'cable') {
    const posts = Math.max(2, Math.round(length(run) / 3.2));
    railPosts(spec, run, posts, (s) => [place(s, 0, 0), place(s, 0, h)], 0.07, 0.05);
    for (const frac of [0.42, 0.62, 0.82, 1.0]) {
      addTube(spec, run.map((s) => place(s, -0.03, h * frac)), 0.016, 5);
    }
    return 1;
  }

  // W-beam / thrie-beam: corrugated rail on posts with blockouts
  const lobes = g.type === 'thrie' ? 3 : 2;
  const beamHeight = g.type === 'thrie' ? 0.5 : 0.312;
  const depth = 0.083;
  const railTop = h;
  const railBottom = railTop - beamHeight;
  const shape = corrugation(lobes, depth, beamHeight).map(([o, z]) => [o - depth, z + railBottom]);
  sweepProfile(spec, run, shape, place, side, false);

  const spacing = g.type === 'thrie' ? 1.905 : 1.905;
  const posts = Math.max(2, Math.round(length(run) / spacing));
  railPosts(
    spec,
    run,
    posts,
    (s) => [place(s, -0.02, -0.02), place(s, -0.02, railBottom + beamHeight * 0.5)],
    0.14,
    0.12,
  );
  // blockout spacers between post and rail
  for (let i = 0; i <= posts; i++) {
    const s = at(run, i / posts);
    addMember(
      spec,
      place(s, -0.02, railBottom + beamHeight * 0.5),
      place(s, -depth - 0.02, railBottom + beamHeight * 0.5),
      0.12,
      0.16,
    );
  }
  return 1;
}

// Sweeps an open profile (list of [outward, up]) along the run. `closed` adds the back face so concrete reads solid.
function sweepProfile(spec, run, shape, place, side, closed = true) {
  const rings = run.map((s) => shape.map(([o, z]) => place(s, o, z)));
  const flip = side < 0;
  for (let i = 0; i < rings.length - 1; i++) {
    for (let k = 0; k < shape.length - 1; k++) {
      const quad = [rings[i][k], rings[i + 1][k], rings[i + 1][k + 1], rings[i][k + 1]];
      spec.addFace(flip ? quad.slice().reverse() : quad);
    }
  }
  if (closed) {
    spec.addPolygon(flip ? rings[0] : rings[0].slice().reverse());
    spec.addPolygon(flip ? rings[rings.length - 1].slice().reverse() : rings[rings.length - 1]);
  }
}

function railPosts(spec, run, count, span, width, thickness) {
  for (let i = 0; i <= count; i++) {
    const [a, b] = span(at(run, i / count));
    addMember(spec, a, b, width, thickness);
  }
}

function at(run, t) {
  const i = Math.min(run.length - 1, Math.max(0, Math.round(t * (run.length - 1))));
  return run[i];
}

function length(run) {
  return Math.max(1e-3, run[run.length - 1].distance - run[0].distance);
}

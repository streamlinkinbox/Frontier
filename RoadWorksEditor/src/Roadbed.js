//============================================================================================================================================
//                                                              ROADBED.JS
//============================================================================================================================================
// What holds an elevated road up.
//
// Until now a corridor that climbed above ground level was a floating ribbon: the only thing below the pavement was
// the 18 cm outer drop, so a ramp at +24 m showed its own backfaces and ended in mid-air. This module closes that
// underside and gives the fill a real shape, procedurally, per station:
//
//   embankment — earth batter from the pavement edge down to grade at a configurable slope, with a closed toe
//   wall       — board-marked retaining wall with a coping lip and a battered face
//   slab       — a shallow box soffit for a road that is deliberately a floating structure
//   auto       — embankment while the fill is shallow, retaining wall once it exceeds `maxFill`
//
// The mirror image of all that is a cutting: where the corridor runs *below* ground the ground has to be taken away
// rather than piled up, so each side gets a drainage ditch at the verge and a batter climbing back to grade — or a
// board-marked retaining wall once the cut is deeper than `maxCut`.
//
// Every cross-section is built from the same station frames the carriageway uses, so the skirt follows the miter
// scaling around curves exactly like the curbs do, and it starts/stops cleanly where the corridor crosses grade.

import { MeshSpec } from './MeshSpec.js?v=9';

export const ROADBED_DEFAULTS = {
  mode: 'auto', // auto | embankment | wall | slab | none
  slope: 1.6, // metres of run per metre of rise on an embankment batter
  maxFill: 7.0, // above this the auto mode switches to a retaining wall
  wallBatter: 0.035, // lean on the wall face, metres per metre of height
  copeWidth: 0.16,
  copeDepth: 0.34,
  slabDepth: 0.55,
  cut: true, // excavate a batter where the road runs below ground level
  cutSlope: 1.5, // metres of run per metre of depth on a cut batter
  maxCut: 8.0, // deeper than this and the cutting is held back by a wall
  ditchWidth: 1.1, // m of drainage ditch between the verge and the toe of the batter
  ditchDepth: 0.45,
};

const MIN_FILL = 0.12; // below this the corridor is effectively at grade

export function resolveRoadbed(corridor) {
  return { ...ROADBED_DEFAULTS, ...(corridor?.roadbed || {}) };
}

// ── station ribs ──────────────────────────────────────────────────────────────────────────────────────────────────

// Lateral offset from a station, re-using the station's own frame and miter factor so the skirt tracks curves.
function offset(section, lateral, z) {
  const f = section.frame.left;
  const l = lateral * section.miter;
  return { x: section.base.x + f.x * l, y: section.base.y + f.y * l, z };
}

// Outer boundary of one side, ordered top → bottom. `side` is +1 left, -1 right.
function rib(section, side, mode, rb, groundZ, profile) {
  const top = side > 0 ? section.paveLeftBase : section.paveRightBase;
  const edge = side > 0 ? profile.leftTotalHalf : profile.rightTotalHalf;
  const h = Math.max(0, section.base.z - groundZ);

  if (mode === 'embankment') {
    return [top, offset(section, side * (edge + h * rb.slope), groundZ)];
  }
  if (mode === 'wall') {
    return [
      top,
      offset(section, side * (edge + rb.copeWidth), top.z - 0.05),
      offset(section, side * (edge + rb.copeWidth), top.z - rb.copeDepth),
      offset(section, side * (edge + rb.copeWidth * 0.3 + h * rb.wallBatter), groundZ),
    ];
  }
  // slab
  return [top, offset(section, side * edge, top.z - rb.slabDepth)];
}

function ribLength(points) {
  const out = [0];
  for (let i = 1; i < points.length; i++) {
    out.push(out[i - 1] + Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y, points[i].z - points[i - 1].z));
  }
  return out;
}

// ── corridor skirt ────────────────────────────────────────────────────────────────────────────────────────────────

export function buildRoadbedMesh(edge, sections, out, cfg = {}) {
  if (!sections || sections.length < 2) return null;
  if (edge.family === 'bridge') return null; // see buildBridgeApproachFill

  const rb = edge.roadbed || ROADBED_DEFAULTS;
  if (rb.mode === 'none') return null;

  const groundZ = cfg.groundZ ?? 0;
  const profile = edge.profile;
  const fill = sections.map((s) => s.base.z - groundZ);
  const peak = Math.max(...fill);
  if (peak <= MIN_FILL) return null;

  const mode = rb.mode === 'auto' ? (peak > rb.maxFill ? 'wall' : 'embankment') : rb.mode;
  const earth = mode === 'embankment';
  const spec = earth
    ? out.earth || (out.earth = new MeshSpec('earth'))
    : out.roadbed || (out.roadbed = new MeshSpec('roadbed'));

  // Build only over the runs that are actually above grade, so a corridor that dips to ground has no skirt there.
  let runStart = -1;
  let built = 0;
  for (let i = 0; i <= sections.length; i++) {
    const above = i < sections.length && fill[i] > MIN_FILL;
    if (above && runStart < 0) runStart = i;
    if (!above && runStart >= 0) {
      if (i - runStart >= 2) built += emitRun(spec, sections, runStart, i - 1, mode, rb, groundZ, profile);
      runStart = -1;
    }
  }
  return built ? { mode, peak } : null;
}

function emitRun(spec, sections, from, to, mode, rb, groundZ, profile) {
  const left = [];
  const right = [];
  for (let i = from; i <= to; i++) {
    left.push(rib(sections[i], 1, mode, rb, groundZ, profile));
    right.push(rib(sections[i], -1, mode, rb, groundZ, profile));
  }
  const rows = left[0].length;
  const vLeft = ribLength(left[0]);

  for (let i = 0; i < left.length - 1; i++) {
    const u0 = sections[from + i].distance;
    const u1 = sections[from + i + 1].distance;
    for (let k = 0; k < rows - 1; k++) {
      const v0 = vLeft[k];
      const v1 = vLeft[k + 1];
      spec.addFace(
        [left[i][k], left[i + 1][k], left[i + 1][k + 1], left[i][k + 1]],
        [{ x: u0, y: v0 }, { x: u1, y: v0 }, { x: u1, y: v1 }, { x: u0, y: v1 }],
      );
      spec.addFace(
        [right[i][k], right[i][k + 1], right[i + 1][k + 1], right[i + 1][k]],
        [{ x: u0, y: v0 }, { x: u0, y: v1 }, { x: u1, y: v1 }, { x: u1, y: v0 }],
      );
    }
    // underside, closing the solid off so nothing shows through from below
    const lb = left[i][rows - 1];
    const lb2 = left[i + 1][rows - 1];
    const rbm = right[i][rows - 1];
    const rb2 = right[i + 1][rows - 1];
    spec.addFace(
      [lb, lb2, rb2, rbm],
      [{ x: u0, y: 0 }, { x: u1, y: 0 }, { x: u1, y: 6 }, { x: u0, y: 6 }],
    );
  }

  // end caps: a strip between the two ribs rather than a fan, so the coping notch cannot flip a triangle
  capRun(spec, left[0], right[0], true);
  capRun(spec, left[left.length - 1], right[right.length - 1], false);
  return left.length;
}

function capRun(spec, ribL, ribR, atStart) {
  for (let k = 0; k < ribL.length - 1; k++) {
    const quad = [ribL[k], ribL[k + 1], ribR[k + 1], ribR[k]];
    spec.addFace(atStart ? quad : quad.slice().reverse());
  }
}

// ── cuttings ──────────────────────────────────────────────────────────────────────────────────────────────────────
// Below grade the geometry is inverted: from the back of the footway the ground drops into a ditch, then climbs the
// batter to daylight. The strip is closed at the top with a thin lip of ground so the cut reads as excavated earth
// rather than a hole with no edges.

function cutRib(section, side, rb, groundZ, profile, deep) {
  const top = side > 0 ? section.paveLeftBase : section.paveRightBase;
  const edge = side > 0 ? profile.leftTotalHalf : profile.rightTotalHalf;
  const depth = Math.max(0, groundZ - section.base.z);
  const out = [top];
  if (deep) {
    // Retaining wall: straight up the face, a coping lip, then a short shelf of ground.
    out.push(offset(section, side * (edge + 0.1), top.z));
    out.push(offset(section, side * (edge + 0.1 + depth * rb.wallBatter), groundZ - rb.copeDepth * 0.4));
    out.push(offset(section, side * (edge + 0.1 + depth * rb.wallBatter + rb.copeWidth), groundZ));
    out.push(offset(section, side * (edge + 0.9 + depth * rb.wallBatter + rb.copeWidth), groundZ + 0.02));
    return out;
  }
  const ditch = Math.min(rb.ditchDepth, Math.max(0.1, depth * 0.5));
  out.push(offset(section, side * (edge + rb.ditchWidth * 0.5), top.z - ditch));
  out.push(offset(section, side * (edge + rb.ditchWidth), top.z));
  out.push(offset(section, side * (edge + rb.ditchWidth + depth * rb.cutSlope), groundZ));
  out.push(offset(section, side * (edge + rb.ditchWidth + depth * rb.cutSlope + 0.9), groundZ + 0.02));
  return out;
}

function emitCutRun(spec, sections, from, to, rb, groundZ, profile, deep) {
  const left = [];
  const right = [];
  for (let i = from; i <= to; i++) {
    left.push(cutRib(sections[i], 1, rb, groundZ, profile, deep));
    right.push(cutRib(sections[i], -1, rb, groundZ, profile, deep));
  }
  const rows = left[0].length;
  const vLeft = ribLength(left[0]);
  for (let i = 0; i < left.length - 1; i++) {
    const u0 = sections[from + i].distance;
    const u1 = sections[from + i + 1].distance;
    for (let k = 0; k < rows - 1; k++) {
      const v0 = vLeft[k];
      const v1 = vLeft[k + 1];
      // Wound the opposite way round from an embankment: a cutting is seen from the inside.
      spec.addFace(
        [left[i][k], left[i][k + 1], left[i + 1][k + 1], left[i + 1][k]],
        [{ x: u0, y: v0 }, { x: u0, y: v1 }, { x: u1, y: v1 }, { x: u1, y: v0 }],
      );
      spec.addFace(
        [right[i][k], right[i + 1][k], right[i + 1][k + 1], right[i][k + 1]],
        [{ x: u0, y: v0 }, { x: u1, y: v0 }, { x: u1, y: v1 }, { x: u0, y: v1 }],
      );
    }
  }
  capRun(spec, left[0], right[0], false);
  capRun(spec, left[left.length - 1], right[right.length - 1], true);
  return left.length;
}

// Excavates the cutting for one corridor. Returns `{ mode, depth }` when anything was dug.
export function buildCuttingMesh(edge, sections, out, cfg = {}) {
  if (!sections || sections.length < 2) return null;
  if (edge.family === 'bridge') return null;
  const rb = edge.roadbed || ROADBED_DEFAULTS;
  if (rb.mode === 'none' || rb.cut === false) return null;

  const groundZ = cfg.groundZ ?? 0;
  const profile = edge.profile;
  const cut = sections.map((s) => groundZ - s.base.z);
  const deepest = Math.max(...cut);
  if (deepest <= MIN_FILL) return null;

  const deep = deepest > (rb.maxCut ?? 8);
  const spec = deep
    ? out.roadbed || (out.roadbed = new MeshSpec('roadbed'))
    : out.earth || (out.earth = new MeshSpec('earth'));

  let runStart = -1;
  let built = 0;
  for (let i = 0; i <= sections.length; i++) {
    const below = i < sections.length && cut[i] > MIN_FILL;
    if (below && runStart < 0) runStart = i;
    if (!below && runStart >= 0) {
      if (i - runStart >= 2) built += emitCutRun(spec, sections, runStart, i - 1, rb, groundZ, profile, deep);
      runStart = -1;
    }
  }
  return built ? { mode: deep ? 'wall' : 'batter', depth: deepest } : null;
}

// ── bridge approaches ─────────────────────────────────────────────────────────────────────────────────────────────
// A bridge that climbs out of the ground used to keep its full deck box, girders and abutment all the way down to
// grade: the soffit ended up underground and a stray abutment block sat in the middle of the ramp. Real approaches
// are filled, not spanned, so wherever the deck has less than its own structural depth of daylight beneath it we
// build an embankment (or wall) from the deck edge to grade and let the fill swallow the buried structure.

export function buildBridgeApproachFill(edge, sections, out, cfg = {}) {
  if (!sections || sections.length < 2) return null;
  if (edge.family !== 'bridge') return null;

  const rb = edge.roadbed || ROADBED_DEFAULTS;
  if (rb.mode === 'none') return null;

  const groundZ = cfg.groundZ ?? 0;
  const profile = edge.profile;
  const bridge = edge.bridge || {};
  // Daylight the structure needs: deck slab plus whatever hangs below it.
  const depth = (bridge.deckThickness ?? 0.85) + (bridge.type === 'beam' || bridge.type === 'box' ? bridge.girderDepth ?? 1.3 : 0.4);
  const clearance = depth + 0.6;

  const fill = sections.map((s) => s.base.z - groundZ);
  const mode = rb.mode === 'auto' || rb.mode === 'slab' ? 'embankment' : rb.mode;
  const spec = mode === 'embankment'
    ? out.earth || (out.earth = new MeshSpec('earth'))
    : out.roadbed || (out.roadbed = new MeshSpec('roadbed'));

  let runStart = -1;
  let built = 0;
  for (let i = 0; i <= sections.length; i++) {
    const buried = i < sections.length && fill[i] > MIN_FILL && fill[i] < clearance;
    if (buried && runStart < 0) runStart = i;
    if (!buried && runStart >= 0) {
      if (i - runStart >= 2) built += emitRun(spec, sections, runStart, i - 1, mode, rb, groundZ, profile);
      runStart = -1;
    }
  }
  return built ? { mode, clearance } : null;
}

// ── junction aprons ───────────────────────────────────────────────────────────────────────────────────────────────
// An elevated junction gets the same treatment: the outer pavement ring is dropped to grade as a wall and the
// underside is fanned to the centre, so a crossing up on an embankment is a solid, not a hovering disc.

export function buildApronSkirt(junction, out, cfg = {}) {
  if (!junction || !junction.paveBaseArcs?.length) return null;
  const groundZ = cfg.groundZ ?? 0;
  // Measure from the *base* of the pavement ring, not the cambered centre: a wide carriageway's crown alone can
  // be 150 mm, which used to convince an at-grade junction that it was on an embankment and fan a skirt out from
  // under it — visible wherever the apron ring is not a closed loop, such as a fork.
  let low = Infinity;
  for (const arc of junction.paveBaseArcs) for (const p of arc) low = Math.min(low, p.z);
  if (!Number.isFinite(low) || low - groundZ <= MIN_FILL) return null;

  const spec = out.roadbed || (out.roadbed = new MeshSpec('roadbed'));
  const under = { x: junction.centre.x, y: junction.centre.y, z: groundZ - 0.02 };

  for (const arc of junction.paveBaseArcs) {
    for (let i = 0; i < arc.length - 1; i++) {
      const a = arc[i];
      const b = arc[i + 1];
      const aG = { x: a.x, y: a.y, z: groundZ };
      const bG = { x: b.x, y: b.y, z: groundZ };
      spec.addFace(
        [a, b, bG, aG],
        [{ x: 0, y: a.z - groundZ }, { x: 3, y: b.z - groundZ }, { x: 3, y: 0 }, { x: 0, y: 0 }],
      );
      spec.addTriangle(aG, bG, under);
    }
  }
  // The arm mouths are left open on purpose: the corridor skirt caps its own run at exactly those stations,
  // and drawing both would leave two coincident faces fighting for the same pixels.
  return true;
}

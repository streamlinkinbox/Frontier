//============================================================================================================================================
//                                                               SIGNS.JS
//============================================================================================================================================
// Junction furniture: stop bars, zebra crossings and the signs themselves.
//
// Everything here is positioned from the *approach frames* the junction mesh already uses — the trimmed end
// cross-section of each arm — so a stop bar sits square across its carriageway and a sign lands on the pavement of
// the arm it governs, whatever the angle the arms meet at and whatever width each one is.
//
// Markings are painted 13 mm proud of the carriageway with the same camber as the road underneath, so they never
// z-fight and never float off a cambered surface.

import { MeshSpec } from './MeshSpec.js?v=3';
import { SIGN_UV } from './Textures.js?v=3';

export const SIGNAGE_DEFAULTS = {
  signage: 'stop', // stop | yield | none
  stopBars: true,
  crosswalks: true,
  signHeight: 2.25, // centre of the plate above the pavement
  signSize: 0.82, // plate across the flats
};

const LIFT = 0.013;

export function buildJunctionFurniture(junction, node, out, cfg = {}) {
  if (!junction || node.degree < 3) return 0;
  const opts = { ...SIGNAGE_DEFAULTS, ...cfg };
  if (opts.signage === 'none' && !opts.stopBars && !opts.crosswalks) return 0;

  let placed = 0;
  for (const approach of junction.approaches) {
    if (approach.family === 'bridge') continue;
    const frame = planarFrame(approach);
    if (!frame) continue;
    const p = approach.profile;

    if (opts.stopBars !== false && cfg.markings !== false) {
      const markings = out.markings || (out.markings = new MeshSpec('markings'));
      // Right-hand traffic: the bar covers the approaching half only, centreline → right edge.
      bar(markings, frame, p, -0.12, -(p.roadHalf - 0.2), 1.3, 1.85);
    }

    if (opts.crosswalks !== false && cfg.markings !== false && p.roadWidth >= 5) {
      const markings = out.markings || (out.markings = new MeshSpec('markings'));
      zebra(markings, frame, p, 2.4, 5.1);
    }

    if (opts.signage !== 'none') {
      placed += post(out, frame, p, opts) ? 1 : 0;
    }
  }
  return placed;
}

// ── frame helpers ─────────────────────────────────────────────────────────────────────────────────────────────────

// Approach tangents point *into* the junction; we work in (left, back) coordinates measured from the arm mouth.
function planarFrame(approach) {
  const t = approach.tangent;
  const len = Math.hypot(t.x, t.y);
  if (len < 1e-5) return null;
  const tangent = { x: t.x / len, y: t.y / len };
  const left = { x: -tangent.y, y: tangent.x };
  return { origin: approach.base, tangent, left, crownRise: approach.profile.crownRise };
}

// lateral: +left / −right in metres. back: metres away from the junction along the arm.
function at(frame, profile, lateral, back, lift = LIFT) {
  const u = Math.max(-1, Math.min(1, lateral / Math.max(profile.roadHalf, 1e-3)));
  const rise = profile.crownRise * (1 - u * u) + lift;
  return {
    x: frame.origin.x + frame.left.x * lateral - frame.tangent.x * back,
    y: frame.origin.y + frame.left.y * lateral - frame.tangent.y * back,
    z: frame.origin.z + rise,
  };
}

function bar(spec, frame, profile, lateralA, lateralB, back, backEnd) {
  spec.addFace(
    [at(frame, profile, lateralA, back), at(frame, profile, lateralB, back), at(frame, profile, lateralB, backEnd), at(frame, profile, lateralA, backEnd)],
    [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }],
  );
}

function zebra(spec, frame, profile, nearBack, farBack) {
  const span = profile.roadWidth - 0.6;
  const stripe = 0.52;
  const gap = 0.5;
  const count = Math.max(2, Math.floor(span / (stripe + gap)));
  const pitch = span / count;
  for (let i = 0; i < count; i++) {
    const a = profile.roadHalf - 0.3 - i * pitch;
    const b = a - stripe;
    bar(spec, frame, profile, a, b, nearBack, farBack);
  }
}

// ── sign props ────────────────────────────────────────────────────────────────────────────────────────────────────

function post(out, frame, profile, opts) {
  const face = out.signFace || (out.signFace = new MeshSpec('signFace'));
  const steel = out.signPost || (out.signPost = new MeshSpec('signPost'));

  const pavement = profile.pavementRight;
  if (pavement < 0.5) return false; // nowhere to stand

  const lateral = -(profile.roadHalf + profile.curbWidth + pavement * 0.5);
  const back = 1.1;
  const ground = at(frame, profile, lateral, back, profile.curbHeight);
  const r = opts.signSize * 0.5;
  const centreZ = ground.z + opts.signHeight;
  const plateCentre = { x: ground.x, y: ground.y, z: centreZ };

  // pole
  const half = 0.045;
  const L = frame.left;
  const T = frame.tangent;
  const corner = (sl, st, z) => ({ x: ground.x + L.x * sl * half + T.x * st * half, y: ground.y + L.y * sl * half + T.y * st * half, z });
  const top = centreZ + r * 0.1;
  steel.addBox([
    corner(-1, -1, ground.z), corner(1, -1, ground.z), corner(1, 1, ground.z), corner(-1, 1, ground.z),
    corner(-1, -1, top), corner(1, -1, top), corner(1, 1, top), corner(-1, 1, top),
  ]);

  // plate: an octagon for STOP, a triangle for YIELD, facing back down the approach
  const kind = opts.signage === 'yield' ? 'yield' : 'stop';
  const sides = kind === 'yield' ? 3 : 8;
  const phase = kind === 'yield' ? Math.PI / 2 : Math.PI / 8;
  const thickness = 0.05;
  const front = [];
  const back2 = [];
  const uvsFront = [];
  const uvsBack = [];
  const cellF = SIGN_UV[kind];
  const cellB = SIGN_UV.back;

  for (let i = 0; i < sides; i++) {
    const a = (Math.PI * 2 * i) / sides + phase;
    const lx = Math.cos(a) * r;
    const lz = Math.sin(a) * r;
    // the plate faces oncoming traffic, i.e. along −tangent
    const px = plateCentre.x + L.x * lx;
    const py = plateCentre.y + L.y * lx;
    front.push({ x: px - T.x * thickness * 0.5, y: py - T.y * thickness * 0.5, z: plateCentre.z + lz });
    back2.push({ x: px + T.x * thickness * 0.5, y: py + T.y * thickness * 0.5, z: plateCentre.z + lz });
    const u = 0.5 + lx / (2 * r);
    const v = 0.5 + lz / (2 * r);
    uvsFront.push({ x: cellF.u0 + u * (cellF.u1 - cellF.u0), y: cellF.v0 + v * (cellF.v1 - cellF.v0) });
    uvsBack.push({ x: cellB.u0 + u * (cellB.u1 - cellB.u0), y: cellB.v0 + v * (cellB.v1 - cellB.v0) });
  }

  // Ordered by increasing angle the ring winds about +tangent, so the readable face is the reversed one.
  face.addPolygon(front.slice().reverse(), uvsFront.slice().reverse());
  face.addPolygon(back2, uvsBack);
  for (let i = 0; i < sides; i++) {
    const j = (i + 1) % sides;
    face.addFace([front[i], back2[i], back2[j], front[j]], [uvsBack[i], uvsBack[i], uvsBack[j], uvsBack[j]]);
  }
  return true;
}

// Network analysis: detect junctions where splines cross (X) or branch (Y/T),
// split splines into arms between junctions, and place mouth stations where
// each arm meets its junction chamber.

import { distXZ, norm, sub, v3 } from './vec.js';
import { projectOnDense, resample, sampleDense, segIntersectXZ } from './spline.js';

const EPS = 1e-3;

/** Interpolate pos+tan on a dense polyline at arc length s. */
export function sampleAt(dense, s) {
  if (dense.length === 0) return { pos: v3(), tan: v3(0, 0, 1), s: 0 };
  if (s <= dense[0].s) return dense[0];
  const last = dense[dense.length - 1];
  if (s >= last.s) return last;
  let j = 0;
  while (j < dense.length - 2 && dense[j + 1].s < s) j++;
  const a = dense[j];
  const b = dense[j + 1];
  const span = b.s - a.s;
  const u = span > EPS ? (s - a.s) / span : 0;
  return {
    pos: v3(
      a.pos.x + (b.pos.x - a.pos.x) * u,
      a.pos.y + (b.pos.y - a.pos.y) * u,
      a.pos.z + (b.pos.z - a.pos.z) * u
    ),
    tan: norm(v3(
      a.tan.x + (b.tan.x - a.tan.x) * u,
      a.tan.y + (b.tan.y - a.tan.y) * u,
      a.tan.z + (b.tan.z - a.tan.z) * u
    )),
    s,
  };
}

function lerpH(a, b, t) {
  return a + (b - a) * t;
}

/**
 * Analyse splines and produce:
 *   { dense, arms, junctions, overpasses }
 * arms: { id, splineId, s0, s1, stations:[{pos,tan,s}], ends: [{junctionId, mouthS, dir}|null] }
 * junctions: { id, x, y, z, arms: [{armId, mouthS, dir, mouthDist}] }
 */
export function analyzeNetwork(splines, params) {
  const {
    stationSpacing = 3,
    junctionDist = 11,
    detectRadius = 5,
    heightTolerance = 6.5,
    minArmLen = 26,
    junctionMergeDist = 9,
  } = params;

  // ---- dense sampling
  const dense = splines.map((sp) => sampleDense(sp.points, 20));

  // ---- junction candidates
  const candidates = [];
  const overpasses = [];

  // crossings
  for (let i = 0; i < dense.length; i++) {
    for (let j = i + 1; j < dense.length; j++) {
      const A = dense[i];
      const B = dense[j];
      for (let a = 0; a < A.length - 1; a++) {
        for (let b = 0; b < B.length - 1; b++) {
          const hit = segIntersectXZ(A[a].pos, A[a + 1].pos, B[b].pos, B[b + 1].pos);
          if (!hit) continue;
          const hA = lerpH(A[a].pos.y, A[a + 1].pos.y, hit.t);
          const hB = lerpH(B[b].pos.y, B[b + 1].pos.y, hit.u);
          const sA = lerpH(A[a].s, A[a + 1].s, hit.t);
          const sB = lerpH(B[b].s, B[b + 1].s, hit.u);
          const pos = { x: hit.x, y: (hA + hB) / 2, z: hit.z };
          if (Math.abs(hA - hB) > heightTolerance) {
            overpasses.push({ a: i, b: j, ...pos, dh: Math.abs(hA - hB) });
            continue;
          }
          const endEps = stationSpacing * 0.75;
          const evA = sA < endEps ? { spline: i, s: 0, kind: 'end' }
            : sA > A[A.length - 1].s - endEps ? { spline: i, s: A[A.length - 1].s, kind: 'end' }
              : { spline: i, s: sA, kind: 'cross' };
          const evB = sB < endEps ? { spline: j, s: 0, kind: 'end' }
            : sB > B[B.length - 1].s - endEps ? { spline: j, s: B[B.length - 1].s, kind: 'end' }
              : { spline: j, s: sB, kind: 'cross' };
          candidates.push({ x: pos.x, y: pos.y, z: pos.z, events: [evA, evB], src: 'cross' });
        }
      }
    }
  }

  // endpoint proximity -> Y / T / star junctions
  for (let i = 0; i < dense.length; i++) {
    const Li = dense[i][dense[i].length - 1].s;
    for (const endS of [0, Li]) {
      const endPos = sampleAt(dense[i], endS).pos;
      for (let j = 0; j < dense.length; j++) {
        if (i === j) continue;
        const Lj = dense[j][dense[j].length - 1].s;
        const proj = projectOnDense(dense[j], endPos);
        if (proj.dist > detectRadius) continue;
        if (Math.abs(proj.pos.y - endPos.y) > heightTolerance) continue;
        // both at ends and already crossing-detected? clustering handles it.
        const endEps = stationSpacing * 0.75;
        const evJ = proj.s < endEps ? { spline: j, s: 0, kind: 'end' }
          : proj.s > Lj - endEps ? { spline: j, s: Lj, kind: 'end' }
            : { spline: j, s: proj.s, kind: 'cross' };
        candidates.push({
          x: (endPos.x + proj.pos.x) / 2,
          y: (endPos.y + proj.pos.y) / 2,
          z: (endPos.z + proj.pos.z) / 2,
          events: [{ spline: i, s: endS, kind: 'end' }, evJ],
          src: 'end',
        });
      }
    }
  }

  // ---- cluster candidates into junctions
  const parent = candidates.map((_, i) => i);
  const find = (x) => (parent[x] === x ? x : (parent[x] = find(parent[x])));
  const union = (a, b) => {
    const ra = find(a);
    const rb = find(b);
    if (ra !== rb) parent[rb] = ra;
  };
  for (let i = 0; i < candidates.length; i++) {
    for (let j = i + 1; j < candidates.length; j++) {
      const d = distXZ(candidates[i], candidates[j]);
      const dy = Math.abs(candidates[i].y - candidates[j].y);
      if (d < junctionMergeDist && dy < heightTolerance) union(i, j);
    }
  }

  const clusters = new Map();
  for (let i = 0; i < candidates.length; i++) {
    const r = find(i);
    if (!clusters.has(r)) clusters.set(r, []);
    clusters.get(r).push(candidates[i]);
  }

  const junctions = [];
  for (const group of clusters.values()) {
    let x = 0;
    let y = 0;
    let z = 0;
    for (const c of group) {
      x += c.x;
      y += c.y;
      z += c.z;
    }
    x /= group.length;
    y /= group.length;
    z /= group.length;
    // merge events: same spline, s within mergeEps -> one event
    const events = [];
    for (const c of group) {
      for (const ev of c.events) {
        const dup = events.find((e) => e.spline === ev.spline && Math.abs(e.s - ev.s) < junctionMergeDist * 0.5);
        if (dup) {
          dup.s = (dup.s + ev.s) / 2;
          if (ev.kind === 'end') dup.kind = 'end';
        } else {
          events.push({ ...ev });
        }
      }
    }
    junctions.push({ id: junctions.length, x, y, z, events });
  }

  // ---- cut splines into proto-arms
  const arms = [];
  const junctionOfEvent = new Map(); // `${spline}:${s}` -> junction id (approx)
  junctions.forEach((j) => {
    j.events.forEach((ev) => junctionOfEvent.set(`${ev.spline}:${ev.s.toFixed(3)}`, j.id));
  });

  const findJunctionAt = (splineId, s) => {
    for (const j of junctions) {
      for (const ev of j.events) {
        if (ev.spline === splineId && Math.abs(ev.s - s) < junctionMergeDist * 0.5) return j;
      }
    }
    return null;
  };

  for (let i = 0; i < dense.length; i++) {
    const Li = dense[i][dense[i].length - 1].s;
    const cuts = new Set([0, Li]);
    for (const j of junctions) {
      for (const ev of j.events) {
        if (ev.spline === i) cuts.add(Math.min(Math.max(ev.s, 0), Li));
      }
    }
    const sorted = [...cuts].sort((a, b) => a - b);
    for (let k = 0; k < sorted.length - 1; k++) {
      const s0 = sorted[k];
      const s1 = sorted[k + 1];
      const len = s1 - s0;
      const jStart = findJunctionAt(i, s0);
      const jEnd = findJunctionAt(i, s1);
      // a cut is a real junction only if some event matches it
      const fromJ = jStart && jStart.events.some((e) => e.spline === i && Math.abs(e.s - s0) < junctionMergeDist * 0.5) ? jStart : null;
      const toJ = jEnd && jEnd.events.some((e) => e.spline === i && Math.abs(e.s - s1) < junctionMergeDist * 0.5) ? jEnd : null;
      arms.push({
        id: arms.length,
        splineId: i,
        s0,
        s1,
        len,
        fromJ: fromJ ? fromJ.id : null,
        toJ: toJ ? toJ.id : null,
        keep: len >= minArmLen,
        stations: null,
        ends: [null, null],
      });
    }
  }

  // ---- attach arms to junctions, clamp mouth distances, build stations
  for (const j of junctions) {
    j.arms = [];
    for (const ev of j.events) {
      const touching = arms.filter(
        (a) => a.splineId === ev.spline && (a.fromJ === j.id || a.toJ === j.id) && a.keep
      );
      for (const arm of touching) {
        const atStart = arm.fromJ === j.id;
        const cutS = atStart ? arm.s0 : arm.s1;
        const maxD = Math.min(junctionDist, arm.len * 0.42);
        const d = Math.max(maxD, stationSpacing * 1.2);
        const mouthS = atStart ? cutS + d : cutS - d;
        const mouth = sampleAt(dense[arm.splineId], mouthS);
        const dir = norm(v3(mouth.pos.x - j.x, 0, mouth.pos.z - j.z));
        if (!isFinite(dir.x) || (dir.x === 0 && dir.z === 0)) dir.z = 1;
        j.arms.push({ armId: arm.id, mouthS, dir, mouthDist: d, atStart });
      }
    }
    if (j.arms.length < 2) j.dead = true;
  }

  const liveJunctions = junctions.filter((j) => !j.dead && j.arms.length >= 2);
  const liveIds = new Set(liveJunctions.map((j) => j.id));

  for (const arm of arms) {
    if (!arm.keep) continue;
    if (arm.fromJ !== null && !liveIds.has(arm.fromJ)) arm.fromJ = null;
    if (arm.toJ !== null && !liveIds.has(arm.toJ)) arm.toJ = null;

    // mouth stations (forced) or plain ends
    let a = arm.s0;
    let b = arm.s1;
    const jStart = liveJunctions.find((j) => j.id === arm.fromJ);
    const jEnd = liveJunctions.find((j) => j.id === arm.toJ);
    const mStart = jStart ? jStart.arms.find((m) => m.armId === arm.id && m.atStart) : null;
    const mEnd = jEnd ? jEnd.arms.find((m) => m.armId === arm.id && !m.atStart) : null;
    if (mStart) a = mStart.mouthS;
    if (mEnd) b = mEnd.mouthS;
    arm.ends[0] = mStart ? { junctionId: jStart.id, mouthS: a, dir: mStart.dir } : null;
    arm.ends[1] = mEnd ? { junctionId: jEnd.id, mouthS: b, dir: mEnd.dir } : null;

    // stations: uniform between a..b, exact endpoints forced
    const stations = [];
    const total = b - a;
    const n = Math.max(Math.round(total / stationSpacing), 1);
    for (let k = 0; k <= n; k++) {
      stations.push(sampleAt(dense[arm.splineId], a + (total * k) / n));
    }
    stations[0].s = a;
    stations[stations.length - 1].s = b;
    arm.stations = stations;
  }

  return {
    dense,
    arms: arms.filter((a) => a.keep),
    junctions: liveJunctions,
    overpasses,
  };
}

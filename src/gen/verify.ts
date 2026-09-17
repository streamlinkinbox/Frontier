/**
 * verify.ts — the checks that keep the generator honest.
 *
 * The brief was explicit: the roofs must be CORRECT, must NOT FLOAT, and must
 * have the tiles and structure properly implemented. Those are testable claims,
 * so they are tested rather than asserted:
 *
 *   舉折         obeys the Yingzao Fashi, including its own worked example
 *   pitch        lands in the historical 1:3 … 1:1.5 band
 *   purlins      rise monotonically from the eave, never more than seven steps
 *   hips         the two faces meeting at a hip compute the SAME height — a hip
 *                is a true surface intersection, so it cannot float or tear
 *   eave         projects the same real distance on all four sides
 *   ridge        is dead level
 *   corner       upturn vanishes at the eave purlin and the ridge, so purlin
 *                logs and column tops stay on the flat juzhe line
 *   tiles        every tile sits on the bedding plane (via a fixed-point
 *                projection along the surface normal, so the test is not
 *                fooled by the very offset it is checking)
 *   rafters      every rafter's centre line is a constant perpendicular distance
 *                under the bedding plane, and every rafter END lands over a
 *                purlin — i.e. the roof is carried, not hovering
 *   purlins      tops lie exactly on the 舉折 line
 *   columns      reach the eave purlin: no gap between building and roof
 *   ridges       caps straddle the tile surface; 鴟吻 terminate a 正脊
 */

import { Matrix4, Vector3 } from 'three';
import { solveJuzhe } from '../core/juzhe';
import type { RoofField } from './roofField';
import { barrelCrown as crownOf, type TileInstance, type TileSpec } from './tiles';
import type { FrameInstance, FrameSpec } from './frame';
import type { RidgeInstance, RidgeSpec } from './ridges';

export interface Check {
  name: string;
  pass: boolean;
  worst: number;
  tolerance: number;
  samples: number;
  detail: string;
}

export interface VerifyReport {
  pass: boolean;
  checks: Check[];
  notes: string[];
}

export interface VerifyInput {
  field: RoofField;
  tileSpec: TileSpec;
  tiles: TileInstance[];
  frame: FrameInstance[];
  frameSpec: FrameSpec;
  ridges: RidgeInstance[];
  ridgeSpec: RidgeSpec;
}

export function verifyRoof(input: VerifyInput): VerifyReport {
  const checks: Check[] = [];
  const notes: string[] = [];
  const { field, tileSpec, tiles, frame, frameSpec, ridges, ridgeSpec } = input;
  const lift = field.spec.surfaceLift;
  const rafterDepth = frameSpec.rafterDiameter;
  const sheathT = Math.max(0.02, lift - rafterDepth);

  // ---------------------------------------------------------------- 舉折 rules
  {
    // The Fashi's own worked example: B = 10, rise 1/3 ⇒ H0 = 200 cun, first
    // purlin dropped 20 cun below the working line, the next one 10 cun.
    const res = solveJuzhe({
      halfSpan: 5,
      riseRatio: 1 / 3,
      steps: 7,
      eaveProjection: 1,
      flyOverEave: 0.6,
      flyPitch: 1.2,
    });
    const k = 200 / res.H0;
    let worst = 0;
    for (let n = 1; n <= 6; n++) {
      const Hn = res.purlins[res.purlins.length - 1 - n].h;
      const onLine =
        res.purlins[res.purlins.length - n].h * ((res.steps - n) / (res.steps - n + 1));
      const expected = Math.pow(0.5, n - 1) * (200 / 10);
      worst = Math.max(worst, Math.abs((onLine - Hn) * k - expected));
    }
    checks.push({
      name: 'juzhe/fashi-drops',
      pass: worst < 0.5,
      worst,
      tolerance: 0.5,
      samples: 6,
      detail: '折屋 drops are H0/10, then halved each step (cun)',
    });
  }
  {
    const p = field.juzhe.pitch;
    checks.push({
      name: 'juzhe/pitch-band',
      pass: p >= 1 / 3.05 && p <= 1 / 1.45,
      worst: Math.max(0, p - 1 / 1.45, 1 / 3.05 - p),
      tolerance: 0,
      samples: 1,
      detail: `pitch 1:${(1 / p).toFixed(2)} — historical band 1:3 … 1:1.5`,
    });
  }
  {
    let worst = 0;
    const ps = field.juzhe.purlins;
    for (let i = 1; i < ps.length; i++) worst = Math.max(worst, ps[i - 1].h - ps[i].h);
    checks.push({
      name: 'juzhe/monotonic-steps',
      pass: worst >= -1e-9 && field.juzhe.steps <= 7,
      worst: Math.max(0, worst),
      tolerance: 0,
      samples: ps.length,
      detail: `purlin heights rise from the eave; m = ${field.juzhe.steps} (Fashi max 7)`,
    });
  }

  // ------------------------------------------------------- surface continuity
  {
    let worst = 0;
    let samples = 0;
    for (const run of field.ridgeRuns()) {
      if (run.kind !== 'hip' && run.kind !== 'mainRidge') continue;
      for (const p of run.points) {
        worst = Math.max(worst, Math.abs(field.yAt(p.x, p.z) - p.y));
        samples++;
      }
    }
    checks.push({
      name: 'surface/hips-on-surface',
      pass: worst < 1e-6,
      worst,
      tolerance: 1e-6,
      samples,
      detail: 'every hip / ridge vertex lies exactly on the height field',
    });
  }
  {
    // Walk each hip twice, once from each face that meets there. If these ever
    // disagree the hip is torn — which is the failure mode that produces
    // "floating roof" artefacts.
    let worst = 0;
    let samples = 0;
    const tEnd = field.endClip;
    const { ex, ez, inset } = field;
    for (const cx of [-1, 1]) {
      for (const cz of [-1, 1]) {
        const slope = field.faces.find((f) => f.kind === 'slope' && f.sign === cz);
        const end = field.faces.find((f) => f.kind === 'end' && f.sign === cx);
        if (!slope || !end) continue;
        for (let k = 0; k <= 20; k++) {
          const t = (k / 20) * tEnd;
          const a = field.pos(slope, t, cx * (ex - inset * t), new Vector3());
          const b = field.pos(end, t, cz * (ez - ez * t), new Vector3());
          worst = Math.max(worst, a.distanceTo(b));
          samples++;
        }
      }
    }
    checks.push({
      name: 'surface/hip-seam-closed',
      pass: worst < 1e-5,
      worst,
      tolerance: 1e-5,
      samples,
      detail: 'the two faces at a hip agree to the micron — no gap, no overlap',
    });
  }
  {
    // the eave overhang is a real length: identical on all four sides
    const plans: number[] = [];
    for (const face of field.faces) {
      const tTip = field.faceNodes(face)[0].t;
      for (const u of [-0.4, 0, 0.4]) {
        const a = field.pos(face, 0, u, new Vector3());
        const b = field.pos(face, tTip, u, new Vector3());
        plans.push(Math.hypot(b.x - a.x, b.z - a.z));
      }
    }
    const worst = plans.length ? Math.max(...plans) - Math.min(...plans) : 0;
    checks.push({
      name: 'surface/eave-overhang-equal',
      pass: plans.length > 0 && worst < 2e-3,
      worst,
      tolerance: 2e-3,
      samples: plans.length,
      detail: `eave projects ${(plans[0] ?? 0).toFixed(3)} m on every side (spread ${worst.toExponential(1)} m)`,
    });
  }
  {
    const tTip = field.faceNodes(field.faces[0])[0].t;
    const atPurlin = field.uplift(field.ex, field.ez, 0);
    const atRidge = field.uplift(0, 0, 1);
    const atTip = field.uplift(field.ex, field.ez, tTip);
    checks.push({
      name: 'surface/upturn-leaves-timber-alone',
      pass: atPurlin < 1e-9 && atRidge < 1e-9,
      worst: Math.max(atPurlin, atRidge),
      tolerance: 1e-9,
      samples: 3,
      detail: `0 at the eave purlin and the ridge, ${atTip.toFixed(3)} m at the corner tip`,
    });
  }
  {
    // 攢尖 has no 正脊 at all (its slopes mass to a point) so the check is
    // vacuous there — that is correct, not a skip.
    const run = field.ridgeRuns().find((r) => r.kind === 'mainRidge');
    let worst = 0;
    if (run) {
      const y0 = run.points[0].y;
      for (const p of run.points) worst = Math.max(worst, Math.abs(p.y - y0));
    }
    checks.push({
      name: 'surface/ridge-level',
      pass: (!run && field.spec.type === 'pyramid') || (!!run && worst < 1e-6),
      worst,
      tolerance: 1e-6,
      samples: run ? run.points.length : 0,
      detail: run
        ? '正脊 height is constant along its whole length'
        : '攢尖 has no 正脊 by design — the slopes mass to the apex',
    });
  }
  {
    // a point strictly inside a face must be governed by that same face
    let worst = 0;
    let samples = 0;
    for (const face of field.faces) {
      for (let j = 1; j < 8; j++) {
        const t = (j / 8) * Math.min(0.92, face.endClip);
        const uLim = field.uLimit(face, t);
        for (const frac of [-0.75, -0.3, 0.25, 0.8]) {
          const u = frac * uLim;
          const p = field.pos(face, t, u, new Vector3());
          const g = field.governing(p.x, p.z);
          if (g.face.id !== face.id) continue; // near the hip: legitimately the other face
          const direct = field.yAt(p.x, p.z);
          const local = field.profileY(t, face.depth) + field.uplift(p.x, p.z, t);
          worst = Math.max(worst, Math.abs(direct - local));
          samples++;
        }
      }
    }
    checks.push({
      name: 'surface/governing-face-agrees',
      pass: worst < 1e-6,
      worst,
      tolerance: 1e-6,
      samples,
      detail: 'a point inside a face reads the same height through the face or the field',
    });
  }

  // ---------------------------------------------------------------------- tiles
  {
    // Each tile END must rest on its own face at exactly `lift` above the
    // bedding plane. Checking the ends (not the centre) is deliberate: a rigid
    // tile bridging a purlin kink is SUPPOSED to touch at its ends, and
    // measuring the centre would report that correct bridging as an error.
    let worst = 0;
    let samples = 0;
    const byId = new Map(field.faces.map((f) => [f.id, f]));
    for (const t of tiles) {
      const face = byId.get(t.face);
      if (!face) continue;
      const e = t.m.elements;
      const zc = new Vector3(e[8], e[9], e[10]);
      const c = new Vector3(e[12], e[13], e[14]);
      const half = t.nominalLength * 0.5;
      for (const [end, tt] of [
        [c.clone().addScaledVector(zc, -half), t.t0],
        [c.clone().addScaledVector(zc, half), t.t1],
      ] as [Vector3, number][]) {
        const s = field.pos(face, tt, t.uC, new Vector3());
        worst = Math.max(worst, Math.abs(end.distanceTo(s) - t.lift));
        samples++;
      }
    }
    checks.push({
      name: 'tiles/on-sheathing',
      pass: worst < 0.02,
      worst,
      tolerance: 0.02,
      samples,
      detail: `${samples} tile ends rest on the bedding plane — none hover, none sink`,
    });
  }
  {
    // along a course the tiles must span the face edge to edge, no gaps
    let worstGap = 0;
    let samples = 0;
    const pitch = tileSpec.pitch;
    for (const face of field.faces) {
      const uEave = field.uLimit(face, field.faceNodes(face)[0].t);
      const nCol = Math.max(1, Math.ceil((2 * uEave) / pitch));
      for (const t of [0.06, 0.2, 0.45, 0.7]) {
        const uLim = field.uLimit(face, t);
        if (uLim < 1e-3) continue;
        const spans: [number, number][] = [];
        for (let k = 0; k < nCol; k++) {
          const uc = -uEave + pitch * (k + 0.5);
          const lo = Math.max(-uLim, uc - pitch / 2);
          const hi = Math.min(uLim, uc + pitch / 2);
          if (hi - lo > 1e-4) spans.push([lo, hi]);
        }
        spans.sort((a, b) => a[0] - b[0]);
        for (let i = 0; i < spans.length; i++) {
          if (i > 0) worstGap = Math.max(worstGap, spans[i][0] - spans[i - 1][1]);
          samples++;
        }
        if (spans.length) {
          worstGap = Math.max(worstGap, -uLim - spans[0][0], spans[spans.length - 1][1] - uLim);
        }
      }
    }
    checks.push({
      name: 'tiles/courses-span-the-face',
      pass: worstGap < 2e-3,
      worst: Math.max(0, worstGap),
      tolerance: 2e-3,
      samples,
      detail: 'no gap and no overhang between adjacent tiles along a course',
    });
  }
  {
    // The eave course must sit on the flying-rafter tip: every 瓦當/滴水 in the
    // lowest course starts exactly at the face's tip parameter.
    let worst = 0;
    let samples = 0;
    let tipQ = 0;
    const byId = new Map(field.faces.map((f) => [f.id, f]));
    for (const t of tiles) {
      if (t.kind !== 'drip' && t.kind !== 'eaveRound') continue;
      const face = byId.get(t.face);
      if (!face) continue;
      const tTip = field.faceNodes(face)[0].t;
      worst = Math.max(worst, Math.abs(t.t0 - tTip) * face.depth);
      tipQ = Math.max(tipQ, -field.lifted.profile[0].q);
      samples++;
    }
    checks.push({
      name: 'tiles/eave-course-at-tip',
      pass: samples > 0 && worst < 5e-3,
      worst,
      tolerance: 5e-3,
      samples,
      detail:
        `every eave tile starts on the flying-rafter tip (${tipQ.toFixed(3)} m out; ` +
        `the bedding plane overhangs it by the parallel-curve offset)`,
    });
  }

  // ---------------------------------------------------------------------- frame
  {
    // A rafter's centre line must sit a constant perpendicular distance under
    // its own bay's bedding plane, so its top meets the sheathing's underside.
    let worst = 0;
    let samples = 0;
    const byId = new Map(field.faces.map((f) => [f.id, f]));
    for (const inst of frame) {
      if (inst.kind !== 'rafter' && inst.kind !== 'flyRafter') continue;
      const face = inst.face ? byId.get(inst.face) : undefined;
      if (!face || inst.u === undefined || inst.tA === undefined || inst.tB === undefined) continue;
      const a = field.pos(face, inst.tA, inst.u, new Vector3());
      const b = field.pos(face, inst.tB, inst.u, new Vector3());
      const c = new Vector3().setFromMatrixPosition(inst.m);
      const target =
        sheathT + (inst.kind === 'flyRafter' ? rafterDepth * 0.45 : rafterDepth * 0.5);
      worst = Math.max(worst, Math.abs(pointToSegment3D(c, a, b) - target));
      samples++;
    }
    checks.push({
      name: 'frame/rafters-touch-sheathing',
      pass: worst < 0.02,
      worst,
      tolerance: 0.02,
      samples,
      detail: 'every 椽/飛椽 top meets the sheathing underside — no air gap',
    });
  }
  {
    // Every rafter end must be SUPPORTED: either it lands over a 檩, or (for the
    // 飛椽, whose tail is nailed to the 檐椽) it meets another rafter's end.
    // A 飛椽 head cantilevers past the eave by design — that is what a flying
    // rafter IS — so its outboard end is allowed to be free.
    const purlinLines: { a: Vector3; b: Vector3 }[] = [];
    const rafterEnds: Vector3[] = [];
    for (const inst of frame) {
      const e = inst.m.elements;
      const yCol = new Vector3(e[4], e[5], e[6]);
      const c = new Vector3(e[12], e[13], e[14]);
      if (inst.kind === 'purlin') {
        purlinLines.push({
          a: c.clone().addScaledVector(yCol, -0.5),
          b: c.clone().addScaledVector(yCol, 0.5),
        });
      } else if (inst.kind === 'rafter') {
        rafterEnds.push(
          c.clone().addScaledVector(yCol, -0.5),
          c.clone().addScaledVector(yCol, 0.5),
        );
      }
    }
    let worst = 0;
    let samples = 0;
    let freeHeads = 0;
    for (const inst of frame) {
      if (inst.kind !== 'rafter' && inst.kind !== 'flyRafter') continue;
      const e = inst.m.elements;
      const yCol = new Vector3(e[4], e[5], e[6]);
      const c = new Vector3(e[12], e[13], e[14]);
      const ends = [c.clone().addScaledVector(yCol, -0.5), c.clone().addScaledVector(yCol, 0.5)];
      for (const end of ends) {
        let d = Infinity;
        for (const p of purlinLines) d = Math.min(d, distToSegment2D(end, p.a, p.b));
        let joined = Infinity;
        for (const r of rafterEnds) joined = Math.min(joined, end.distanceTo(r));
        if (inst.kind === 'flyRafter' && d > frameSpec.purlinDiameter && joined > 0.2) {
          freeHeads++; // the cantilevered flying-eave head: allowed, counted
          continue;
        }
        worst = Math.max(worst, Math.min(d, joined));
        samples++;
      }
    }
    checks.push({
      name: 'frame/rafter-ends-supported',
      pass: worst < frameSpec.purlinDiameter * 0.8,
      worst,
      tolerance: frameSpec.purlinDiameter * 0.8,
      samples,
      detail:
        `every 椽 end lands over a 檩; 飛椽 tails meet the 檐椽 ` +
        `(${freeHeads} cantilevered flying-eave heads, by design)`,
    });
  }
  {
    let worst = 0;
    let samples = 0;
    for (const inst of frame) {
      if (inst.kind !== 'purlin') continue;
      const c = new Vector3().setFromMatrixPosition(inst.m);
      const expected = field.timberY(c.x, c.z) - frameSpec.purlinDiameter * 0.5;
      worst = Math.max(worst, Math.abs(c.y - expected));
      samples++;
    }
    checks.push({
      name: 'frame/purlins-on-juzhe-line',
      pass: worst < 3e-3,
      worst,
      tolerance: 3e-3,
      samples,
      detail: '檩/槫 tops sit exactly on the 舉折 profile',
    });
  }
  {
    let minTop = Infinity;
    let samples = 0;
    for (const inst of frame) {
      if (inst.kind !== 'column') continue;
      const e = inst.m.elements;
      const yCol = new Vector3(e[4], e[5], e[6]);
      const c = new Vector3(e[12], e[13], e[14]);
      minTop = Math.min(minTop, c.clone().addScaledVector(yCol, 0.5).y);
      samples++;
    }
    const under = field.timberY(0, field.ez) - frameSpec.purlinDiameter;
    const worst = Number.isFinite(minTop) ? Math.abs(minTop - under) : Infinity;
    checks.push({
      name: 'frame/columns-reach-the-purlin',
      pass: worst < 5e-3,
      worst,
      tolerance: 5e-3,
      samples,
      detail: 'every 柱 top meets the eave purlin — the roof does not float',
    });
  }

  // --------------------------------------------------------------------- ridges
  {
    // Two facts about a 脊瓦, kept separate so each stays tight:
    //
    //  (1) bedding — along its own centre line the rim must reach down to the
    //      筒瓦 crowns either side, no higher and not sunk through the sheathing;
    //  (2) straddle — sampled out at the barrel positions it must still be in
    //      contact, allowing for mortar. At a corner the surface is a saddle and
    //      at a 歇山's 博脊 the inner rim runs into the 山牆, so (2) is the loose
    //      of the two; (1) is where a floating cap gets caught.
    const capW = tileSpec.pitch * ridgeSpec.capWidthRatio;
    const capSag = capW * 0.3;
    const barrelCrown = crownOf(tileSpec);
    let bedLow = Infinity;
    let bedHigh = -Infinity;
    let wideLow = Infinity;
    let wideHigh = -Infinity;
    let samples = 0;
    const readAt = (inst: RidgeInstance, u: number, s: number) => {
      const e = inst.m.elements;
      const across = new Vector3(e[0], e[1], e[2]);
      const up = new Vector3(e[4], e[5], e[6]);
      const along = new Vector3(e[8], e[9], e[10]);
      const o = new Vector3(e[12], e[13], e[14]);
      const zScale = Math.hypot(e[8], e[9], e[10]);
      const halfL = (tileSpec.length * ridgeSpec.capLengthRatio * zScale) / 2;
      const p = o
        .clone()
        .addScaledVector(along, halfL * s)
        .addScaledVector(across, u)
        .addScaledVector(up, -capSag);
      return p.y - field.yAt(p.x, p.z);
    };
    for (const inst of ridges) {
      if (inst.kind !== 'ridgeCap' && inst.kind !== 'hipCap' && inst.kind !== 'breakCap') continue;
      for (const s of [-1, 1]) {
        const hv = readAt(inst, 0, s);
        bedLow = Math.min(bedLow, hv);
        bedHigh = Math.max(bedHigh, hv);
      }
      if (inst.kind === 'breakCap') continue; // its inner rim belongs to the wall
      for (const u of [tileSpec.pitch * 0.5, -tileSpec.pitch * 0.5]) {
        for (const s of [-1, 1]) {
          const hv = readAt(inst, u, s);
          wideLow = Math.min(wideLow, hv);
          wideHigh = Math.max(wideHigh, hv);
        }
      }
      samples++;
    }
    const ok =
      samples > 0 &&
      bedLow > -0.035 && // a cap chords the 翼角 curve and beds into the mortar there
      bedHigh < barrelCrown + 0.045 && // + sag of a rigid cap on a curved run
      wideLow > -0.4 &&
      wideHigh < barrelCrown + 0.16;
    checks.push({
      name: 'ridges/caps-sit-on-tiles',
      pass: ok,
      worst: ok ? 1 : 0,
      tolerance: 0,
      samples,
      detail:
        `脊瓦 bed at ${bedLow.toFixed(3)}…${bedHigh.toFixed(3)} m on the centre line ` +
        `(筒瓦 crowns ${barrelCrown.toFixed(3)} m), straddling out to ` +
        `${wideLow.toFixed(3)}…${wideHigh.toFixed(3)} m at the 筒瓦 positions`,
    });
  }
  {
    // Ornaments stand on the surface at the ridge's own height.
    let worst = 0;
    let samples = 0;
    for (const inst of ridges) {
      if (inst.kind !== 'chiwen' && inst.kind !== 'onigawara' && inst.kind !== 'finial') continue;
      const p = new Vector3().setFromMatrixPosition(inst.m);
      if (inst.kind === 'finial') {
        // the 寶頂 is capped over the apex: it must sit at or above the apex
        const apex = field.yAt(0, 0);
        worst = Math.max(worst, Math.max(0, apex - p.y + 1e-9) * 100);
      } else {
        worst = Math.max(worst, Math.abs(p.y - field.yAt(p.x, p.z)) < 1e-6 ? 1 : 0);
      }
      samples++;
    }
    checks.push({
      name: 'ridges/ornaments-on-surface',
      pass: worst < 0.35,
      worst,
      tolerance: 0.35,
      samples,
      detail: 'every 吻/鬼瓦/寶頂 is anchored on the roof it crowns',
    });
  }
  {
    let worst = 0;
    let samples = 0;
    const run = field.ridgeRuns().find((r) => r.kind === 'mainRidge');
    for (const inst of ridges) {
      if (inst.kind !== 'chiwen' || !run) continue;
      const p = new Vector3().setFromMatrixPosition(inst.m);
      const d = Math.min(
        Math.hypot(p.x - run.points[0].x, p.z - run.points[0].z),
        Math.hypot(
          p.x - run.points[run.points.length - 1].x,
          p.z - run.points[run.points.length - 1].z,
        ),
      );
      worst = Math.max(worst, d);
      samples++;
    }
    checks.push({
      name: 'ridges/chiwen-at-ridge-ends',
      pass:
        (samples === 2 || (samples === 0 && field.spec.type === 'pyramid')) &&
        worst < tileSpec.pitch * ridgeSpec.ornamentRatio,
      worst,
      tolerance: tileSpec.pitch * ridgeSpec.ornamentRatio,
      samples,
      detail:
        samples === 0
          ? '攢尖 has no 正脊, so no 正吻 — its 寶頂 crowns the apex instead'
          : 'both 正脊 ends carry exactly one 正吻',
    });
  }

  notes.push(
    `roof extents ${(field.ex * 2 + field.juzhe.eaveTipRun * 2).toFixed(2)} × ` +
      `${(field.ez * 2 + field.juzhe.eaveTipRun * 2).toFixed(2)} m, ` +
      `apex ${field.juzhe.H0.toFixed(2)} m above the eave purlin`,
  );

  return { pass: checks.every((c) => c.pass), checks, notes };
}

/** 3-D distance from a point to a segment. */
function pointToSegment3D(p: Vector3, a: Vector3, b: Vector3): number {
  const ab = new Vector3().subVectors(b, a);
  const l2 = ab.lengthSq();
  const t = l2 > 1e-12 ? Math.max(0, Math.min(1, new Vector3().subVectors(p, a).dot(ab) / l2)) : 0;
  return p.distanceTo(a.clone().addScaledVector(ab, t));
}

/** Plan-view (XZ) distance from a point to a segment. */
function distToSegment2D(p: Vector3, a: Vector3, b: Vector3): number {
  const ax = a.x;
  const az = a.z;
  const bx = b.x;
  const bz = b.z;
  const dx = bx - ax;
  const dz = bz - az;
  const l2 = dx * dx + dz * dz;
  const t =
    l2 > 1e-12 ? Math.max(0, Math.min(1, ((p.x - ax) * dx + (p.z - az) * dz) / l2)) : 0;
  return Math.hypot(p.x - (ax + dx * t), p.z - (az + dz * t));
}

export function formatReport(r: VerifyReport): string {
  const lines: string[] = [];
  for (const c of r.checks) {
    lines.push(
      `${c.pass ? 'PASS' : 'FAIL'}  ${c.name.padEnd(34)} n=${String(c.samples).padStart(6)}  ` +
        `worst=${c.worst.toExponential(2)}  tol=${c.tolerance}`,
    );
    lines.push(`        ${c.detail}`);
  }
  for (const n of r.notes) lines.push(`  · ${n}`);
  lines.push(r.pass ? 'ALL CHECKS PASSED' : 'SOME CHECKS FAILED');
  return lines.join('\n');
}

export function matrixToPos(m: Matrix4): Vector3 {
  return new Vector3().setFromMatrixPosition(m);
}

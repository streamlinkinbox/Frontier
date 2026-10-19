import * as THREE from "three";
import {
  shoulderProfile,
  cornerLayout,
  cornerKey,
  mouthKey,
  terminationFraction,
} from "./edge-transitions.js";
import { CHANNELS, GUTTER_CUTS } from "./drainage.js";
import {
  channelWidth,
  isWaterChannel,
  curbMaterial,
  collectorStations,
  emitGutterStrip,
  emitGutterCorner,
} from "./drainage.js";
import { ribbonUVRows, objTextureCoordinates } from "./surface-uv.js";
import { auditPavementPanels, planShape } from "./pavement-audit.js";
import { QuadMesh, roadY } from "./geometry.js";

export const STREET_DEFAULTS = {
  curbStyle: "granite",
  curbHeight: 0.15,
  rampStyle: "flared",
  paving: "slab",
  tactile: "yellow",
  drainage: "inlet",
};
export const STREET_PRESETS = [
  {
    id: "brick-channel",
    name: "Brick roll + collector",
    note: "Radial brick courses · dished bed · six open grille slots",
    curbStyle: "brick-roll",
    curbHeight: 0.12,
    rampStyle: "flared",
    paving: "slab",
    tactile: "stone",
    drainage: "brick-channel",
  },
  {
    id: "weep-channel",
    name: "Broomed spoon + weeps",
    note: "Rounded concrete toe · depressed channel · four open weep holes",
    curbStyle: "spoon",
    curbHeight: 0.12,
    rampStyle: "flared",
    paving: "slab",
    tactile: "stone",
    drainage: "weep-channel",
  },
  {
    id: "slot-channel",
    name: "Dished curb + slot drain",
    note: "Deep concrete channel · continuous narrow collector slot",
    curbStyle: "dished",
    curbHeight: 0.15,
    rampStyle: "flared",
    paving: "slab",
    tactile: "yellow",
    drainage: "slot-channel",
  },
  {
    id: "granite-channel",
    name: "Granite + linear drain",
    note: "Segmented stone curb · open-bar channel",
    curbStyle: "granite",
    rampStyle: "flared",
    paving: "slab",
    tactile: "stone",
    drainage: "channel",
  },
  {
    id: "tactile-drop",
    name: "Flared tactile crossing",
    note: "Dropped lip · stone warning tiles",
    curbStyle: "concrete",
    rampStyle: "flared",
    paving: "slab",
    tactile: "stone",
    drainage: "none",
  },
  {
    id: "storm-inlet",
    name: "Stormwater curb inlet",
    note: "Open throat · concrete gutter · access cover",
    curbStyle: "concrete",
    rampStyle: "flared",
    paving: "slab",
    tactile: "yellow",
    drainage: "inlet",
  },
  {
    id: "blended-corner",
    name: "Blended corner",
    note: "Lowered curved edge · yellow tactile paving",
    curbStyle: "concrete",
    rampStyle: "blended",
    paving: "brick",
    tactile: "yellow",
    drainage: "none",
  },
  {
    id: "brick-drop",
    name: "Brick drop crossing",
    note: "Rolled curb returns · continuous brick ramp",
    curbStyle: "rolled",
    rampStyle: "flared",
    paving: "brick",
    tactile: "none",
    drainage: "none",
  },
  {
    id: "grooved-drop",
    name: "Grooved concrete ramp",
    note: "Recessed grooves · red tactile warning strip",
    curbStyle: "concrete",
    rampStyle: "grooved",
    paving: "slab",
    tactile: "red",
    drainage: "inlet",
  },
];
export const streetSettings = (e) =>
  Object.fromEntries(
    Object.entries(STREET_DEFAULTS).map(([k, v]) => [k, e[k] ?? v]),
  );
export const gutterWidth = channelWidth;
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const unique = (values) =>
  [...new Set(values.map((v) => Math.round(v * 1e7) / 1e7))].sort(
    (a, b) => a - b,
  );
const interpolate = (a, b, t) => a + (b - a) * t;
function face(mesh, points, mat, reverse = false, uvs = null) {
  const ids = points.map((p) => mesh.vertex(p));
  if (reverse) {
    ids.reverse();
    if (uvs) uvs = [...uvs].reverse();
  }
  mesh.faces.push(ids);
  mesh.groups.push(mat);
  mesh.uvs.push(uvs);
}
function gridFace(mesh, fn, s0, s1, u0, u1, mat, side) {
  face(mesh, [fn(s0, u0), fn(s1, u0), fn(s1, u1), fn(s0, u1)], mat, side > 0);
}
export function rampCenters(r, junctions) {
  if (r.e.sidewalk < 1.2 || streetSettings(r.e).rampStyle === "none") return [];
  const min = r.from * r.length,
    max = r.to * r.length,
    centers = [];
  for (const [id, s] of [
    [r.e.a, min + 3],
    [r.e.b, max - 3],
  ])
    if (
      junctions.some((j) => j.id === id && j.degree >= 3) &&
      s - 2.2 > min + 0.02 &&
      s + 2.2 < max - 0.02
    )
      centers.push(s);
  return centers.filter((s, i) => !i || s - centers[i - 1] > 4.4);
}
export function rampHeight(e, s, u, centers, baseHeight) {
  const cfg = streetSettings(e),
    g = gutterWidth(e),
    run = Math.min(e.sidewalk - 0.3, (cfg.curbHeight - 0.006) * 12);
  let factor = 0;
  const half = cfg.rampStyle === "blended" ? 1.5 : 1.2;
  for (const c of centers)
    factor = Math.max(factor, clamp((half + 1 - Math.abs(s - c)) / 1));
  let h = interpolate(
    baseHeight,
    0.006 + (cfg.curbHeight - 0.006) * clamp((u - g) / Math.max(0.1, run)),
    factor,
  );
  if (
    cfg.rampStyle === "grooved" &&
    factor === 1 &&
    u > g + 0.32 &&
    u < g + run &&
    Math.min(...centers.map((c) => Math.abs(s - c))) > 0.01
  ) {
    const phase = (((s - centers[0]) % 0.12) + 0.12) % 0.12,
      depth = clamp((0.027 - Math.min(phase, 0.12 - phase)) / 0.015);
    h -= 0.003 * depth;
  }
  return h;
}
function section(e, g) {
  const c = streetSettings(e),
    h = c.curbHeight,
    w = e.sidewalk,
    scale = Math.min(1, w / 0.6),
    edge = 0.28 * scale;
  const tactileEnd = Math.min(0.95, w - 0.14 * scale),
    runEnd = Math.min(
      Math.max(tactileEnd + 0.06 * scale, Math.min(w - 0.3, (h - 0.006) * 12)),
      w - 0.07 * scale,
    );
  const rolled =
    ["rolled", "brick-roll", "spoon", "dished"].includes(c.curbStyle) &&
    c.drainage !== "inlet";
  const toe =
    c.curbStyle === "spoon"
      ? [0.065, 0.12, 0.18, 0.65]
      : c.curbStyle === "dished"
        ? [0.08, 0.45, 0.2, 0.95]
        : [0.04, 0.38, 0.12, 0.8];
  return [
    [0, 0],
    [g, 0],
    [g + (rolled ? toe[0] * scale : 0), rolled ? h * toe[1] : h - 0.035],
    [g + (rolled ? toe[2] : 0.045) * scale, rolled ? h * toe[3] : h],
    [g + edge, h],
    [g + 0.32 * scale, h],
    [g + 0.35 * scale, h],
    [g + tactileEnd, h],
    [g + runEnd, h],
    [g + w, h],
  ];
}
function materialAt(e, s, u, centers) {
  const c = streetSettings(e),
    g = gutterWidth(e);
  if (u < g) return "gutter";
  if (u < g + 0.32 && ((s % 1) + 1) % 1 < 0.0121) return "joint";
  if (u < g + 0.32) return curbMaterial(c.curbStyle);
  if (
    c.tactile !== "none" &&
    u >= g + 0.35 &&
    u <= g + 0.95 &&
    centers.some(
      (center) =>
        Math.abs(s - center) < (c.rampStyle === "blended" ? 1.5 : 1.2),
    )
  )
    return `tactile_${c.tactile}`;
  return c.paving === "brick" ? "brick" : "paving";
}

/** Continuous cross-section ribbons: the ramp REPLACES the raised curb/pavement. */
export function generateStreetscape(p, result) {
  const detailedGutters = p.edges.some(isWaterChannel);
  const expectedMouths = new Set(
    result.roads.flatMap((r) =>
      [r.e.a, r.e.b].flatMap((n) =>
        [-1, 1].map((side) => mouthKey(r.e.id, n, side)),
      ),
    ),
  );
  const layout = cornerLayout(result),
    roadMouths = new Map(),
    connections = [],
    transitions = [],
    completedCorners = [];
  const mesh = new QuadMesh(),
    ramps = [],
    panels = [],
    drains = [],
    instances = { yellow: [], stone: [], red: [] };
  for (const r of result.roads) {
    const e = r.e;
    if (e.sidewalk <= 0) {
      const profile = shoulderProfile(),
        min = r.from * r.length,
        max = r.to * r.length,
        steps = Math.max(1, Math.ceil((max - min) / 2));
      for (const side of [-1, 1]) {
        const rows = Array.from({ length: steps + 1 }, (_, i) => {
          const t = r.from + ((r.to - r.from) * i) / steps;
          return profile.map(([u, h]) => [
            r.a.x + r.dx * t * r.length + r.nx * side * (r.w / 2 + u),
            roadY(p, e, t) + h,
            r.a.z + r.dz * t * r.length + r.nz * side * (r.w / 2 + u),
          ]);
        });
        const uv = ribbonUVRows(rows, { reverse: side > 0 });
        roadMouths.set(
          mouthKey(e.id, e.a, side),
          rows[0].map((v) => mesh.vertex(v)),
        );
        roadMouths.set(
          mouthKey(e.id, e.b, side),
          rows.at(-1).map((v) => mesh.vertex(v)),
        );
        for (let i = 1; i < rows.length; i++) {
          for (let k = 1; k < profile.length; k++) {
            const cuts = k === 1 && detailedGutters ? GUTTER_CUTS : [0, 1],
              point = (r, f) =>
                rows[r][k - 1].map((v, d) => interpolate(v, rows[r][k][d], f)),
              tex = (r, f) =>
                uv[r][k - 1].map((v, d) => interpolate(v, uv[r][k][d], f));
            for (let q = 1; q < cuts.length; q++) {
              const a = cuts[q - 1],
                b = cuts[q];
              face(
                mesh,
                [point(i - 1, a), point(i, a), point(i, b), point(i - 1, b)],
                e.bridgeNetwork ? "broomconcrete" : "shoulder",
                side > 0,
                [tex(i - 1, a), tex(i, a), tex(i, b), tex(i - 1, b)],
              );
            }
          }
          const points = [
            rows[i - 1][0],
            rows[i][0],
            rows[i].at(-1),
            rows[i - 1].at(-1),
          ];
          if (side > 0) points.reverse();
          panels.push({
            points,
            owner: `shoulder:${e.id}:${side}`,
            bridge: e.structure === "bridge",
          });
        }
      }
      continue;
    }
    const cfg = streetSettings(e),
      g = gutterWidth(e),
      profile = section(e, g),
      min = r.from * r.length,
      max = r.to * r.length,
      centers = rampCenters(r, result.junctions);
    const drainLength =
        cfg.drainage === "channel" ? Math.min(6, (max - min) * 0.32) : 1.8,
      drainCenter = (min + max) / 2;
    const hasDrain =
      e.structure !== "bridge" &&
      cfg.drainage !== "none" &&
      max - min > 13 &&
      centers.every((c) => Math.abs(c - drainCenter) > drainLength / 2 + 3);
    const drain = hasDrain
      ? {
          start: drainCenter - drainLength / 2,
          end: drainCenter + drainLength / 2,
        }
      : null;
    const stations = [min, max];
    for (let s = Math.ceil(min); s < max; s += 1) {
      stations.push(s);
      if (s + 0.012 < max) stations.push(s + 0.012);
    }
    for (const c of centers) {
      const half = cfg.rampStyle === "blended" ? 1.5 : 1.2;
      stations.push(c - half - 1, c - half, c, c + half, c + half + 1);
      for (let s = c - half + 0.3; s < c + half; s += 0.3) stations.push(s);
      if (cfg.rampStyle === "grooved")
        for (let s = c - 1.08; s < c + 1.2; s += 0.12)
          stations.push(s - 0.027, s - 0.012, s + 0.012, s + 0.027);
    }
    if (drain)
      stations.push(drain.start, drain.end, ...collectorStations(e, drain));
    const samples = unique(stations.filter((s) => s >= min && s <= max));
    for (const side of [-1, 1]) {
      const pos = (s, u, h = 0) => [
        r.a.x + r.dx * s + r.nx * side * (r.w / 2 + u),
        roadY(p, e, s / r.length) + h,
        r.a.z + r.dz * s + r.nz * side * (r.w / 2 + u),
      ];
      const surface = (s, u, base = cfg.curbHeight) =>
        pos(s, u, rampHeight(e, s, u, centers, base));
      const stripRows = samples.map((s) =>
        profile.map(([u, h]) => (h === 0 ? pos(s, u, 0) : surface(s, u, h))),
      );
      const stripUV = ribbonUVRows(stripRows, {
        material: cfg.paving === "brick" ? "brick" : "paving",
        reverse: side > 0,
      });
      const openings = detailedGutters
        ? emitGutterStrip(mesh, pos, samples, stripUV, e, drain, side)
        : [];
      roadMouths.set(
        mouthKey(e.id, e.a, side),
        stripRows[0].map((v) => mesh.vertex(v)),
      );
      roadMouths.set(
        mouthKey(e.id, e.b, side),
        stripRows.at(-1).map((v) => mesh.vertex(v)),
      );
      const sourceStart = layout.mouths.get(mouthKey(e.id, e.a, side))?.owner,
        sourceEnd = layout.mouths.get(mouthKey(e.id, e.b, side))?.owner;
      const startRamp = centers.find((c) => Math.abs(c - (min + 3)) < 1e-5),
        endRamp = centers.find((c) => Math.abs(c - (max - 3)) < 1e-5);
      const startCut = startRamp === undefined ? min : startRamp,
        endCut = endRamp === undefined ? max : endRamp;
      for (const [owner, cut, end, hasRamp] of [
        [sourceStart, startCut, "start", startRamp !== undefined],
        [sourceEnd, endCut, "end", endRamp !== undefined],
      ])
        if (owner && streetSettings(owner).paving !== cfg.paving)
          transitions.push({
            road: e.id,
            side,
            end,
            at: cut,
            mode: hasRamp ? "crossing-ramp" : "hard-joint",
            from: streetSettings(owner).paving,
            to: cfg.paving,
          });
      const surfaceMaterial = (s, u) => {
        const mat = materialAt(e, s, u, centers),
          owner = s < startCut ? sourceStart : s > endCut ? sourceEnd : null;
        if (!owner) return mat;
        const style = streetSettings(owner);
        if (["paving", "brick"].includes(mat))
          return style.paving === "brick" ? "brick" : "paving";
        if (
          ["granite", "curbstone", "brickcourse", "broomconcrete"].includes(mat)
        )
          return curbMaterial(style.curbStyle);
        return mat;
      };
      // Independent UV islands at the material divider keep the corner's tile
      // phase intact even when the next road uses a different brick/slab repeat.
      const startPaving = sourceStart
          ? streetSettings(sourceStart).paving
          : cfg.paving,
        endPaving = sourceEnd ? streetSettings(sourceEnd).paving : cfg.paving;
      const indexAt = (s) =>
        samples.reduce(
          (best, v, i) =>
            Math.abs(v - s) < Math.abs(samples[best] - s) ? i : best,
          0,
        );
      const first = startPaving === cfg.paving ? 0 : indexAt(startCut),
        last = endPaving === cfg.paving ? samples.length - 1 : indexAt(endCut);
      const uvBlocks =
        first === 0 && last === samples.length - 1
          ? [{ lo: 0, hi: last, uv: stripUV }]
          : [
              [0, first, startPaving],
              [first, last, cfg.paving],
              [last, samples.length - 1, endPaving],
            ]
              .filter(([lo, hi]) => hi > lo)
              .map(([lo, hi, paving]) => ({
                lo,
                hi,
                uv: ribbonUVRows(stripRows.slice(lo, hi + 1), {
                  material: paving === "brick" ? "brick" : "paving",
                  reverse: side > 0,
                }),
              }));
      for (let i = 1; i < samples.length; i++) {
        const block = uvBlocks.find((b) => i > b.lo && i <= b.hi),
          tex = (row, k, mat) =>
            ["paving", "brick"].includes(mat)
              ? block.uv[row - block.lo][k]
              : stripUV[row][k];
        const s0 = samples[i - 1],
          s1 = samples[i],
          mid = (s0 + s1) / 2,
          inDrain = drain && mid > drain.start && mid < drain.end;
        for (let k = 1; k < profile.length; k++) {
          const [u0, h0] = profile[k - 1],
            [u1, h1] = profile[k];
          if (detailedGutters && k === 1) continue;
          if (inDrain && cfg.drainage === "channel" && k === 1) continue;
          if (inDrain && cfg.drainage === "inlet" && k === 2) continue;
          // Gutter stays at road level; only the curb and pavement are lowered.
          const h = (s, u, base) =>
            k === 1 || base === 0 ? pos(s, u, 0) : surface(s, u, base);
          const mat = surfaceMaterial(mid, (u0 + u1) / 2);
          face(
            mesh,
            [h(s0, u0, h0), h(s1, u0, h0), h(s1, u1, h1), h(s0, u1, h1)],
            mat,
            side > 0,
            [
              tex(i - 1, k - 1, mat),
              tex(i, k - 1, mat),
              tex(i, k, mat),
              tex(i - 1, k, mat),
            ],
          );
        }
        const walkPanel = [
          surface(s0, profile[4][0]),
          surface(s1, profile[4][0]),
          surface(s1, g + e.sidewalk),
          surface(s0, g + e.sidewalk),
        ];
        if (side > 0) walkPanel.reverse();
        panels.push({
          points: walkPanel,
          owner: `road:${e.id}:${side}`,
          bridge: e.structure === "bridge",
        });
        if (inDrain && cfg.drainage === "inlet") {
          gridFace(
            mesh,
            (s, u) => pos(s, u, cfg.curbHeight - 0.035),
            s0,
            s1,
            g,
            g + 0.32,
            "drainmetal",
            -side,
          );
          face(
            mesh,
            [
              pos(s0, g + 0.32, 0.005),
              pos(s1, g + 0.32, 0.005),
              pos(s1, g + 0.32, cfg.curbHeight - 0.035),
              pos(s0, g + 0.32, cfg.curbHeight - 0.035),
            ],
            "drainvoid",
            side < 0,
          );
          gridFace(
            mesh,
            (s, u) => pos(s, u, 0.005),
            s0,
            s1,
            g,
            g + 0.32,
            "drainvoid",
            side,
          );
        }
      }
      for (const c of centers) {
        const half = cfg.rampStyle === "blended" ? 1.5 : 1.2,
          run = Math.min(e.sidewalk - 0.3, (cfg.curbHeight - 0.006) * 12);
        ramps.push({
          road: e.id,
          side,
          s: c,
          run,
          rise: cfg.curbHeight - 0.006,
          grade: (cfg.curbHeight - 0.006) / run,
          style: cfg.rampStyle,
          lip: 0.006,
          width: half * 2,
        });
        if (cfg.tactile !== "none")
          for (let s = c - half + 0.055; s < c + half - 0.04; s += 0.1)
            for (
              let u = g + 0.4;
              u < Math.min(g + 0.94, g + e.sidewalk - 0.04);
              u += 0.1
            ) {
              const q = surface(s, u);
              instances[cfg.tactile].push({
                position: q,
                normal: [
                  (-r.nx * side * (cfg.curbHeight - 0.006)) / run,
                  1,
                  (-r.nz * side * (cfg.curbHeight - 0.006)) / run,
                ],
              });
            }
      }
      if (drain) {
        drains.push({
          road: e.id,
          side,
          type: cfg.drainage,
          ...drain,
          openings: openings.map((h) => ({
            ...h,
            position: pos(h.s, h.u, h.bed),
          })),
        });
        if (cfg.drainage === "channel")
          channel(mesh, pos, drain.start, drain.end, g, side);
        else if (cfg.drainage === "inlet") {
          for (const [s, reverse] of [
            [drain.start, false],
            [drain.end, true],
          ])
            face(
              mesh,
              [
                pos(s, g, 0.005),
                pos(s, g + 0.32, 0.005),
                pos(s, g + 0.32, cfg.curbHeight - 0.035),
                pos(s, g, cfg.curbHeight - 0.035),
              ],
              "drainmetal",
              reverse !== side < 0,
            );
          accessCover(
            mesh,
            pos,
            drainCenter,
            g + Math.min(e.sidewalk - 0.35, 0.95),
            cfg.curbHeight,
            0.3,
            side,
          );
        }
      }
    }
  }
  // Junction sidewalks use mitered shared cross-sections rather than overlapping boxes.
  for (const j of result.junctions)
    for (let index = 0; index < j.corners.length; index++) {
      const corner = j.corners[index],
        start = j.arms[index],
        end = j.arms[(index + 1) % j.arms.length];
      const design = layout.corners.get(cornerKey(j, index)),
        owner = design.owner,
        paved = owner.sidewalk > 0;
      const gutterA =
          start.e.sidewalk > 0 ? start.e : { ...start.e, drainage: "none" },
        gutterB = end.e.sidewalk > 0 ? end.e : { ...end.e, drainage: "none" };
      const pts = corner.points,
        sa = streetSettings(owner),
        sb = sa,
        rows = [],
        materials = [];
      const g0 = gutterWidth(gutterA),
        g1 = gutterWidth(gutterB);
      // Both endpoint profiles have the same semantic bands; interpolate their widths/heights.

      // Use the exact road endpoint profile union, avoiding T-junctions at width changes.
      const pa =
          start.e.sidewalk > 0 ? section(start.e, g0) : shoulderProfile(),
        pb = end.e.sidewalk > 0 ? section(end.e, g1) : shoulderProfile(),
        count = Math.max(pa.length, pb.length);

      for (let i = 0; i < pts.length; i++) {
        const t = corner.fractions?.[i] ?? i / (pts.length - 1),
          v = pts[i];
        const [nx, nz] = corner.normals[i];
        const row = [];
        for (let k = 0; k < count; k++) {
          const a = pa[Math.min(k, pa.length - 1)],
            b = pb[Math.min(k, pb.length - 1)],
            blend = design.mixed
              ? terminationFraction(t, design, start.e.sidewalk > 0)
              : t,
            u = interpolate(a[0], b[0], t);
          let h = interpolate(a[1], b[1], blend);
          // The optional blended corner lowers the middle of the curved curb itself.
          if (
            k > 1 &&
            (sa.rampStyle === "blended" || sb.rampStyle === "blended") &&
            Math.min(start.e.sidewalk, end.e.sidewalk) >= 1.2
          ) {
            const factor = Math.sin(t * Math.PI) ** 2,
              g = interpolate(g0, g1, t),
              run = Math.min(
                interpolate(start.e.sidewalk, end.e.sidewalk, t) - 0.3,
                1.8,
              );
            h = interpolate(
              h,
              0.006 +
                (interpolate(sa.curbHeight, sb.curbHeight, t) - 0.006) *
                  clamp((u - g) / run),
              factor,
            );
          }
          row.push([v[0] + nx * u, v[1] + h, v[2] + nz * u]);
        }
        rows.push(row);
      }
      // Canonicalize sub-micron arithmetic differences at the shared mouth.
      // Do not conceal an actual profile mismatch: those remain audit errors.
      for (const [rowIndex, key] of [
        [0, mouthKey(start.e.id, j.id, start.start ? 1 : -1)],
        [rows.length - 1, mouthKey(end.e.id, j.id, end.start ? -1 : 1)],
      ]) {
        const mouth = roadMouths.get(key);
        if (
          mouth &&
          rows[rowIndex].every(
            (v, k) =>
              Math.hypot(...v.map((x, d) => x - mesh.vertices[mouth[k]][d])) <
              1e-6,
          )
        )
          rows[rowIndex] = mouth.map((id) => [...mesh.vertices[id]]);
      }
      completedCorners.push({
        faceStart: mesh.faces.length,
        id: design.id,
        owner: owner.id,
        paving: paved ? sa.paving : "shoulder",
        mode: design.mode,
        run: design.run,
        grade: design.mixed ? sa.curbHeight / design.run : 0,
      });
      connections.push(
        {
          corner: design.id,
          mouth: mouthKey(start.e.id, j.id, start.start ? 1 : -1),
          vertices: rows[0].map((v) => mesh.vertex(v)),
        },
        {
          corner: design.id,
          mouth: mouthKey(end.e.id, j.id, end.start ? -1 : 1),
          vertices: rows.at(-1).map((v) => mesh.vertex(v)),
        },
      );
      const cornerUV = ribbonUVRows(rows, {
        material: sa.paving === "brick" ? "brick" : "paving",
      });
      for (let i = 1; i < rows.length; i++) {
        const row = rows[i];
        if (i)
          panels.push({
            points: [
              rows[i - 1][paved ? 4 : 0],
              row[paved ? 4 : 0],
              row.at(-1),
              rows[i - 1].at(-1),
            ],
            owner: `corner:${j.id}:${index}`,
            bridge:
              start.e.structure === "bridge" || end.e.structure === "bridge",
          });
        if (i)
          for (let k = 1; k < count; k++) {
            if (detailedGutters && k === 1) {
              emitGutterCorner(
                mesh,
                rows[i - 1],
                row,
                cornerUV[i - 1],
                cornerUV[i],
                gutterA,
                gutterB,
                corner.fractions[i - 1],
                corner.fractions[i],
                paved
                  ? (CHANNELS[sa.drainage]?.material ?? "gutter")
                  : start.e.bridgeNetwork || end.e.bridgeNetwork
                    ? "broomconcrete"
                    : "shoulder",
              );
              continue;
            }
            const q = [rows[i - 1][k - 1], row[k - 1], row[k], rows[i - 1][k]];
            if (quadArea(q) < 1e-9) continue;
            const blended =
              (sa.rampStyle === "blended" || sb.rampStyle === "blended") &&
              Math.min(start.e.sidewalk, end.e.sidewalk) >= 1.2;
            const tactile =
                sa.rampStyle === "blended" ? sa.tactile : sb.tactile,
              hasTactile =
                blended &&
                tactile !== "none" &&
                k === 7 &&
                i > 1 &&
                i < pts.length - 1;
            face(
              mesh,
              q,
              !paved
                ? start.e.bridgeNetwork || end.e.bridgeNetwork
                  ? "broomconcrete"
                  : "shoulder"
                : hasTactile
                  ? `tactile_${tactile}`
                  : k === 1
                    ? "gutter"
                    : k <= 5
                      ? curbMaterial(sa.curbStyle)
                      : sa.paving === "brick"
                        ? "brick"
                        : "paving",
              false,
              [
                cornerUV[i - 1][k - 1],
                cornerUV[i][k - 1],
                cornerUV[i][k],
                cornerUV[i - 1][k],
              ],
            );
            if (hasTactile) {
              const length = Math.hypot(q[1][0] - q[0][0], q[1][2] - q[0][2]),
                width = Math.hypot(q[3][0] - q[0][0], q[3][2] - q[0][2]);
              for (let d = 0.05; d < length; d += 0.1)
                for (let u = 0.05; u < width; u += 0.1) {
                  const a = d / length,
                    b = u / width;
                  const position = q[0].map(
                    (v, k) =>
                      (1 - a) * (1 - b) * v +
                      a * (1 - b) * q[1][k] +
                      a * b * q[2][k] +
                      (1 - a) * b * q[3][k],
                  );
                  const normal = new THREE.Vector3()
                    .subVectors(
                      new THREE.Vector3(...q[1]),
                      new THREE.Vector3(...q[0]),
                    )
                    .cross(
                      new THREE.Vector3().subVectors(
                        new THREE.Vector3(...q[3]),
                        new THREE.Vector3(...q[0]),
                      ),
                    )
                    .normalize();
                  if (normal.y < 0) normal.negate();
                  instances[tactile].push({
                    position,
                    normal: normal.toArray(),
                  });
                }
            }
          }
      }
      completedCorners.at(-1).faceEnd = mesh.faces.length;
    }
  return {
    mesh,
    ramps,
    drains,
    instances,
    gutterWidth,
    panels,
    expectedCorners: [...layout.corners.keys()],
    completedCorners,
    transitions,
    seams: connections.map((c) => ({
      ...c,
      external: !expectedMouths.has(c.mouth),
      roadVertices: roadMouths.get(c.mouth) ?? [],
    })),
    corners: result.junctions.flatMap((j) =>
      j.corners.map((c, i) => ({ ...c, id: `${j.id}:${i}` })),
    ),
  };
}
function channel(mesh, pos, start, end, width, side) {
  const ss = [start, start + 0.04, end - 0.04, end],
    us = [0, 0.035, width - 0.035, width];
  for (let s = start + 0.12; s < end - 0.05; s += 0.13)
    ss.push(s - 0.025, s + 0.025);
  for (let u = 0.17; u < width - 0.06; u += 0.17) us.push(u - 0.013, u + 0.013);
  const xs = unique(ss),
    ys = unique(us),
    solid = (i, j) => {
      const s = (xs[i] + xs[i + 1]) / 2,
        u = (ys[j] + ys[j + 1]) / 2;
      return (
        s < start + 0.04 ||
        s > end - 0.04 ||
        u < 0.035 ||
        u > width - 0.035 ||
        Math.abs(
          ((((s - start - 0.12 + 0.065) % 0.13) + 0.13) % 0.13) - 0.065,
        ) < 0.025 ||
        Math.abs(u - 0.17) < 0.0131 ||
        Math.abs(u - 0.34) < 0.0131
      );
    };
  for (let i = 0; i < xs.length - 1; i++)
    for (let j = 0; j < ys.length - 1; j++) {
      if (!solid(i, j)) continue;
      const a = xs[i],
        b = xs[i + 1],
        u = ys[j],
        v = ys[j + 1];
      gridFace(
        mesh,
        (s, u) => pos(s, u, 0.004),
        a,
        b,
        u,
        v,
        "drainmetal",
        side,
      );
      gridFace(
        mesh,
        (s, u) => pos(s, u, -0.024),
        a,
        b,
        u,
        v,
        "drainmetal",
        -side,
      );
      for (const [ni, nj, edge] of [
        [i - 1, j, 0],
        [i + 1, j, 1],
        [i, j - 1, 2],
        [i, j + 1, 3],
      ])
        if (
          ni < 0 ||
          nj < 0 ||
          ni >= xs.length - 1 ||
          nj >= ys.length - 1 ||
          !solid(ni, nj)
        ) {
          const pair =
            edge === 0
              ? [
                  [a, u],
                  [a, v],
                ]
              : edge === 1
                ? [
                    [b, v],
                    [b, u],
                  ]
                : edge === 2
                  ? [
                      [b, u],
                      [a, u],
                    ]
                  : [
                      [a, v],
                      [b, v],
                    ];
          face(
            mesh,
            [
              pos(...pair[0], 0.004),
              pos(...pair[1], 0.004),
              pos(...pair[1], -0.024),
              pos(...pair[0], -0.024),
            ],
            "drainmetal",
            side > 0,
          );
        }
    }
  gridFace(
    mesh,
    (s, u) => pos(s, u, -0.075),
    start,
    end,
    0,
    width,
    "drainvoid",
    side,
  );
  for (const u of [0, width])
    face(
      mesh,
      [
        pos(start, u, -0.075),
        pos(end, u, -0.075),
        pos(end, u, -0.024),
        pos(start, u, -0.024),
      ],
      "drainvoid",
      (u === 0) === side > 0,
    );
  for (const s of [start, end])
    face(
      mesh,
      [
        pos(s, 0, -0.075),
        pos(s, width, -0.075),
        pos(s, width, -0.024),
        pos(s, 0, -0.024),
      ],
      "drainvoid",
      (s === start) !== side > 0,
    );
}
function accessCover(mesh, pos, s, u, h, r, side) {
  const outer = [],
    inner = [];
  for (let i = 0; i < 16; i++) {
    const angle = (i * Math.PI) / 8;
    outer.push(
      pos(s + Math.cos(angle) * r, u + Math.sin(angle) * r, h + 0.006),
    );
    inner.push(
      pos(
        s + Math.cos(angle) * r * 0.91,
        u + Math.sin(angle) * r * 0.91,
        h + 0.009,
      ),
    );
  }
  const center = pos(s, u, h + 0.009);
  const coverUV = (i) => [
    Math.cos((i * Math.PI) / 8) * r * 0.91,
    Math.sin((i * Math.PI) / 8) * r * 0.91,
  ];
  for (let i = 0; i < 16; i++) {
    face(
      mesh,
      [outer[i], outer[(i + 1) % 16], inner[(i + 1) % 16], inner[i]],
      "drainmetal",
      side > 0,
    );
    if (i % 2 === 0)
      face(
        mesh,
        [inner[i], inner[(i + 1) % 16], center, inner[(i + 15) % 16]],
        "ironcover",
        side > 0,
        [coverUV(i), coverUV((i + 1) % 16), [0, 0], coverUV((i + 15) % 16)],
      );
  }
}
export function tactilePrototype() {
  const mesh = new QuadMesh(),
    rings = [];
  for (const [radius, y] of [
    [0.016, 0],
    [0.014, 0.004],
    [0.009, 0.006],
  ]) {
    const ring = [];
    for (let i = 0; i < 8; i++) {
      const a = (i * Math.PI) / 4;
      ring.push([Math.cos(a) * radius, y, Math.sin(a) * radius]);
    }
    rings.push(ring);
  }
  for (let k = 1; k < 3; k++)
    for (let i = 0; i < 8; i++)
      face(
        mesh,
        [
          rings[k - 1][i],
          rings[k - 1][(i + 1) % 8],
          rings[k][(i + 1) % 8],
          rings[k][i],
        ],
        "tactile",
      );
  for (let i = 0; i < 8; i += 2) {
    face(
      mesh,
      [
        rings[2][i],
        rings[2][(i + 1) % 8],
        [0, 0.006, 0],
        rings[2][(i + 7) % 8],
      ],
      "tactile",
      true,
    );
    face(
      mesh,
      [rings[0][i], rings[0][(i + 1) % 8], [0, 0, 0], rings[0][(i + 7) % 8]],
      "tactile",
    );
  }
  return mesh;
}
export function quadArea(q) {
  const v = q.map((p) => new THREE.Vector3(...p));
  return (
    new THREE.Vector3()
      .subVectors(v[1], v[0])
      .cross(new THREE.Vector3().subVectors(v[2], v[0]))
      .length() /
      2 +
    new THREE.Vector3()
      .subVectors(v[2], v[0])
      .cross(new THREE.Vector3().subVectors(v[3], v[0]))
      .length() /
      2
  );
}
export function auditStreetscape(street) {
  let degenerate = 0,
    duplicates = 0,
    invertedFaces = 0,
    foldedFaces = 0;
  const seen = new Set();
  for (let faceId = 0; faceId < street.mesh.faces.length; faceId++) {
    const f = street.mesh.faces[faceId],
      material = street.mesh.groups[faceId];
    if (!["drainmetal", "drainvoid", "ironcover"].includes(material)) {
      const shape = planShape(f.map((i) => street.mesh.vertices[i]));
      if (shape.inverted) invertedFaces++;
      if (shape.folded) foldedFaces++;
    }
    if (
      new Set(f).size !== 4 ||
      quadArea(f.map((i) => street.mesh.vertices[i])) < 1e-10
    )
      degenerate++;
    const key = [...f].sort((a, b) => a - b).join(":");
    if (seen.has(key)) duplicates++;
    seen.add(key);
  }
  const panelAudit = auditPavementPanels(street);
  const missing = (street.expectedCorners ?? []).filter(
    (id) =>
      !street.completedCorners?.some(
        (c) => c.id === id && c.faceEnd > c.faceStart,
      ),
  );
  const brokenSeams = (street.seams ?? []).filter(
    (s) =>
      !s.external &&
      (s.vertices.length !== s.roadVertices.length ||
        s.vertices.some((v, i) => v !== s.roadVertices[i])),
  );
  const missingCorners = missing.length,
    seamErrors = brokenSeams.length,
    issues = [
      ...panelAudit.issues,
      ...missing.map((owner) => ({ type: "missing-return", owner })),
      ...brokenSeams.map((s) => ({ type: "mouth-seam", owner: s.mouth })),
    ].slice(0, 24);
  const steepTransitions = (street.completedCorners ?? []).filter(
    (c) => c.grade > 1 / 12 + 0.0001,
  ).length;
  return {
    ...panelAudit,
    issues,
    missingCorners,
    seamErrors,
    steepTransitions,
    ok:
      !missingCorners &&
      !seamErrors &&
      panelAudit.ok &&
      !degenerate &&
      !duplicates &&
      !invertedFaces &&
      !foldedFaces,
    invertedFaces,
    foldedFaces,
    quads: street.mesh.faces.length,
    degenerate,
    duplicates,
    ramps: street.ramps.length,
    drains: street.drains.length,
    steepRamps: street.ramps.filter((r) => r.grade > 1 / 12 + 0.0001).length,
  };
}
export function exportStreetscapeOBJ(street) {
  if (!auditStreetscape(street).ok)
    throw Error(
      "Streetscape export blocked: missing returns, mismatched seams, inverted, folded, overlapping or insufficient-clearance paving.",
    );
  let out =
    "# Frontier streetscape | meters | Y up | quad faces\n# Open curb/pavement ribbons and drain grilles. Not a watertight whole-world solid.\n";
  const base = street.mesh;
  out +=
    base.vertices
      .map((v) => `v ${v.map((n) => n.toFixed(6)).join(" ")}`)
      .join("\n") + "\n";
  out += objTextureCoordinates(base);
  let group = "";
  base.faces.forEach((f, i) => {
    if (base.groups[i] !== group) {
      group = base.groups[i];
      out += `g ${group}\n`;
    }
    out += `f ${f.map((v, k) => `${v + 1}/${i * 4 + k + 1}`).join(" ")}\n`;
  });
  const proto = tactilePrototype(),
    up = new THREE.Vector3(0, 1, 0);
  let offset = base.vertices.length;
  for (const [color, list] of Object.entries(street.instances)) {
    out += `g tactile_domes_${color}\n`;
    for (const item of list) {
      const q = new THREE.Quaternion().setFromUnitVectors(
          up,
          new THREE.Vector3(...item.normal).normalize(),
        ),
        pos = new THREE.Vector3(...item.position);
      for (const v of proto.vertices) {
        const p = new THREE.Vector3(...v).applyQuaternion(q).add(pos);
        out += `v ${p
          .toArray()
          .map((n) => n.toFixed(6))
          .join(" ")}\n`;
      }
      for (const f of proto.faces)
        out += `f ${f.map((i) => i + offset + 1).join(" ")}\n`;
      offset += proto.vertices.length;
    }
  }
  return out;
}

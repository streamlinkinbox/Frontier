// Formed water-channel beds and real collector openings. No flow simulation.
export const CHANNELS = {
  "brick-channel": {
    width: 0.58,
    depth: 0.025,
    material: "brickcourse",
    collector: "grille",
  },
  "weep-channel": {
    width: 0.62,
    depth: 0.035,
    material: "broomconcrete",
    collector: "weeps",
  },
  "slot-channel": {
    width: 0.52,
    depth: 0.045,
    material: "gutter",
    collector: "slot",
  },
};
export const channelWidth = (e) =>
  CHANNELS[e.drainage]?.width ??
  { none: 0.12, channel: 0.52, inlet: 0.38 }[e.drainage ?? "inlet"];
export const isWaterChannel = (e) => !!CHANNELS[e.drainage];
export const GUTTER_CUTS = [
  0, 0.2, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.8, 1,
];
export const curbMaterial = (style) =>
  style === "brick-roll"
    ? "brickcourse"
    : style === "spoon"
      ? "broomconcrete"
      : style === "granite"
        ? "granite"
        : "curbstone";
export function bedHeight(e, f, s = null, drain = null) {
  const c = CHANNELS[e.drainage];
  if (!c) return 0;
  const collector =
    drain && s !== null
      ? 0.018 * Math.max(0, 1 - Math.abs(s - (drain.start + drain.end) / 2) / 4)
      : 0;
  return -(c.depth + collector) * Math.sin(Math.PI * f) ** 2;
}
export function collectorHoles(e, drain) {
  if (!drain || !isWaterChannel(e)) return [];
  const center = (drain.start + drain.end) / 2;
  if (e.drainage === "slot-channel")
    return [{ a: drain.start, b: drain.end, lo: 0.45, hi: 0.55, round: false }];
  const weeps = e.drainage === "weep-channel",
    n = weeps ? 4 : 6,
    spacing = weeps ? 0.34 : 0.25,
    half = weeps ? 0.075 : 0.07;
  return Array.from({ length: n }, (_, i) => {
    const s = center + (i - (n - 1) / 2) * spacing;
    return { a: s - half, b: s + half, lo: 0.35, hi: 0.65, round: weeps };
  });
}
export function collectorStations(e, drain) {
  return collectorHoles(e, drain).flatMap((h) => [h.a, (h.a + h.b) / 2, h.b]);
}
function face(mesh, points, mat, side, uv = null) {
  const ids = points.map((p) => mesh.vertex(p));
  if (side > 0) {
    ids.reverse();
    if (uv) uv = [...uv].reverse();
  }
  mesh.faces.push(ids);
  mesh.groups.push(mat);
  mesh.uvs.push(uv);
}
export function emitGutterStrip(mesh, pos, samples, stripUV, e, drain, side) {
  const g = channelWidth(e),
    cfg = CHANNELS[e.drainage],
    holes = collectorHoles(e, drain);
  const at = (s, f) => pos(s, f * g, bedHeight(e, f, s, drain));
  for (let i = 1; i < samples.length; i++) {
    const a = samples[i - 1],
      b = samples[i],
      s = (a + b) / 2;
    if (e.drainage === "channel" && drain && s > drain.start && s < drain.end)
      continue;
    for (let k = 1; k < GUTTER_CUTS.length; k++) {
      const lo = GUTTER_CUTS[k - 1],
        hi = GUTTER_CUTS[k],
        mid = (lo + hi) / 2;
      if (holes.some((h) => s > h.a && s < h.b && mid > h.lo && mid < h.hi))
        continue;
      const plate =
        cfg?.collector === "grille" &&
        drain &&
        s >= drain.start &&
        s <= drain.end &&
        mid >= 0.2 &&
        mid <= 0.8;
      const rim =
        cfg?.collector === "slot" &&
        drain &&
        s >= drain.start &&
        s <= drain.end &&
        mid >= 0.4 &&
        mid <= 0.6;
      face(
        mesh,
        [at(a, lo), at(b, lo), at(b, hi), at(a, hi)],
        plate || rim ? "drainmetal" : (cfg?.material ?? "gutter"),
        side,
        [
          [stripUV[i - 1][0][0], lo * g],
          [stripUV[i][0][0], lo * g],
          [stripUV[i][0][0], hi * g],
          [stripUV[i - 1][0][0], hi * g],
        ],
      );
    }
  }
  for (const hole of holes) {
    const { a, b, lo, hi } = hole,
      s = (a + b) / 2,
      u = (lo + hi) / 2;
    // The box boundary follows every surrounding mesh station and cross-cut.
    const ss = samples.filter((x) => x >= a - 1e-7 && x <= b + 1e-7),
      ff = GUTTER_CUTS.filter((x) => x >= lo && x <= hi);
    const outer = [
      ...ss.map((x) => [x, lo]),
      ...ff.slice(1).map((f) => [b, f]),
      ...ss
        .slice(0, -1)
        .reverse()
        .map((x) => [x, hi]),
      ...ff
        .slice(1, -1)
        .reverse()
        .map((f) => [a, f]),
    ];
    const inner = hole.round
      ? outer.map(([x, f]) => {
          const nx = (x - s) / ((b - a) / 2),
            nz = (f - u) / ((hi - lo) / 2),
            l = Math.hypot(nx, nz);
          return [
            s + (nx / l) * (b - a) * 0.35,
            u + (nz / l) * (hi - lo) * 0.35,
          ];
        })
      : outer;
    if (hole.round)
      for (let k = 0; k < outer.length; k++) {
        const j = (k + 1) % outer.length,
          coords = [outer[k], outer[j], inner[j], inner[k]];
        face(
          mesh,
          coords.map(([x, f]) => at(x, f)),
          cfg.material,
          side,
          coords.map(([x, f]) => [x, g * f]),
        );
      }
    for (let k = 0; k < inner.length; k++) {
      const j = (k + 1) % inner.length,
        [x, f] = inner[k],
        [xx, ff] = inner[j];
      face(
        mesh,
        [at(x, f), at(xx, ff), pos(xx, ff * g, -0.105), pos(x, f * g, -0.105)],
        "drainvoid",
        side,
      );
    }
    face(
      mesh,
      [
        pos(a, lo * g, -0.105),
        pos(b, lo * g, -0.105),
        pos(b, hi * g, -0.105),
        pos(a, hi * g, -0.105),
      ],
      "drainvoid",
      side,
    );
  }
  return holes.map((h) => ({
    s: (h.a + h.b) / 2,
    u: (g * (h.lo + h.hi)) / 2,
    bed: bedHeight(e, (h.lo + h.hi) / 2, (h.a + h.b) / 2, drain),
    bottom: -0.105,
    round: h.round,
  }));
}
export function emitGutterCorner(
  mesh,
  prev,
  row,
  prevUV,
  rowUV,
  a,
  b,
  t0,
  t1,
  material = null,
) {
  const g0 = channelWidth(a),
    g1 = channelWidth(b),
    mix = (x, y, t) => x + (y - x) * t;
  const at = (r, f, t) => {
    const p = r[0].map((v, k) => mix(v, r[1][k], f));
    p[1] += mix(bedHeight(a, f), bedHeight(b, f), t);
    return p;
  };
  for (let k = 1; k < GUTTER_CUTS.length; k++) {
    const lo = GUTTER_CUTS[k - 1],
      hi = GUTTER_CUTS[k],
      ga = mix(g0, g1, t0),
      gb = mix(g0, g1, t1);
    const mat =
      CHANNELS[(t0 + t1) / 2 < 0.5 ? a.drainage : b.drainage]?.material ??
      "gutter";
    face(
      mesh,
      [at(prev, lo, t0), at(row, lo, t1), at(row, hi, t1), at(prev, hi, t0)],
      material ?? mat,
      -1,
      [
        [prevUV[0][0], lo * ga],
        [rowUV[0][0], lo * gb],
        [rowUV[0][0], hi * gb],
        [prevUV[0][0], hi * ga],
      ],
    );
  }
}

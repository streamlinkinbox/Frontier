// Shared cross-section contract for topology, markings, planting and the editor.
export const LANDSCAPES = {
  parking: { name: "Parking-protected street", median: 0 },
  standard: { name: "Standard street", median: 0 },
  garden: { name: "Garden boulevard", median: 3.2 },
  cycle: { name: "Protected cycle street", median: 0 },
  tram: { name: "Green tram boulevard", median: 7 },
};
export const modern = (e) => !!e.landscape && e.landscape !== "standard";
export function streetLayout(e) {
  const mode = e.landscape === undefined ? "standard" : e.landscape;
  if (typeof mode !== "string" || !Object.hasOwn(LANDSCAPES, mode))
    throw Error("Unknown landscape layout.");
  if (
    modern(e) &&
    (e.lanes < 2 ||
      e.lanes % 2 ||
      e.structure === "bridge" ||
      e.sidewalk < 1.8 ||
      (["cycle", "parking"].includes(mode) && !e.bike))
  )
    throw Error(
      "Landscaped streets need an even number of traffic lanes, ground-level construction and at least 1.8 m sidewalks. Cycle and parking-protected streets also need their cycle tracks.",
    );
  const median = LANDSCAPES[mode].median,
    cycle = e.bike ? (modern(e) ? 2 : 1.6) : 0,
    parking = mode === "parking" ? 2.2 : 0,
    buffer = e.bike && modern(e) ? (parking ? 0.9 : 1.2) : 0,
    traffic = e.lanes * e.laneWidth,
    inner = median / 2,
    outer = inner + traffic / 2,
    width = median + traffic + 2 * (cycle + buffer + parking);
  const bands = [];
  if (median)
    bands.push({
      lo: -inner,
      hi: inner,
      kind: mode === "tram" ? "tram" : "median",
    });
  for (const side of [-1, 1]) {
    const band = (lo, hi, kind) =>
      bands.push({ lo: side < 0 ? -hi : lo, hi: side < 0 ? -lo : hi, kind });
    band(inner, outer, "traffic");
    if (parking) band(outer, outer + parking, "parking");
    if (buffer)
      band(
        outer + parking,
        outer + parking + buffer,
        parking ? "doorzone" : "buffer",
      );
    if (cycle) band(outer + parking + buffer, width / 2, "cycle");
  }
  const cuts = new Set([-width / 2, 0, width / 2]);
  for (const b of bands) {
    cuts.add(b.lo);
    cuts.add(b.hi);
    if (["median", "tram", "buffer"].includes(b.kind)) {
      cuts.add(b.lo + 0.18);
      cuts.add(b.hi - 0.18);
    }
  }
  const base = [...cuts].sort((a, b) => a - b),
    offsets = base.flatMap((v, i) => (i ? [(base[i - 1] + v) / 2, v] : [v]));
  return {
    mode,
    parking,
    median,
    cycle,
    buffer,
    traffic,
    inner,
    outer,
    width,
    bands,
    base,
    offsets,
  };
}
// Clear islands from the entire crossing width and keep vegetation farther back.
export function landscapePlan(e, from, to, length, degreeA = 1, degreeB = 1) {
  const cross = modern(e)
    ? [
        ...(degreeA >= 3 ? [from * length + 3] : []),
        ...(degreeB >= 3 ? [to * length - 3] : []),
      ].filter((s) => s > from * length + 1.6 && s < to * length - 1.6)
    : [];
  cross.splice(
    0,
    cross.length,
    ...cross.filter(
      (s, i, a) =>
        s - 2.2 > from * length + 0.02 &&
        s + 2.2 < to * length - 0.02 &&
        (!i || s - a[i - 1] > 4.4),
    ),
  );
  let intervals = [[from * length + 0.6, to * length - 0.6]];
  for (const c of cross)
    intervals = intervals.flatMap(([a, b]) =>
      b <= c - 1.6 || a >= c + 1.6
        ? [[a, b]]
        : [
            [a, Math.min(b, c - 1.6)],
            [Math.max(a, c + 1.6), b],
          ].filter(([x, y]) => y - x > 0.5),
    );
  intervals = intervals.filter(([a, b]) => b - a > 0.5);
  const stations = new Set([from * length, to * length]);
  const count = Math.max(1, Math.ceil(((to - from) * length) / 3));
  for (let i = 1; i < count; i++)
    stations.add(from * length + ((to - from) * length * i) / count);
  for (const [a, b] of intervals)
    for (const s of [a, a + 0.22, b - 0.22, b]) stations.add(s);
  for (const c of cross) for (const s of [c - 1.6, c + 1.6]) stations.add(s);
  return {
    crossings: cross,
    intervals,
    stations: [...stations]
      .sort((a, b) => a - b)
      .filter((s, i, a) => !i || s - a[i - 1] > 1e-4),
  };
}
export function islandFactor(plan, s) {
  const range = plan.intervals.find(([a, b]) => s >= a - 1e-8 && s <= b + 1e-8);
  return range
    ? Math.max(0, Math.min(1, (s - range[0]) / 0.22, (range[1] - s) / 0.22))
    : 0;
}
export function surfaceAt(layout, plan, s, u) {
  const band = layout.bands.find((b) => u >= b.lo - 1e-8 && u <= b.hi + 1e-8);
  if (!band || ["traffic", "cycle", "parking", "doorzone"].includes(band.kind))
    return {
      height: 0,
      kind:
        band?.kind === "cycle"
          ? "cycle"
          : band?.kind === "parking"
            ? "parking"
            : band?.kind === "doorzone"
              ? "refuge"
              : "asphalt",
    };
  const across = Math.max(
      0,
      Math.min(1, (u - band.lo) / 0.18, (band.hi - u) / 0.18),
    ),
    along = islandFactor(plan, s),
    factor = across * along;
  return {
    height: (band.kind === "tram" ? 0 : 0.15) * factor,
    kind:
      along < 0.001
        ? "refuge"
        : across < 0.999 || along < 0.999
          ? "curb"
          : band.kind === "tram"
            ? "trackgrass"
            : "planting",
  };
}
export function plantingStations(layout, plan) {
  const plants = [];
  for (const [a, b] of plan.intervals) {
    if (b - a < 9) continue;
    for (let s = a + 4.5; s <= b - 4.5; s += 9) {
      if (plan.crossings.some((c) => Math.abs(c - s) < 7)) continue;
      if (layout.mode === "garden")
        plants.push({ s, u: 0, kind: "tree", size: 1.35 });
      if (layout.mode === "tram")
        for (const side of [-1, 1])
          plants.push({
            s,
            u: side * (layout.inner - 0.48),
            kind: "shrub",
            size: 0.25,
          });
    }
    for (let s = a + 4; s <= b - 4; s += 2.8) {
      if (plan.crossings.some((c) => Math.abs(c - s) < 6)) continue;
      if (layout.buffer && layout.mode !== "parking")
        for (const side of [-1, 1])
          plants.push({
            s,
            u: side * (layout.outer + layout.buffer / 2),
            kind: "shrub",
            size: 0.32,
          });
      if (layout.mode === "garden")
        for (const side of [-1, 1])
          plants.push({ s, u: side * 0.65, kind: "shrub", size: 0.25 });
    }
  }
  return plants;
}

export function parkingBays(r) {
  if (!r.layout?.parking) return [];
  const bays = [],
    start = r.from * r.length + 10,
    end = r.to * r.length - 10;
  for (const side of [-1, 1])
    for (let s = start; s + 6.4 <= end; s += 6.4)
      bays.push({
        s0: s,
        s1: s + 6.4,
        u: side * (r.layout.outer + r.layout.parking / 2),
        width: r.layout.parking,
        side,
      });
  return bays;
}

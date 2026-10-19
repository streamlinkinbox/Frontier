// One material owner per corner. Physical profile dimensions can still adapt
// between road widths; a change of surface is not a reason to omit a corner.
export const SHOULDER_WIDTH = 0.9;
export const shoulderProfile = () =>
  [0, 0.12, 0.16, 0.2, 0.28, 0.32, 0.35, 0.56, 0.73, SHOULDER_WIDTH].map(
    (u) => [u, 0],
  );
export function cornerOwner(a, b) {
  const paved = [a, b].filter((e) => e.sidewalk > 0);
  return (paved.length ? paved : [a, b]).sort(
    (x, y) => y.sidewalk - x.sidewalk || x.id.localeCompare(y.id),
  )[0];
}
export function cornerKey(j, i) {
  return `${j.id}:${i}`;
}
export function mouthKey(edge, node, side) {
  return `${edge}:${node}:${side}`;
}
export function cornerLength(c) {
  return c.points
    .slice(1)
    .reduce(
      (sum, p, i) =>
        sum + Math.hypot(p[0] - c.points[i][0], p[2] - c.points[i][2]),
      0,
    );
}
export function cornerLayout(result) {
  const corners = new Map(),
    mouths = new Map();
  for (const j of result.junctions)
    for (let i = 0; i < j.corners.length; i++) {
      const a = j.arms[i],
        b = j.arms[(i + 1) % j.arms.length],
        owner = cornerOwner(a.e, b.e),
        mixed = a.e.sidewalk > 0 !== b.e.sidewalk > 0,
        length = cornerLength(j.corners[i]);
      const run = Math.min(
        length,
        Math.max(2.4, (owner.curbHeight ?? 0.15) * 12),
      );
      const item = {
        id: cornerKey(j, i),
        owner,
        mixed,
        length,
        run,
        mode: mixed
          ? "graded-termination"
          : owner.sidewalk > 0
            ? "paved-corner"
            : "continuous-shoulder",
      };
      corners.set(item.id, item);
      mouths.set(mouthKey(a.e.id, j.id, a.start ? 1 : -1), item);
      mouths.set(mouthKey(b.e.id, j.id, b.start ? -1 : 1), item);
    }
  return { corners, mouths };
}
// Ramped height change stays at the unpaved end, rather than lowering the whole
// urban corner. Very short returns keep their true slope and are reported.
export function terminationFraction(t, layout, startPaved) {
  const span = layout.run / Math.max(layout.length, 1e-9);
  return startPaved
    ? Math.max(0, Math.min(1, (t - (1 - span)) / span))
    : Math.max(0, Math.min(1, t / span));
}

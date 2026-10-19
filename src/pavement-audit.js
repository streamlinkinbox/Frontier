// Positive-area intersections, not edge-touch tests. Operates on walkable ribbon
// panels (not overlaid tactile instances, grate underside faces or cover caps).
const cross = (a, b, c) =>
  (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]);
export function signedPlanArea(p) {
  let a = 0;
  for (let i = 0; i < p.length; i++) {
    const b = p[(i + 1) % p.length];
    a += p[i][0] * b[2] - b[0] * p[i][2];
  }
  return a / 2;
}
export function planShape(p) {
  const area = signedPlanArea(p),
    turns = p.map((_, i) =>
      cross(p[i], p[(i + 1) % p.length], p[(i + 2) % p.length]),
    );
  return {
    area,
    inverted: area > 1e-8,
    folded: turns.some((v) => v > 1e-8) && turns.some((v) => v < -1e-8),
    collapsed: Math.abs(area) < 1e-10,
  };
}
export function intersectionArea(a, b) {
  let polygon = a.map((p) => [...p]);
  const orientation = Math.sign(signedPlanArea(b));
  for (let i = 0; i < b.length && polygon.length; i++) {
    const c = b[i],
      d = b[(i + 1) % b.length],
      input = polygon;
    polygon = [];
    for (let j = 0; j < input.length; j++) {
      const p = input[j],
        q = input[(j + 1) % input.length],
        sp = cross(c, d, p) * orientation,
        sq = cross(c, d, q) * orientation,
        pin = sp >= -1e-10,
        qin = sq >= -1e-10;
      if (pin) polygon.push(p);
      if (pin !== qin) {
        const t = sp / (sp - sq);
        polygon.push(p.map((v, k) => v + (q[k] - v) * t));
      }
    }
  }
  return polygon.length >= 3 ? Math.abs(signedPlanArea(polygon)) : 0;
}
export function auditPavementPanels(street) {
  const panels = street.panels || [],
    cells = new Map(),
    candidates = new Set(),
    issues = [];
  let foldedPanels = 0,
    invertedPanels = 0,
    collapsedPanels = 0,
    overlapPairs = 0,
    overlapArea = 0;
  const meta = panels.map((panel, id) => {
    const q = panel.points,
      shape = planShape(q);
    if (shape.folded) foldedPanels++;
    if (shape.inverted) invertedPanels++;
    if (shape.collapsed) collapsedPanels++;
    if (
      (shape.folded || shape.inverted || shape.collapsed) &&
      issues.length < 24
    )
      issues.push({ type: "invalid-panel", owner: panel.owner });
    const x = q.map((v) => v[0]),
      z = q.map((v) => v[2]),
      y = q.map((v) => v[1]);
    return {
      ...panel,
      id,
      shape,
      minX: Math.min(...x),
      maxX: Math.max(...x),
      minZ: Math.min(...z),
      maxZ: Math.max(...z),
      minY: Math.min(...y),
      maxY: Math.max(...y),
    };
  });
  const cellSize = 4;
  for (const p of meta) {
    if (p.shape.folded || p.shape.collapsed || p.shape.inverted) continue;
    for (
      let x = Math.floor(p.minX / cellSize);
      x <= Math.floor(p.maxX / cellSize);
      x++
    )
      for (
        let z = Math.floor(p.minZ / cellSize);
        z <= Math.floor(p.maxZ / cellSize);
        z++
      ) {
        const key = `${x}:${z}`,
          bucket = cells.get(key) || [];
        for (const i of bucket) {
          const pair = `${i}:${p.id}`;
          if (candidates.has(pair)) continue;
          candidates.add(pair);
          const q = meta[i];
          if (
            p.minX >= q.maxX - 1e-8 ||
            q.minX >= p.maxX - 1e-8 ||
            p.minZ >= q.maxZ - 1e-8 ||
            q.minZ >= p.maxZ - 1e-8
          )
            continue;
          // Grade-separated bridge walks are not same-level paving overlaps.
          if (
            (p.bridge || q.bridge) &&
            (p.minY > q.maxY + 0.04 || q.minY > p.maxY + 0.04)
          )
            continue;
          const area = intersectionArea(p.points, q.points);
          if (area > 1e-6) {
            overlapPairs++;
            overlapArea += area;
            if (issues.length < 24)
              issues.push({
                type: "overlap",
                owners: [p.owner, q.owner],
                area,
              });
          }
        }
        bucket.push(p.id);
        cells.set(key, bucket);
      }
  }
  const failed = (street.corners || []).filter((c) => !c.valid),
    clearanceFailures = failed.length,
    clearanceDeficit = failed.reduce(
      (sum, c) => sum + Math.max(0, 0.12 - c.clearance),
      0,
    );
  for (const c of failed)
    if (issues.length < 24)
      issues.push({
        type: "clearance",
        owner: `corner:${c.id}`,
        deficit: 0.12 - c.clearance,
      });
  return {
    foldedPanels,
    invertedPanels,
    collapsedPanels,
    overlapPairs,
    overlapArea,
    clearanceFailures,
    clearanceDeficit,
    issues,
    ok: !(
      foldedPanels ||
      invertedPanels ||
      collapsedPanels ||
      overlapPairs ||
      clearanceFailures
    ),
  };
}

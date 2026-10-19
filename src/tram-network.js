// Straight-through tram continuity only: do not invent curved track routing or switches.
export function tramJunctionLinks(result) {
  const links = [];
  for (const j of result.junctions) {
    const arms = j.arms.filter((a) => a.e.landscape === "tram");
    if (arms.length !== 2) continue;
    const [a, b] = arms;
    if (
      a.dx * b.dx + a.dz * b.dz > -0.999999 ||
      Math.abs(a.width - b.width) > 1e-6
    )
      continue;
    const faces = result.mesh.faces
      .filter((_, i) => result.mesh.groups[i] === `junction:${j.id}`)
      .map((f) => f.map((v) => result.mesh.vertices[v]));
    const height = (x, z) => {
      for (const q of faces)
        for (const [A, B, C] of [
          [q[0], q[1], q[2]],
          [q[0], q[2], q[3]],
        ]) {
          const d =
            (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2]);
          if (Math.abs(d) < 1e-12) continue;
          const u =
              ((B[2] - C[2]) * (x - C[0]) + (C[0] - B[0]) * (z - C[2])) / d,
            v = ((C[2] - A[2]) * (x - C[0]) + (A[0] - C[0]) * (z - C[2])) / d;
          if (u >= -1e-7 && v >= -1e-7 && u + v <= 1 + 1e-7)
            return u * A[1] + v * B[1] + (1 - u - v) * C[1];
        }
      return null;
    };
    const center = (arm) => arm.right.map((v, k) => (v + arm.left[k]) / 2),
      A = center(a),
      B = center(b),
      steps = Math.ceil(Math.hypot(A[0] - B[0], A[2] - B[2]));
    const paths = [];
    for (const off of [-2.3675, -0.9325, 0.9325, 2.3675]) {
      const points = [];
      for (let i = 0; i <= steps; i++) {
        const t = i / steps,
          x = A[0] + (B[0] - A[0]) * t - a.dz * off,
          z = A[2] + (B[2] - A[2]) * t + a.dx * off,
          y = height(x, z);
        if (y === null) {
          points.length = 0;
          break;
        }
        points.push([x, y + 0.003, z]);
      }
      if (points.length) paths.push(points);
    }
    if (paths.length === 4)
      links.push({ node: j.id, roads: [a.e.id, b.e.id], paths });
  }
  return links;
}

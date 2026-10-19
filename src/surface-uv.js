// Material coordinates are per face corner, not per welded position. This keeps
// the quad topology welded while allowing UV seams at junction/material borders.
export const tileSize = (material) => (material === "brick" ? 1.92 : 1.2);
const distance = (a, b) => Math.hypot(...a.map((v, i) => v - b[i]));
export function ribbonUVRows(
  rows,
  { anchor = 5, material = "paving", reverse = false } = {},
) {
  const lengths = [0];
  for (let i = 1; i < rows.length; i++)
    lengths.push(
      lengths[i - 1] + distance(rows[i - 1][anchor], rows[i][anchor]),
    );
  const period = tileSize(material),
    total = lengths.at(-1),
    repeats = Math.max(1, Math.round(total / period));
  return rows.map((row, i) => {
    const across = [0];
    for (let j = 1; j < row.length; j++)
      across.push(across[j - 1] + distance(row[j - 1], row[j]));
    // Whole repeats at both mouths align slab joints with the adjoining strip.
    // Reference-rail arc length makes the joints fan around curves, not world X/Z.
    const s =
      total > 1e-10
        ? (lengths[i] / total) * repeats * period * (reverse ? -1 : 1)
        : 0;
    return row.map((_, j) => [s, across[j] - across[anchor]]);
  });
}
export function planarFaceUV(points) {
  const a = points[0],
    b = points[1],
    c = points[2],
    u = b.map((v, i) => v - a[i]),
    v = c.map((v, i) => v - a[i]);
  const n = [
    u[1] * v[2] - u[2] * v[1],
    u[2] * v[0] - u[0] * v[2],
    u[0] * v[1] - u[1] * v[0],
  ].map(Math.abs);
  const axes =
    n[1] >= n[0] && n[1] >= n[2] ? [0, 2] : n[0] >= n[2] ? [2, 1] : [0, 1];
  return points.map((p) => axes.map((k) => p[k]));
}
export function normalizedFaceUV(mesh, i) {
  const coords =
      mesh.uvs?.[i] ?? planarFaceUV(mesh.faces[i].map((v) => mesh.vertices[v])),
    size = tileSize(mesh.groups[i]);
  return coords.map((uv) => uv.map((v) => v / size));
}
export function objTextureCoordinates(mesh) {
  return (
    mesh.faces
      .map((_, i) =>
        normalizedFaceUV(mesh, i)
          .map((uv) => `vt ${uv.map((v) => v.toFixed(7)).join(" ")}`)
          .join("\n"),
      )
      .join("\n") + "\n"
  );
}

# Tyre quad mesh

`quad-mesh.mjs` builds a tyre surface as one structured **pure-quad** grid:
the cross-section (bead → sidewall → shoulder → tread → shoulder → sidewall → bead)
is revolved around the axis, giving N rows × M columns. Columns wrap around the
circumference, so there are no seams, no triangles, and no T-junctions.
Tread detail is applied as a per-vertex displacement along the profile normal.

Why this fixes the "holes": the current polygon-tread path triangulates each
groove region separately and stitches it to the sidewall mesh, which leaves cracks
where regions meet. A single grid has no such seams.

## API
- `buildTyreQuadMesh({ profile, cols, displace })` → `{ positions, normals, uvs, quads, rows, cols }`
- `validateQuadMesh(mesh)` → manifold / winding / boundary-loop / Euler report
- `quadsToTriangles(quads)` for renderers that need triangle indices
- `computeVertexNormals(positions, quads)`

## Test
    node --test tyre/quad-mesh.test.mjs

## Not done yet
- 2D tread-pattern editor (next stage)
- General quad remeshing of an arbitrary triangle mesh (ZRemesher-style); this grid covers the tyre case, which is a surface of revolution
- Integration with `TyreGenerator.html` (lives in the `unassignedinbox/Slate` repo, not this one)

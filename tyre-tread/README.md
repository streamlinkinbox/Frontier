# Tread editor and pure-quad tyre mesh

Two parts, both plain ES modules with no build step:

1. **2D tread editor** (`index.html`, `src/editor/`). Vector shapes in mm: grooves (polylines with width), filled voids, and dimples. Pitch, mirror, snap, undo/redo, JSON save/load, presets, OBJ export.
2. **Pure-quad tyre generator** (`src/mesh/quadTyre.js`). A structured quad torus built by sweeping one closed cross-section loop around the axis. The tread pattern is applied as vertex displacement, evaluated analytically at each vertex. No heightmap, no texture, no triangles, no open beads.

## Why the old mesh had holes

The reference generator (`Slate/References/TyreGenerator.html`) builds each tread piece as a Clipper polygon and triangulates it with `ShapeUtils.triangulateShape`, adding Steiner points. Walls are emitted separately, and the profile is open at the beads. The result has open boundaries, T-junctions, and triangle fans wherever a groove edge crosses the grid.

The generator here avoids that. The cross-section is a single closed loop (tread centre → shoulder → sidewall → bead → inner liner → centre → mirror), so the mesh is a closed torus by construction. Every face is `(i, j) → (i+1, j) → (i+1, j+1) → (i, j+1)`.

## Topology guarantees (checked by `tests/tyre_tread.test.mjs`)

- Quads only, zero triangles.
- No boundary edges, no non-manifold edges, no degenerate faces.
- Consistent orientation (no directed edge used twice).
- Euler characteristic V − E + F = 0 (torus).
- Enclosed volume positive, so faces point outward.
- Cross-section loop is simple (no self-intersections).
- Groove walls sit on mesh edges: floor vertices get full depth, wall-edge vertices half, land zero.

The editor shows the same checks live in its Mesh panel.

## Poly budget

Grid lines are placed on the pattern's own breakpoints (groove and sipe edges, corners), plus a small uniform base grid. Quads = around lines × cross-section loop segments.

| Budget | Smooth tyre | Demo pattern (channels + sipes) | Notes |
|--------|------------:|--------------------------------:|-------|
| low    | 384 quads   | 4,416 quads                     | Default. Smooth volume within 2.9 % of the fine reference. |
| medium | ~1,800      | ~7,000                          | For checking whether the pattern needs more lines. |

The pattern cost comes from the number of repeats around the tyre times the features per repeat. A structured grid needs a full ring of lines for each feature, so a 240 mm pitch (eight repeats) costs about eight times one repeat. Dimple patterns are the worst case, since round features need several lines each.

Diagonal features (chevrons) are represented by their corner vertices and the base grid, not by their full length. Thin sipes cost four lines per repeat. Going lower than the low budget breaks the shape: a coarse inner liner (stride 6) put the smooth-tyre volume 218 % off, which is why it's not used.

## Not included yet

- **Remeshing arbitrary imported triangle meshes** (ZRemesher-style). This build generates clean quads procedurally. A general quad remesher is a separate, larger project (field-aligned parameterisation or a voxel/ray projection for tyre-like surfaces). It should be scoped before building.
- Integration into the Slate `TyreGenerator.html`. That file lives on branch `arena/01a0d364-slate`, which this session cannot push to.

## Running

```
cd tyre-tread
node --test tests/tyre_tread.test.mjs      # topology and pattern tests (no browser needed)
python3 -m http.server 8080 --bind 0.0.0.0 # then open index.html
```

three.js is loaded from jsDelivr (`three@0.160.0`) through the import map in `index.html`. If it cannot load, the 2D editor and OBJ export still work, and the 3D panel shows a message.

## Files

- `src/tread/pattern.js`: shape model, `depthAt`, `patternBreakpoints`, JSON, validation.
- `src/tread/presets.js`: three starter patterns.
- `src/mesh/quadTyre.js`: cross-section loop, pattern-aligned quad grid, `TYRE_DEFAULTS` (205/55 R16 class, metres), `BUDGET` (low, medium, fine reference).
- `src/mesh/quadCheck.js`: topology report and signed volume.
- `src/io/obj.js`: quad-only OBJ writer.
- `src/editor/app.js`, `view3d.js`, `index.html`: editor UI and 3D preview.
- `tests/tyre_tread.test.mjs`: 16 Node tests.

# Phase 19 plan — mirror operations

**Status:** design locked. Implementation phase next.

## Scope

Mirror / pattern / radial / empty. Blender + Plasticity parity, 2-word naming, 19 role suffix.

### Verbs

- `mirror <fig...|selected> [--across=<plane|axis>] [--also=<plane|axis>]... [--copy|--in-place] [--name=<stem>]`
  - Default: copy mode, name `Mirror.<src>`, across XY workplane.
  - Plane: workplane name (`xy`, `xz`, `yz`) or named plane (`P_top`, `P_aux`, etc.).
  - Axis: `(origin,dir)` — a 3D line. E.g. `--across=((0,0,0),(0,0,1))` mirrors across the world Z axis.
  - Multi-axis: `--across=XY --also=XZ` produces 4 copies (one per quadrant).
  - `--in-place` mutates the source instead of copying.
  - `--name=<stem>` overrides the `Mirror.` prefix.
- `radial <fig...|selected> --count=N --axis=((ox,oy,oz),(dx,dy,dz)) [--angle=deg=360] [--name=<stem>]`
  - Source + (N-1) copies, evenly spaced around the axis. Angle 360 = full circle.
  - Default: 2 copies, 360° (one at source + one diametrically opposite).
  - `angle=180 count=4` = 4 copies at 0°, 45°, 90°, 135° around the axis.
- `empty --name=E --at=(x,y,z) [--rotation=(rx,ry,rz)] [--scale=(sx,sy,sz)]`
  - Create a transform handle. No geometry; a position/orientation marker.
- `list empty` — list all empties.
- `delete empty <name>|all` — remove empties.

### Math

- **Plane mirror**: `P' = P - 2 * n * dot(P - O, n)`. O = plane origin, n = unit normal.
- **Axis mirror**: decompose `P - O` into parallel + perpendicular to the line direction `d`. Flip the perpendicular: `P' = P - 2 * perpendicular`. Equivalent: `P' = 2 * (O + parallel) - P` where `parallel = ((P-O)·d̂) d̂`.
- **Multi-axis**: apply each mirror in order. For two planes with non-parallel normals, the composition is a 180° rotation around the intersection line. The result is the same shape as applying each mirror once.
- **Radial**: rotate `P` around the axis by `θ = k * step` (k = 0..N-1), where `step = angle / N`. Rodrigues' formula: `P' = O + parallel + (cos θ * perpendicular) + (sin θ * d̂ × parallel)`.

### Blueprint reflection

For each Figure form, read every position-bearing Blueprint cell (A, B, C, Axis, Normal, PolylinePoints), reflect each, write back, then call the per-Form builder to reconstruct the Curve or Body. Same pattern as Phase 16 (`ApplyLiveEdit`) and Phase 18 (`RebuildFigureFromBlueprint`).

- Line: reflect A, B → rebuild.
- Polyline: reflect each PolylinePoints[K] → rebuild.
- Circle: reflect A (centre); reflect Normal; recompute R0 from the reflected centre and a reflected radius-point (we don't store the radius-point, so R0 stays the same).
- Arc: same as Circle + reflect the sweep start angle.
- Ellipse: reflect A, MajorDirection; R0, R1 stay the same.
- Rectangle: reflect A, B (opposite corners). R3 (corner radius) and R0 (rotation) stay.
- Bodies (Box, Sphere, Cylinder, Cone, Torus): reflect A, B, C, Axis, Normal; rebuild.

### Empty objects

A new figure classification: `FigureClassification::Empty`. The Curve and Body are both empty. Auto-emit a small axis cross at the Empty's position (3 short coloured lines, one per world axis) for visibility. The Empty's transform is stored as a Blueprint. Mirror / radial operate on empties by transforming the (x, y, z) position.

### Files to create

- `Editor/EditorTools/ParametricSketcher/Kernel/MirrorSolver.h` — pure math: plane/axis reflection, multi-axis composition, radial rotation. Returns Vec3.
- `Editor/EditorTools/ParametricSketcher/Kernel/MirrorSolver.cpp` — implementations.
- `Editor/EditorTools/ParametricSketcher/Verification/MirrorVerification.cpp` — 30+ checks: each reflection formula on a known point set, multi-axis composition is a 180° rotation, radial covers the full circle, Blueprint round-trip preserves length/area, Empty create + list + delete round-trip, mirror copies a line / circle / rectangle, radial copies a body, in-place reflection works, across a custom named plane works, across an axis line works.
- `Editor/EditorTools/ParametricSketcher/Scripts/Phase19_Mirror.scr` — demo tiles: mirror a line across XY, mirror a box across a custom plane, multi-axis mirror of a polyline, radial pattern of a circle, in-place reflection of a polyline, mirror an empty, radial pattern of an empty.

### Files to modify

- `Editor/EditorTools/ParametricSketcher/Console/ConsoleHost.cpp` — add `mirror`, `radial`, `empty`, `list empty`, `delete empty` verbs.
- `Editor/EditorTools/ParametricSketcher/Console/ConsoleHost.h` — add helpers: `ReflectBlueprint(figure, plane)`, `ReflectFigureAcrossPlane(...)`, `RotateFigureAroundAxis(...)`.
- `Editor/EditorTools/ParametricSketcher/Document/SceneDocument.h` — extend `ParametricForm` enum with `Empty`; extend `FigureClassification` with `Empty`.
- `Editor/EditorTools/ParametricSketcher/Kernel/CurveSpecification.h` / `.cpp` — `NurbsCurve::Empty(Pos)` (no-op curve used as a placeholder; the actual visible geometry is drawn by the renderer as 3 short lines).
- `Editor/EditorTools/ParametricSketcher/Presentation/ScenePresentation.cpp` — draw an Empty as a small axis cross at its position.
- `Editor/EditorTools/ParametricSketcher/CMakeLists.txt` — register `MirrorVerification`.
- `Editor/EditorTools/ParametricSketcher/Verification/SuiteVerification.cpp` — register `Phase19_Mirror.scr` (+2 checks).
- `Editor/EditorTools/ParametricSketcher/README.md` — Phase 19 row + new totals (850 → ~885).
- `Scratchpad/BuildSolidArc.sh` — add `Kernel/MirrorSolver.cpp` to the source list.

### Verification plan

Direct math (~12 checks):
- Plane reflection of a known point: `(1, 2, 3)` across XY = `(1, 2, -3)`. Across XZ = `(1, -2, 3)`. Across YZ = `(-1, 2, 3)`.
- Axis reflection across the Z axis: `(3, 4, 5)` → `(-3, -4, 5)`. Across a tilted axis through `(1,1,0)` direction `(1,0,0)`: `(2,1,0)` → `(0,1,0)`.
- Multi-axis composition: two orthogonal planes → 180° rotation around the intersection line. Verify by reflecting `(1, 2, 3)` across XY then XZ, getting `(1, -2, -3)`. Compare to a known 180° rotation result.
- Radial: rotating `(1, 0, 0)` around the Z axis by 90° → `(0, 1, 0)`. By 360° → `(1, 0, 0)`. 4 copies of a circle around an axis produce 4 distinct positions.

Host integration (~18 checks):
- `empty --name=E --at=(1,2,3)` creates an empty; `list empty` shows it; `delete empty E` removes it.
- `mirror L1 --across=XY` produces a `Mirror.L1` whose endpoints are `(start.x, start.y, -start.z)`. (Take a line in 3D.)
- `mirror L1 --in-place` mutates the source.
- `mirror L1 --across=P_top` works (across a named plane).
- `mirror L1 --across=((0,0,0),(1,0,0))` works (across an axis).
- `radial L1 --count=4 --axis=((0,0,0),(0,0,1)) --angle=360` produces 4 lines at 0°, 90°, 180°, 270° around the Z axis.
- `radial Box1 --count=6 --axis=((0,0,0),(0,0,1)) --angle=360` produces 6 boxes in a hex pattern.
- `mirror C1 --also=XY --also=XZ` (where C1 is on a custom plane) produces 4 copies.
- Blueprint round-trip: after mirror, the figure's length is preserved; after radial, the bounding box is centred on the axis.
- Render: a mirrored body renders to a non-empty PNG.

### Demo script tiles

1. **Mirror a line across XY**: line `(1, 2, 3)` to `(4, 5, 6)` mirrored across XY gives `Mirror.L` with endpoints `(1, 2, -3)` to `(4, 5, -6)`. Render: shows the two lines (original + mirror) reflected about the XY plane.
2. **Mirror a box across a custom plane**: define a plane `P_diag` with normal `(1, 1, 0).Normalised()`, then mirror a box across it. Render: shows the box and its reflection.
3. **Multi-axis mirror of a polyline**: polyline with 4 points, mirror across XY + XZ. Produces 4 copies in a 2x2 grid. Render.
4. **Radial pattern of a circle**: circle at `(1, 0, 0)` radius 0.2, radial 6 copies around the Z axis. Render: hex of circles.
5. **In-place reflection of a polyline**: polyline starts above XY, `--in-place --across=XY` flips it below XY. Render: only one figure, now below.
6. **Mirror an empty + radial of an empty**: create an empty at `(1, 0, 0)`, mirror across XY to get a second at `(1, 0, -0)` (same XY), radial 4 copies around Z. Render: 5 axis crosses.

Total 6 tiles, 6 PNGs.

### Risks

- **Reflection of a closed periodic curve** (circle, ellipse): the closed-periodic Knots + the pole order both need to be preserved. For a plain mirror, the pole positions just get reflected; the parameterisation (which pole is first) doesn't change. Verification: after mirror of a circle, the centre is reflected but the radius is unchanged, and the curve is still classified as Circle with the same knot count.
- **Axis reflection with negative determinant**: for a line mirror (axis), the reflection has determinant -1 in 3D. For a plane mirror, also -1. For a 180° rotation, +1. We use Rodrigues' formula, so no matrix inversion or determinant issues.
- **Multi-axis composition**: applying XY then XZ mirror is the same as a 180° rotation around the X axis. We don't pre-compose the mirrors; we just apply them in order. The verification checks that the composition is geometrically correct.
- **Empty without geometry**: SceneFigure requires either Curve or Body. Empty is a degenerate case — both are present but the Curve is a zero-length line and the Body is empty. The renderer draws the axis cross using a special path.
- **Blueprint.Normal for circles / ellipses**: reflection flips the normal's sign. The Circle builder uses `Normal.Normalised()`, so a sign flip is fine.
- **Named planes from `plane --name=`**: the workplane store is `std::map<std::string, Workplane>`. Looking up by name should work. If a name is not found, refuse with a clear error.
- **Empty rotation / scale**: empties have a transform (R * S * T). For Phase 19, only T (position) is required; rotation / scale on empties can be Phase 19b.

### Acceptance criteria

- `MirrorVerification` has 30+ checks, 0 failed.
- `SuiteVerification` has 61 checks (+2: Phase 19 script runs without refusal and produces 6 PNGs).
- Total: 850 → ~885 checks.
- 6 new proof PNGs in `Proofs/Phase19_*.png`.
- README Phase 19 row + new totals.
- All existing 14 verifications + Suite still pass.
- Build via `Scratchpad/BuildSolidArc.sh`; no warnings introduced.
- `git commit` + `git push` to `arena/01a07bb0-frontier`.

### Order of work

1. Create `Kernel/MirrorSolver.h` + `.cpp` — pure math, no host dependency.
2. Add `FigureClassification::Empty` to SceneDocument.h + `ParametricForm::Empty`.
3. Add `NurbsCurve::Empty()` (a zero-length placeholder).
4. Add helpers in `ConsoleHost`: `ReflectBlueprint(figure, plane)`, `RotateBlueprint(figure, axis, angle)`, `MirrorFigure(...)`, `RadialFigure(...)`, `EmptyFigure(...)`.
5. Add verbs: `mirror`, `radial`, `empty`, `list empty`, `delete empty`.
6. Update `ScenePresentation.cpp` to draw Empties as axis crosses.
7. Update `Scratchpad/BuildSolidArc.sh` to include `Kernel/MirrorSolver.cpp`.
8. Write `Verification/MirrorVerification.cpp` (~30 checks).
9. Write `Scripts/Phase19_Mirror.scr` (6 tiles).
10. Register in CMakeLists + SuiteVerification (+2).
11. Update README.
12. Build + run all 16 verifications + SuiteVerification.
13. git commit + push.

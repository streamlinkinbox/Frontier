# Phase 17: sub-entity dimensions + leader lines + dimension anchors on faces/edges

## Goal

Right now, `dim edit` only works on auto-emitted dims that point at primitive-level
slots (Box dims, Cylinder radius, Pipe radius, etc.). The user cannot:

1. Click a face or edge and get a dim anchored to *that* sub-entity.
2. Place a leader (a free-floating label + line from a feature to a label position).
3. Get a "click on a face, get its area" or "click on an edge, get its length" dim.

Phase 17 wires all three. It is the missing piece for Plasticity-style
"select a thing, see its measurement" workflow.

## Existing pieces we can lean on

- `SceneFigure::SelectedFaces` / `SelectedEdges` already exist (Phase 4).
  `select B face 3` and `select B edge 12` populate them via `ConsoleSelection`.
- `BrepBody::EdgePolyline(int Edge)` returns the polyline of an edge for picking.
- `BrepBody::FaceNormal(int Face, double U, double V)` returns the outward normal.
- `BrepBody::FaceTriangles(int Face)` tessellates a face — we can use it to compute
  face area and centroid.
- `BrepEdge::Curve.Classification` and `Curve.Length()` give us edge length + type.
- `DimensionEntry` already has `Anchor` (figure id) and an `A`, `B`, `N` triple
  for endpoints + normal. We add: `AnchorFace` (int, -1 if whole figure),
  `AnchorEdge` (int, -1 if whole figure), `Leader` (bool, marks a leader-style dim).

## What we add

### 1. `dim face <figure> <f> [--leader=(x,y,z)] [--along=U|V|N]`

Emits a dim tied to face F of the figure. The default placement is a
face-normal offset around the face's bounding-box perimeter, lifted off the
face by 4 cm. `--leader=(lx,ly,lz)` switches the dim to a leader: line
from face centroid to label position.

- Emits a "face area" dim with value = `BrepBody::Area() / F`'s piece (we
  compute via tessellation).
- Emits a "face perimeter" dim with value = sum of edge lengths around the
  outer + hole loops of F.
- Both are read-only measurements (Slot = -1) by default. For a face area
  to be live-editable we'd need a parametric face, which most faces don't
  have. We document this and move on.

### 2. `dim edge <figure> <e> [--leader=(x,y,z)]`

Emits a dim tied to edge E of the figure.

- Emits a "edge length" dim with value = `BrepEdge::Curve.Length()`.
- Emits a "edge radius" dim if the curve is a Circle or Arc — `Curve.RadiusMajor`.
- Slot = -1 (read-only). Edge length can't be live-edited without a
  parametric edge source, which we don't record.

### 3. `dim leader <figure> <p> <label>`

Emits a leader: a line from point `p` on the figure to a free-floating
label position. The user types the label text (e.g. "M6 hole"). The label
shows in the dim overlay.

### 4. `dim sub <figure>` — auto-emit per-face + per-edge dims for the figure

Convenience verb: iterates every face and every edge, emits one dim per
sub-entity. Useful for dense inspection of a complex body.

### 5. Renderer additions

The dim renderer (DrawDimensions) needs to handle:

- **Leader dims**: a line from `D.A` (feature) to `D.B` (label position),
  a small dot at `D.A`, and a free-floating label at `D.B`.
- **Sub-entity dims**: same draw as today, but the dim anchor is the
  sub-entity (face centroid, edge midpoint) instead of the bbox.

We add a `Leader` bool + a `Label` (free-form text, not a measurement) to
`DimensionEntry`. The existing label code already accepts a `Label` string
and just renders it. The leader renderer is a thin extension of the
existing dim renderer.

### 6. Live-edit on sub-entity dims

`ApplyLiveEdit` checks `D.Slot >= 0`. Sub-entity dims have `Slot = -1` by
default (read-only), so the existing label-override path handles them.

For the live-edit of leader label text, we add `D.Label = "M6 hole"` on
edit — same as the existing label-override path.

### 7. Verification

`SubEntityDimensionVerification.cpp`:
- `box (0,0,0) (2,3,4)` → `dim B face 0` emits 2 dims (area, perimeter).
- `box (0,0,0) (2,3,4)` → `dim B edge 0` emits 1 dim (length = 3).
- `cylinder (0,0,0) 1 2` → `dim C face 0` emits a face area dim.
- `dim B face 0 --leader=(3,2,0)` switches the dim to a leader and the
  leader endpoint is `(3,2,0)`.
- `dim leader B (0.5,0.5,0) M6 hole` adds a leader with text "M6 hole".
- 10+ new checks.

`Phase17_SubEntityDims.scr`:
- Build a box, switch to face select mode, click a face, then add a
  face dim and a leader. Render the result. Repeat for an edge.

### 8. README + SuiteVerification registration

Update the README table to add Phase 17 row, and add the new script to
`SuiteVerification`'s scripts list.

## Slot encoding (no change)

The existing slot encoding (0..17 scalars + 18+ for polyline) is enough.
Sub-entity dims use Slot = -1 (read-only), so they don't need a slot.

## Risks

1. **Face area math**: `BrepBody::Area()` is the total body area, not per-face.
   We tessellate the face and sum triangle areas. Acceptable.
2. **Edge length with `EdgePolyline`** uses a chord tolerance of 2 mm, so
   the length is approximated. For straight lines and arcs the chord
   approximation is exact (curves are linear/circular). For freeform edges
   the error is < 2 mm per chord segment. Document and accept.
3. **Leader text "label-override" path**: when the user types
   `dim edit <id> 1.5` on a leader, the existing path treats it as a
   measurement edit. The label itself is overridden only if the dim is
   non-live (Slot = -1). We add a path so that `dim edit <id> --label <text>`
   changes the label text. Or: the leader text is set at creation and not
   editable via `dim edit` — the user deletes the leader and re-creates.
   Simpler. We go with the simpler.

## File list (what we touch)

- `Editor/EditorTools/ParametricSketcher/Console/ConsoleHost.h` — add 2 fields to
  DimensionEntry (`AnchorFace`, `AnchorEdge`, `Leader`).
- `Editor/EditorTools/ParametricSketcher/Console/ConsoleHost.cpp` — extend `dim` verb with
  the new subcommands and the leader render path in `DrawDimensions`.
- `Editor/EditorTools/ParametricSketcher/Verification/SubEntityDimensionVerification.cpp` — new.
- `Editor/EditorTools/ParametricSketcher/CMakeLists.txt` — register the new verification.
- `Editor/EditorTools/ParametricSketcher/Scripts/Phase17_SubEntityDims.scr` — new.
- `Editor/EditorTools/ParametricSketcher/Verification/SuiteVerification.cpp` — add the new
  script to the smoke-test list.
- `Editor/EditorTools/ParametricSketcher/README.md` — add Phase 17 row.

# RoadWorks Editor

Procedural **road network and bridge** authoring in the browser. Draw corridors, raise them into bridges, and the
solver merges every intersection it finds — with curvature-correct curbs and pavements, and a fully parametric
bridge kit underneath the deck.

No build step, no package install, no CDN. Open `index.html` directly, or straight from the repository:

**<https://raw.githack.com/streamlinkinbox/Frontier/arena/cd280459-frontier/RoadWorksEditor/index.html>**

---

## What it does

| Area | Behaviour |
| --- | --- |
| Topology | Corridors are sampled, split at every planar crossing, welded into shared junction nodes and trimmed by a radius derived from the widths and angular gaps meeting there. |
| Junctions | Fillet arcs between neighbouring arms, Coons-patch aprons, and curb / pavement bands that wrap every corner. |
| Grade separation | Crossings separated vertically are *not* merged — they stay as overpasses for the bridge generator. |
| Cross-section | Carriageway with camber, three-face curb (gutter → face → top), pavement with crossfall, outer drop, lane markings. |
| Bridges | Cantilevered deck with inset soffit, six superstructure families, four pier families, three railing families, abutments. |
| Output | Wavefront OBJ (Y-up, metres) and a `.roadworks.json` document you can reload. |

### Bridge catalogue

* **Superstructure** — beam / girder, box girder, deck arch, Warren truss, suspension, cable-stayed
* **Substructure** — wall pier, twin column, hammerhead, V-pier, or abutments only
* **Furniture** — concrete parapet, steel rail, Jersey barrier, none

Every member is generated from a small swept-box / tube kit, so adding a new superstructure or pier family is a
recipe, not a new mesh pipeline.

## Controls

| Input | Action |
| --- | --- |
| Drag | Orbit |
| Right-drag / Alt-drag | Pan |
| Scroll | Zoom |
| Click a handle, then drag | Move a control point (axis + XY-plane gizmo) |
| Shift-click a corridor ribbon | Insert a control point there |
| `Delete` | Remove the selected control point (or the corridor, if only two remain) |
| `1` / `2` / `3` | Select · Draw road · Draw bridge |
| Click (draw mode), `Enter` | Place points · finish the corridor (`Esc` cancels) |
| `F` | Frame the network |

## Layout

```
index.html              static entry (import map → vendored three.js)
styles/Workspace.css    Frontier experimental editor theme
lib/three.module.min.js vendored renderer (MIT) so raw hosting works offline
src/
  Vec.js                vector maths
  Polyline.js           arc-length utilities, frames, miter correction
  Spline.js             centripetal Catmull-Rom corridor splines
  Graph.js              crossing detection, node welding, edge extraction, corner radii
  RoadMesh.js           cross-sections, swept corridor mesh, markings
  JunctionMesh.js       fillets, offset bands, Coons aprons
  BridgeMesh.js         deck, superstructure families, piers, railings
  MeshSpec.js           triangle buffer, welded normals, OBJ export
  Network.js            corridors → graph → material groups
  Viewport.js           three.js scene, lighting, overlay, picking
  Gizmo.js              translate gizmo
  App.js                document state and UI
tools/
  smoke-test.mjs        headless generator checks  (node tools/smoke-test.mjs)
  preview-render.mjs    headless PNG rasteriser     (node tools/preview-render.mjs out.png iso|top|side)
```

Everything under `src/` except `Viewport.js`, `Gizmo.js` and `App.js` is renderer-agnostic and runs in plain Node,
which is what the two tools in `tools/` use.

## Design notes

The junction topology follows the approach proven in the Unreal **TransitArchitect** plugin (sample → split →
weld → trim → fillet), re-implemented here in JavaScript. The parts of the earlier browser prototype that were
unreliable were *not* carried over; they were replaced:

* **Curve handling.** Raw Bézier handles could cross, cusp and invert tangents. Corridors now store control points
  plus one tension value, and tangents come from the centripetal Catmull-Rom parameterisation, which cannot cusp
  inside a span.
* **Curbs and pavements on curves.** Straight perpendicular offsets undershoot the true parallel curve, which is
  what made curbs ripple and pavements self-intersect. Every lateral offset is now scaled by the miter factor
  `1 / cos(θ/2)` of the local turn and clamped against the local curvature radius, so an inner curb can never fold
  through the centreline.
* **Junction seams.** Junction approach frames are taken *from* the trimmed corridor cross-sections instead of being
  recomputed, so the junction ring and the corridor end ring share identical vertices.
* **Gizmo drift.** The grab offset is recorded once on pointer-down and each move solves a single ray/plane (or
  ray/line) intersection, so dragging is exact and frame-rate independent.
* **Bridges.** The deck is a cantilevered slab with a chamfered fascia and inset soffit rather than an extruded
  slab, and everything below it is a parametric family rather than one hard-coded shape.

## Verification

```bash
node tools/smoke-test.mjs                      # topology, geometry hygiene, every bridge/pier/railing combination
node tools/preview-render.mjs preview.png iso  # software-rendered PNG of the demo network
```

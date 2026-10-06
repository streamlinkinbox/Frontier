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
| Paving | Nine procedural paving patterns drawn to canvas at runtime (colour + derived normal map), tiled in metres and selectable per corridor, with a paving-width and paver-scale control. |
| Roadbed | Anything above ground gets a real underside: earth embankment, board-marked retaining wall, slab soffit, or auto (fill until it exceeds `maxFill`, then wall). Elevated junction aprons get the same treatment, and bridge approaches are filled rather than spanned. |
| Signage | Stop or yield signs on every arm of a 3+ way junction, stop bars on the approaching half, and zebra crossings — all positioned from the junction's own approach frames. |
| Guardrails | Six roadside restraint systems swept as real cross-sections — W-beam, thrie-beam, wire rope, Jersey, parapet, tubular handrail — either along the whole corridor or only where the embankment exceeds a trigger height. |
| Bridges | Cantilevered deck with inset soffit, eleven superstructure families, four pier families, three railing families, bearing pads, abutments. |
| Output | Wavefront OBJ (Y-up, metres) and a `.roadworks.json` document you can reload. |

### Bridge catalogue

* **Superstructure** — beam / girder, box girder, solid slab (haunched over supports), haunched balanced
  cantilever (parabolic soffit, casting-segment ribs), deck arch, tied bowstring arch (hangers + crown bracing),
  masonry viaduct (segmental barrels, spandrel walls, voussoir rings, string course), Warren truss, Pratt through
  truss (portal + sway bracing, floor beams), suspension, cable-stayed
* **Substructure** — wall pier, twin column, hammerhead, V-pier, or abutments only
* **Furniture** — concrete parapet, steel rail, Jersey barrier, none
* **Bearings** — elastomeric pads under each girder line at every support, for the families that sit on them

Structures that span their whole opening — tied arch, through truss, masonry arcade — suppress the generic
intermediate piers, because a column under a self-supporting span is nonsense.

Every member is generated from a small swept-box / tube kit, so adding a new superstructure or pier family is a
recipe, not a new mesh pipeline.

## Controls

| Input | Action |
| --- | --- |
| Right-drag | Mouse-look (Unreal-style flight) |
| `W` `A` `S` `D` | Fly forward / left / back / right |
| `Q` / `E` | Drop / rise |
| `Shift` / `Ctrl` | Sprint ×3.2 / crawl ×0.25 |
| Scroll while right-dragging | Trim flight speed |
| Drag | Orbit |
| Middle-drag / Alt-drag / Space-drag | Pan |
| Scroll | Zoom |
| Click a junction hub, then drag | Move the whole intersection — every arm together |
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
  Roadbed.js            embankment / retaining wall / slab soffit under anything above grade
  Signs.js              stop + yield signs, stop bars, zebra crossings
  Guardrail.js          roadside restraint systems: corrugated beams, wire rope, concrete, handrail
  Ray.js                closest-point solves for the gizmo, kept pure so they can be unit-tested
  Textures.js           runtime canvas paving, asphalt, concrete, earth and sign textures
  RoadMesh.js           cross-sections, swept corridor mesh, markings
  JunctionMesh.js       fillets, offset bands, Coons aprons
  BridgeMesh.js         deck, superstructure families, piers, railings
  MeshSpec.js           triangle buffer, welded normals, OBJ export
  Network.js            corridors → graph → material groups
  Viewport.js           three.js scene, lighting, overlay, picking
  Gizmo.js              translate gizmo
  App.js                document state and UI
tools/
  smoke-test.mjs        headless generator checks   (node tools/smoke-test.mjs)
  boot-test.mjs         boots the UI against stubbed DOM/three and drives it (node tools/boot-test.mjs)
  dom-stub.mjs          the DOM + three.js stubs used by boot-test
  preview-render.mjs    headless PNG rasteriser      (node tools/preview-render.mjs out.png iso|top|side)
```

Everything under `src/` except `Viewport.js`, `Gizmo.js`, `App.js` and the canvas half of `Textures.js` is
renderer-agnostic and runs in plain Node, which is what the tools in `tools/` use. `Textures.js` exports its
catalogue and UV tables as plain data, so the geometry modules can import it without ever touching a canvas.

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
* **Guardrails.** A barrier is placed from the same miter-scaled station frames as the curb, so it tracks a curve
  without clipping the pavement it stands on, and the default rule only rails the stretches that are actually on
  fill. Steel families are swept corrugated sections on posts at the real 1.905 m spacing with blockout spacers;
  concrete families are solid swept profiles.
* **Readable sign faces.** A viewer facing a sign plate has its `+left` axis on their *left*, so the texture `u`
  must run the other way — getting that backwards is what mirrored the STOP legend, and `smoke-test.mjs` now
  checks the sign of `grad u · (Z × n)` on every plate instead of trusting review.
* **Bridges.** The deck is a cantilevered slab with a chamfered fascia and inset soffit rather than an extruded
  slab, and everything below it is a parametric family rather than one hard-coded shape.

### Moving a junction

A junction is not a document object — it is an emergent property of the corridors that meet there — so "move the
intersection" has to be resolved back onto control points. Grabbing a junction hub collects every control point
inside its radius, and for any corridor that merely *passes through* (a crossing with no control point of its own) it
inserts one at the node first. The drag then applies a single rigid translation to all of them, which is why the
intersection moves as one body instead of tearing into separate arms. The selection is anchored to a position rather
than a node id, because node ids are derived from coordinates and change the instant the junction moves.

## Verification

```bash
node tools/smoke-test.mjs                      # topology, roadbeds, signage, paving, guardrails, gizmo maths, every bridge/pier/railing combination
node tools/boot-test.mjs                       # boots the editor shell, clicks the toolbar, drags, draws, exports
node tools/preview-render.mjs preview.png iso  # software-rendered PNG of the demo network
```

`boot-test.mjs` exists because a geometry test cannot catch shell bugs: the first build of this editor shipped a
module-evaluation-order fault that left the viewport stuck behind the loading spinner. The harness now evaluates
`App.js` end to end and fails if boot throws, if the spinner is still up, or if any handler errors.

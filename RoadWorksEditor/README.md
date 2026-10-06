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
| Surfaces | Seven running surfaces — asphalt, chip seal, concrete slab, stone setts, gravel, graded dirt and a rutted two-track — each with its own procedural texture, camber and shoulder. Unsealed presets (gravel road, farm track, forest road) drop the kerb, swap the footway for a loose shoulder and turn the lane paint off. |
| Lane markings | Turn arrows allocated from the movements each junction actually offers, chevron hatching, yellow box junctions, tinted cycle and bus lanes with painted glyphs, and kerbed pedestrian refuges with ghost-island approaches. |
| Roundabouts | Any junction can be switched to a roundabout: arms are trimmed to the outer kerb, the apron becomes the circulating carriageway, and a planted island, mountable truck apron, splitter islands and give-way teeth are laid over it. |
| Slip roads | A ramp drawn to the edge of a motorway is tapered onto its centreline, the fork is left unfilleted, and the wedge between the two carriageways is paved, edged and hatched as a proper gore with a painted nose. The taper is solved in the motorway's own frame — distance along, offset across — so it converges once and never swings back across the centreline, and both arms fold their verges away through the gore (a kerb there would run across the other carriageway) and pick them up again at the nose. |
| Driveways | Vehicle crossovers on any street: the kerb itself drops almost flush across the crossing and ramps back up over a flare at each end, the footway tips down into it, and a flared apron slab runs across the pavement to the property line. Spacing, width, depth and drop are per corridor, and opposite frontages are staggered. |
| Drainage | A working surface-water system under any corridor: gully gratings sunk in the gutter at the low point of the camber with the inlet slot cut into the kerb face above them, manhole covers alternating side to side and cambered to sit flush, a carrier pipe following the road's own long section a metre or so down, with laterals from every gully and a shaft up to every cover. The buried run is hidden until you tick **Buried drainage** in Display. |
| Deck drainage | A bridge cannot drain to a gully and a buried pipe, so a deck gets its own system: slotted scuppers in each gutter at a configurable spacing, taken through the slab and out past the fascia into downpipes that hang in daylight under the deck where you can actually see them. |
| City blocks | **Add block** drops in a whole residential grid — streets, footways, kerbs, dropped-kerb driveways onto every frontage and a drainage run beneath each street. Streets and paving only: the generator deliberately builds no buildings. |
| Vertical alignment | A long-section dock under the viewport (folded away by default — click its header or press `V`): chainage against elevation, draggable elevation handles, grade labels, and design checks for gradient, crest and sag K values and plan radius against a design speed. |
| Paving | Nine procedural paving patterns drawn to canvas at runtime (colour + derived normal map), tiled in metres and selectable per corridor, with a paving-width and paver-scale control. |
| Roadbed | Anything above ground gets a real underside: earth embankment, board-marked retaining wall, slab soffit, or auto (fill until it exceeds `maxFill`, then wall). Elevated junction aprons get the same treatment, and bridge approaches are filled rather than spanned. |
| Cuttings | The mirror image of the fill. Where a corridor runs below ground level the ground is taken away instead: a drainage ditch at each verge, a batter climbing back to daylight at `cutSlope`, and a board-marked retaining cut once the excavation is deeper than `maxCut`. Like the fill it starts and stops cleanly wherever the alignment crosses grade. |
| Signage | Stop or yield signs on every arm of a 3+ way junction, stop bars on the approaching half, and zebra crossings — all positioned from the junction's own approach frames. |
| Guardrails | Six roadside restraint systems swept as real cross-sections — W-beam, thrie-beam, wire rope, Jersey, parapet, tubular handrail — either along the whole corridor or only where the embankment exceeds a trigger height. |
| Bridges | Cantilevered deck with inset soffit, eleven superstructure families, four pier families, three railing families, bearing pads, abutments. |
| Editing modes | Select (roads and bridges), Points, Junctions, Draw road, Draw bridge — switchable from the toolbar or with `1`–`5`. |
| Multi-select | Shift-click or Shift-drag a marquee in Points / Junctions mode; the gizmo then moves the whole selection as one rigid body, intersections included. |
| Street names | Every corridor is named and labelled in the viewport; junctions borrow the names of the streets that meet there. Rename in place from the outliner or the inspector. |
| Default scene | Loads a worked network — a roundabout, a gravel track, a motorway slip road, an avenue carrying a cycle lane, a bus lane, refuge islands and a yellow box, and the Saltmarsh residential block with driveways and drainage — *plus* a gallery holding one span of every bridge family, so nothing has to be drawn to see what the generator does. |
| Performance | Per-corridor mesh cache: dragging one street reuses the meshes of every corridor whose cross-sections did not change. |
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
| `1` `2` `3` | Select · Points · Junctions mode |
| `4` `5` | Draw road · Draw bridge |
| Click a road | Select that stretch — the span between two junctions — and highlight it |
| Click a junction hub | Select the intersection and highlight its apron |
| Shift-click | Add to / remove from the selection |
| Shift-drag (Points / Junctions) | Marquee-select everything inside the rectangle |
| `Ctrl`+`A` | Select all (points, junctions or corridors, depending on the mode) |
| Double-click a name in the outliner | Rename the street in place |
| `L` | Show / hide street-name labels |
| `V` | Show / hide the vertical alignment dock |
| Drag a handle in the alignment dock | Change that control point's elevation |
| `F` | Frame the selection, or the whole network when nothing is selected |
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
| Shift-click a corridor ribbon (Select mode) | Insert a control point there |
| `Delete` | Remove the selected control points (or the corridor, if fewer than two would remain) |
| Click (draw mode), `Enter` | Place points · finish the corridor (`Esc` cancels) |

## Layout

```
index.html              static entry (import map → vendored three.js)
styles/Workspace.css    Frontier experimental editor theme
lib/three.module.min.js vendored renderer (MIT) so raw hosting works offline
src/
  Vec.js                vector maths
  Polyline.js           arc-length utilities, frames, miter correction
  Spline.js             centripetal Catmull-Rom corridor splines
  Graph.js              crossing detection, node welding, edge extraction, corner radii, slip-road attachment
  Surfaces.js           running-surface catalogue and the per-surface mesh groups
  Alignment.js          chainage / grade / K-value profiles and the design-standards tables
  Markings.js           turn arrows, hatching, yellow boxes, cycle + bus lanes, refuges
  Roundabout.js         central island, truck apron, splitter islands, give-way markings
  Driveways.js          dropped-kerb crossovers and apron slabs
  Drainage.js           gullies, kerb inlets, manhole covers, carrier pipe, deck scuppers and downpipes
  Merge.js              slip-road gores: the paved wedge, its edge lines, chevrons and nose
  Roadbed.js            embankment / retaining wall / slab soffit above grade, ditch + batter below it
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
  ProfileDock.js        the long-section canvas editor and its design-check panel
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

### A diverge is not an intersection

Treating a fork as a junction is what made every earlier attempt look broken. The fillet radius a 10° split asks for
is enormous, so both carriageways were pulled tens of metres back from the node and the hole between them was filled
with a blank apron: the motorway appeared to stop, lose its markings, and start again. Worse, the ramp and the
mainline both paved the same ground for the first stretch — two coplanar asphalt surfaces fighting over the same
pixels, which is the black wedge that sat at the fork.

`Network.markDiverges` recognises the case (a fork node where one minor arm leaves a through road at a shallow
angle) and takes it out of the junction system entirely: `nodeGeneratesJunction` returns false, so nothing is
trimmed and the mainline runs on untouched, markings and all. The ramp is then *lapped* into it. `lapForRamp`
intersects each of the ramp's cross-sections with the mainline's edges and hands `buildCrossSections` a per-station
limit on how far the ramp may reach, so while it is buried the ramp paves only the sliver that has actually emerged
past the mainline's edge, widening to its full carriageway exactly where the two part company. Over that stretch the
ramp has no outside of its own: no kerb, no footway, no barrier, no gullies and no paint. `Merge.js` then paves,
edges and hatches the gore from the nose outwards — and it knows to wait for the lap to open, because a lapped arm's
"edge" is the other road's edge and measuring a separation from it would start the gore back at the node.

### What a gore does to the cross-section

The hard part of a diverge is not the paving, it is the verge. For the first fifty-odd metres the ramp is still
lapped *inside* the motorway's footprint, so a kerb and footway on either road would be dragged straight across the
other's running lanes — which is what every earlier attempt here looked like. `Network.forkWindows` works out how
far the two have to run before they clear each other (separation grows as 2·sin(gap/2) per metre) and hands
`buildCrossSections` a per-side fade: the verge closes to nothing at the fork and eases back in at the nose. The
ramp, as the minor arm, folds both sides away; the motorway only the gore side. Junction corners that would wrap a
kerb onto a folded verge are skipped, and the painted nose is drawn at the upstream tip where the carriageways
actually part — not as a slab at the wide end.

### A dropped kerb is geometry, not a decal

A crossover is recognisable from the air because the kerb itself changes shape: it drops almost flush over the
width of the crossing, ramps back up over a short flare, and the footway tips down to meet it. So the crossing is
fed into the cross-section generator rather than painted on top of it — `Driveways.kerbDropFn` returns a height
factor per chainage and side, `buildCrossSections` applies it to the kerb faces while leaving the footway levels
alone, and the swept corridor comes out with real dropped crossings in it. The apron is then one continuous slab
from the gutter, across the footway, out to the property line, flared at the kerb so a car can actually turn in.

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

### Selecting a stretch of road

A corridor is a document object, but the thing you usually mean by "this road" is the stretch between two
intersections — which is a graph *edge*, created by the solver, not by you. Clicking the tarmac therefore resolves
the hit point back to the nearest edge and highlights exactly that edge's trimmed cross-sections: the same geometry
the mesher used, so the highlight ends precisely where the junction apron begins. Clicking an intersection
highlights the junction instead, at its own corner radius.

### Moving a junction

A junction is not a document object — it is an emergent property of the corridors that meet there — so "move the
intersection" has to be resolved back onto control points. Grabbing a junction hub collects every control point
inside its radius, and for any corridor that merely *passes through* (a crossing with no control point of its own) it
inserts one at the node first. The drag then applies a single rigid translation to all of them, which is why the
intersection moves as one body instead of tearing into separate arms. The selection is anchored to a position rather
than a node id, because node ids are derived from coordinates and change the instant the junction moves.

### Keeping a big scene interactive

The default document carries roughly 128 000 triangles, and every edit re-solves the whole network — topology is
global, so there is no honest way to rebuild one street in isolation. What *can* be reused is geometry: each graph
edge is hashed on its own trimmed cross-sections plus its profile, bridge, guardrail and roadbed settings, and an
unchanged hash means the cached mesh is appended instead of rebuilt. Dragging one control point in a 19-corridor
scene re-meshes two or three edges and copies the rest, which is roughly a 40 % saving on the solve; normals are
still welded across the merged groups afterwards, so shading continuity at junction mouths is unaffected.

### Why a slip road is not a junction

A fillet is the wrong shape for a fork. The radius needed to round off two arms meeting at 4° runs away towards
infinity, so the old corner-radius rule clamped out at its maximum and trimmed the motorway back into a stub —
the mainline looked like it simply ended. Shallow pairs are therefore skipped when the corner radius is sized, and
picked up by `Merge.js` instead: it walks both arms outwards from the node in step, measuring the *signed* offset
of the ramp's inner edge from the mainline's. While that offset is negative the ramp is still inside the mainline
and already paved; once it goes positive the wedge between them is lapped as carriageway, edged with solid lines,
filled with chevrons and closed with a painted nose at the gore width.

The other half of the problem is that junctions are born from centreline crossings, and nobody draws a ramp onto
the centreline of a motorway — they draw it to the edge, where it connects to nothing. So any dangling corridor end
that dies *on* another carriageway is extended, in the solver only, along a tapered curve that starts on the ramp's
own tangent and finishes running parallel to the mainline. The document keeps the points that were drawn; the long
shallow overlap that extension produces is exactly the acceleration lane a merge needs.

### Reading the long section

`Alignment.js` samples the corridor into stations carrying grade, vertical curvature and plan radius. Grades come
from a centred difference over an 8 m window so they report the road rather than the sampling noise; vertical
curves are described by K = L / A, recovered as `1 / (100·z'')`, with the sign of `z''` separating crests from sags;
plan radius is the Menger curvature of three consecutive points. `designChecks` reads those against a rounded
AASHTO/Austroads-style table for the chosen design speed and merges consecutive offending stations into one warning
per stretch, so a long steep hill reads as a single line rather than fifty.

## Verification

```bash
node tools/smoke-test.mjs                      # topology, roadbeds, signage, paving, guardrails, surfaces, markings, roundabouts, slip roads, driveways, drainage, alignment maths, gizmo maths, every bridge/pier/railing combination
node tools/boot-test.mjs                       # boots the shell: modes, multi-select, marquee, highlights, names, drags, draws, exports
node tools/preview-render.mjs preview.png iso  # software-rendered PNG of the demo network
```

`boot-test.mjs` exists because a geometry test cannot catch shell bugs: the first build of this editor shipped a
module-evaluation-order fault that left the viewport stuck behind the loading spinner. The harness now evaluates
`App.js` end to end and fails if boot throws, if the spinner is still up, or if any handler errors.

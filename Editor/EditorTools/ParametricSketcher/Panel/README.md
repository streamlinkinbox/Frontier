# SolidArc Panel — Outliner · Viewport · Inspector (HTML)

Single-file UI (`index.html`, no build step) for exercising SolidArc through a UI before the
C++ console host is bridged. Same visual language as the Celestial panel on GitHub Pages
(glass panels, Outfit / JetBrains Mono, tick-track sliders, presence grid, stat tiles) — but the
three panels are **separate floating cards**: Outliner (left), Viewport (centre), Inspector (right).

Run: `python3 -m http.server 8080` in this folder → http://localhost:8080/

| Panel | What works today (JS model) | Hook-up point for the C++ host |
|---|---|---|
| Outliner | groups by figure kind (Sketches → curves, Bodies, Surfaces, Construction, Dimensions), search, kind filter chips, visibility eye, multi-select (⇧), status tiles, undo counter | `doc.figures` ← `SceneDocument` listing |
| Viewport | software raster (Z-up orbit camera, ortho/persp, canonical views), wire/flat/plastic/matcap, lattice, dimension lines, GizmoPRO stub, axis triad, prompt bar for modal tools (L/C/R/B/Y), pick, body/face/edge/vertex mode | replace `draw()` with the PNG/stream from `SoftwareRaster`; forward pointer/keys as `InputEvent` |
| Inspector | hero card (volume/area/length + B-rep stats), presence grid (Visible/Locked/Construction/Dims), transform XYZ steppers + rotation, live parameter sliders that rebuild derived figures, recipe chain, sketch curves + constraint summary, dimensions list (+ add/remove), duplicate/isolate/delete, command line speaking the **real `.arc` grammar** (see below) | `runCmd()` ← send verb to `ConsoleHost`; sliders ← `FigureRecipe` param edit |


## Command line — the `.arc` grammar (shared with the C++ ConsoleHost)

The Inspector's `arc ›` input accepts exactly the text `Console/CommandCodec.cpp` accepts, because
`Scripts/*.arc` is the regression corpus for both halves:

```
verb arg arg…        # comment to end of line
(x,y)  (x,y,z)       vectors — whitespace inside is allowed: (0, 0, 5)
--flag  --flag=value switches
a ; b ; c            several commands on one line
"quoted name"        quoted text
```

`arcDecode()` is a statement-for-statement port of `CommandCodec::Decode`; `arcNumber` / `arcPoint`
mirror `ParseNumber` / `ParsePoint` (a number must consume its whole token, so `40mm` is refused
rather than silently read as `40`).

**Everything fails fast and says why**, like `ConsoleHost::Refuse` — and a refused command leaves no
undo entry:

```
arc › box 40 30 20
✗ box: argument 1 must be a point (x,y[,z]) — usage: box (cornerA) (cornerB) · box (corner) dx dy dz
```

Usage strings are copied from the C++ `Add(verb, help, …)` registrations so the two cannot drift.
Output goes to a **persistent transcript pane above the input** — it is static markup, so it is
visible whatever is selected. (Previously `log()` targeted an element that only existed inside a
single-figure inspector, so every command-line message was silently discarded.)

Implemented: `echo · help · reset · list · select · view · move · rename · hide · show · delete ·
undo · redo · plane · workplane · line · rect · circle · arc · ellipse · polygon · slot · polyline ·
point · box · cylinder · sphere · cone · torus · extrude · fillet · chamfer · dim · measure ·
topology`.

Verbs the C++ host has but the panel has not ported yet (`loft`, `sweep`, `boolean`, `revolve`,
`mirror`, `array`, `render`, …) are registered as **known-but-unimplemented** and answer
`not in the HTML prototype yet — C++ host only`. That is deliberately a *different* message from
`unknown verb '…'`: it tells you whether the HTML is behind the kernel or whether you mistyped.

`help` lists both sets. `help <verb>` prints that verb's usage.

When the console host is bridged, `runCmd` collapses to a transport call — refusal rendering and the
REPL-shaped transcript already work:

```js
function runCmd(s){ send(s); }     // → ConsoleHost::Execute(s)
```

See [`../docs/HTML_CONSOLE_AUDIT.md`](../docs/HTML_CONSOLE_AUDIT.md) for the full audit of what was
broken and why.

## Transform gizmo (port of Slate `References/Gizmo.html`, Blender behaviour)
Select a body and the gizmo appears at its **centre** (bounding-box middle, like Blender's median pivot), oriented with the body, ~92 px on screen at any zoom. It has three modes, chosen with the **Move / Rotate / Scale** segment in the toolbar or **G / R / S** — only the handles of the current mode are shown:
- **Move** — cone per axis (X red, Y green, Z blue) + corner quad per plane (cyan / magenta / yellow).
- **Rotate** — full ring per axis (back half faded) + white outer ring for rotation about the view axis.
- **Scale** — capped cylinder per axis. Rotation and scale act about the pivot, so the body stays centred.

Hover highlights white; drag applies live. The readout at the top (`X move  12.50 mm`, `Y scale 1.100 ×`, `Z rotate 15.0 °`) has an **editable value**: click it or press **Tab / Enter / =** during a drag or modal, type an exact number, **Enter** commits, **Esc** cancels. Hold **Ctrl** to snap (5 mm / 0.1× / 5°). Every operation is one undo step.

Modal (Blender): **G / ⇧R / S** with a body selected (plain **R** always draws a rectangle, even with something selected), move the mouse; **X / Y / Z** lock an axis (again = clear; an axis line is drawn); type digits for an exact amount; **LMB / Enter** confirm; **RMB / Esc** cancel. `R` with no body selected still starts the rectangle tool.

### Curves have their own transform
Clicking a drawn shape selects **that curve**, not its sketch (the sketch is still selectable from the Outliner and moves everything). A curve's gizmo is in-plane: Move = two cones + plane quad, Rotate = ring about the plane normal (+ view ring), Scale = two cylinders. Curve position/rotation/scale are stored in plane (u,v) space and composed with the sketch transform.

Curves can also be lifted off their plane: the Z cone / `G Z` moves along the plane normal (stored as the curve's third coordinate).

### Constraints, dimensions, variables (the "parametric" part)
Every sketch carries `cons_`, solved by a damped Gauss-Newton solver over all curve control points + radii (numeric Jacobian, Cholesky). Open **Tab → Constrain**: Coincident, Horizontal, Vertical, Parallel, Perpendicular, Equal, Tangent, Concentric, Midpoint, Point-on-line, Fix — select vertices/edges (modes 3–4, shift-click) then click the tile. Constraint glyphs appear beside the geometry; the sketch/curve inspector lists them with the DOF count; `×` or `⌫` removes.

**Dimension tool** (`⇧D` or the Dimension tile): click a line → drag → release where you want the label. Click a circle/arc for ⌀/R, two lines for an angle, two points for a distance — while dragging between two points the tool infers **aligned / horizontal / vertical** from where you drag (Fusion style). Drag an existing label to re-place it; double-click it (or click the constraint in the inspector) to edit.

**Per-object dimensions.** Each curve/sketch inspector lists every dimension on it with an inline **name** field (type a name, ⏎) — an object can carry as many dimension lines as you like; `+ dimension` starts the tool on that object. Named dimensions are variables.

**Slots are one entity.** Both Slot and Polyline Slot keep their centre *spine* + radius; the outline is regenerated from them. Picking anywhere on the outline selects the whole slot (one edge), vertices are the slot centres, a length dimension on a slot drives the centre distance, and the inspector exposes the slot radius.

**Editing = variables.** The label editor takes a number or an expression: `W/2 + 3`, `sqrt(2)*R`, `min(A,B)`. Give the dimension a *name* and it becomes a variable other dimensions can use (drawn in blue). **Constrain → Variables** manages free variables. Changing any variable re-solves every sketch. Dragging a constrained vertex pins it and lets the solver move the rest live.

### Mirror · Linear / Circular pattern
Select curves, then **M** mirror / **⇧M** linear / **⌥M** circular (or the tiles in *Sketch Modify*, or the *Mirror · Pattern* section of a curve's inspector). Mirror: click two axis points (⌃ snaps), click an existing line to use it as the axis, or ⏎ for the sketch V axis; the inspector also has a *Mirror across* dropdown (any line in the sketch, or the sketch U/V axes). Linear: click direction start/end, count + "Spacing / Total" in the options. Circular: click the centre, count + total angle. Everything previews dashed while you pick. Copies are **live-linked** to their source: move / edit the original (or the axis line of a mirror) and the copies follow immediately; the inspector shows *Linked · mirror of Circle01* with an *unlink* button. Editing a copy directly unlinks it. Linked copies are excluded from the solver's DOFs. Links survive sketch-modify operations on the source: fillet/chamfer/trim show up on the copies, cutting a source in two gives the new piece its own live copy, deleting the source deletes its copies. Documents saved before live links (`mirrorOf`) are upgraded on load.

### Snapping while drawing
Hold **⌃ (Ctrl/⌘)** to snap the cursor to endpoints, vertices, midpoints, centres, quadrants and intersections (glyph + label at the snap). Hold **⌥ (Alt)** to slide along the nearest curve — with a perpendicular-foot snap from the previous point. Without modifiers the 5 mm lattice snap applies as before.

### History · Save / Open
Every undo step is labelled. **⌃H** (or *history* in the outliner) opens the History page: click any step to jump back or forward; footer has *save .json* (⌃S), *open .json* (⌃O), *copy*, *new document*. The document autosaves to localStorage and is restored on reload (not when `?demo`).

### Analytic B-rep solids · edge fillet / chamfer on bodies
- Extrusions are now built by `solidBuild()` as a **boundary representation**: every control segment of the profile becomes ONE face — a line sweeps to a plane, an arc / circle sweeps to a cylinder (cone under draft); caps are planes. A cylinder is therefore 3 faces, 2 circular edges and no vertices; the side is shaded with per-vertex analytic normals (gradient-filled quads) so it reads as one smooth curved surface, with a silhouette line computed per frame instead of facet lines.
- Faces / edges / vertices come from the B-rep (`mesh.brep`), keyed stably (`side:2`, `cap:top`, `h1.top:0` …) so face picking selects the whole curved side and edits survive rebuilds. Tangent edges (fillet boundaries) draw faint; sharp edges draw dark.
- **Fillet (B) / Chamfer (⇧B) work on solids**: hover a body edge or face (highlight follows), click, drag for the radius / distance (`Ctrl` = 0.5 mm steps) or type it, ⏎ / click to apply; or pre-select edges/faces in Edge/Face mode and press B. Cap face → all its edges; side face → its top, bottom and vertical edges; single edge → just that edge. Cap-edge fillets are exact tori / cylinders swept from inward-offset rings; vertical-edge fillets insert an arc into the profile. Edits are listed in the Inspector (“Edge edits”, × removes), are undoable and are stored in the document (`body.edits`).
- Measure reports faces / edges / tangent edges and the exact tessellated volume.
- Cap-edge fillets are **Blender-style**: only the picked edge is bevelled; a neighbouring corner-fillet arc is not rolled into — the band dies out across it with a triangular blend (offset ramps from the bevel radius to zero along the arc, never past the arc centre). Offset rings are miter-limited and tangent joints are handled analytically, so a fillet next to an existing vertical-edge fillet cannot spike or fly off. Tangent (fillet-boundary) edges are not valid fillet targets. With the Fillet / Chamfer tool active, a click prefers edges and vertices over faces, and clicking an element that is not part of the current selection operates on that element alone — a whole edge loop is only bevelled when you explicitly pick a face (or select the loop).

### Compatibility
SolidArc's viewport is a **Canvas 2D software rasteriser** — it needs no WebGL, so GPU model (GTX vs RTX) doesn't matter; what does is the browser version. The page now polyfills `roundRect`, `requestAnimationFrame`, `ResizeObserver`, pointer capture and pointer events, falls back to a solid panel background when `backdrop-filter`/`color-mix` are unsupported, wraps each frame so one bad draw can't blank the view, and shows any runtime error as a red banner in the viewport (with file:line). The console log prints the build, the detected GPU/WebGL level and the browser at start — if the viewport is empty, that banner/log line is what to send.

### Focus (F) · Extrude (E) · Loft (⇧O) · Matcap
- **F** frames the selection (bodies, curves, sketches, sub-elements) with a short camera glide; with nothing selected it frames everything.
- **Extrude**: with a closed curve selected, E starts immediately and the mouse sets the height along the sketch normal (⌃ = 5 mm steps, ⌥ = symmetric, type a number, ⏎ / click applies, negative = other side). Otherwise click a closed profile — a curve, or a *filled region* of a sketch (holes come along). Bodies keep a link to their profile: edit the sketch and the solid rebuilds; height/draft live in the inspector.
- **Loft**: click two or more closed profiles (any sketches / workplanes — e.g. a circle on `Workplane01` and one on an offset plane), ⏎. Loops are resampled by arc length and aligned by angle; cap ends toggle and ruling count in the tool options. Editing a section rebuilds the loft.
- **Matcap** is now the default shading: a studio-style capture for machined parts — top-left key light, cool rim, soft under-fill, a tight clear-coat highlight and cavity darkening where the normal turns away; selection is a warm brass tint. Plastic / Flat / Wire remain.

### New document · continue previous
On launch, if an autosaved drawing exists, a start card asks **continue previous** / **new document** (or open a `.json`, or jump to history). **⌃N**, the **new** button in the outliner doc-bar and *new document* in the history page start a blank scene; the replaced drawing is kept as a backup and can be brought back with *restore previous* (history page). Autosave carries a timestamp. `?demo` skips the card.

### Fill rule
A region is a hole only when its loop lies **fully inside** another loop. Overlapping or crossing loops (e.g. a shape and its mirror sliding across the axis) are simply filled — no even-odd flicker while things move. The Fill tool still toggles any region by hand.

### Delete
**Delete / ⌫** (or the × in the outliner / inspector) really removes things, with cascade: a sketch takes its curves, constraints and dimension variables; a workplane takes the sketches on it; a body built from a deleted sketch goes too; links to deleted sources/axes are dropped; fill regions and the active plane/sketch are fixed up. In sub-element modes: vertices are removed (curve deleted when fewer than 2 remain), a polyline edge opens a closed loop or splits an open one, a circle/slot edge or any face deletes the curve, a body sub-element deletes the body. Pressing Delete while a tool is active first returns to Select. Fully undoable.

### Select (Q) · Offset/Inset (O) · Fill (⇧F)
- **Select — Q** leaves whatever tool is active (draw, dimension, trim/cut/fillet, offset, fill, gizmo modal) and returns to plain selection. Also a tile in *Sketch Modify*.
- **Offset / Inset — O** hover a curve, click, then drag: the side follows the cursor (inside a closed loop = inset, outside = outset; open curves offset toward the cursor). Type a number for an exact distance, ⏎ / click applies. Circles stay true circles; everything else becomes a mitred polyline. Result is a new curve, ready for a second Fill/extrude.
- **Fill — ⇧F** the sketch is a *planar arrangement*: every closed loop is split at its intersections and the resulting faces are regions (concentric circles → ring + disc; overlapping circles → A, B, lens). Default is even-odd (ring filled, inner disc empty → extrude gives a **pipe**). Hover highlights a region, click toggles it. Fills are stored per sketch (`regions_`); the profile / extrude / area follow the filled regions, so booleans-by-fill work in 2D before any solid boolean exists.

### Sketch Modify — Trim · Cut · Fillet · Chamfer
Tab → *Sketch Modify*, or hotkeys **T** trim, **K** cut, **B** fillet, **⇧B** chamfer. Plasticity-style: no pre-selection, the tool stays live until Esc / right-click.
- **Trim** — hover a curve and the span between its neighbouring intersections lights red; click (or press-and-drag across several) to remove it. End spans shorten the curve, middle spans split it in two, closed loops open, a curve with no intersections is deleted.
- **Cut** — hover a curve, click to split it there; the cursor snaps to nearby intersections (shown as dots). Closed loops become an open polyline at the cut.
- **Fillet / Chamfer** — click a corner (polyline vertex or two curve ends that meet), drag to set radius / distance, or type a number; ⏎ or click applies. Tab flips between fillet and chamfer while dragging. Value is clamped to the corner's maximum. Extra vertices pre-selected on the same curve receive the same fillet. Two touching curves are merged into one polyline.
Fillets are stored as **true arcs**: polylines carry `params.bulge[i]` (DXF-style tan θ/4 per segment). Topology exposes the fillet as a single arc edge (pick it, dimension its radius), drawing/fill/extrude sample it at the current resolution, mirror/pattern flip the bulge correctly. Trim/cut/chamfer still bake to straight polylines; constraints that referenced modified curves are dropped (logged).

### Workplanes follow their transform
A workplane's Position / Rotation / Scale (inspector or G/⇧R/S gizmo) now transform the plane *and everything on it* — sketches, curves, dimensions, picking, the drawing tool. `planeBasis()` composes the plane's own transform over its base axis / offset / in-plane rotation. The plane inspector is a workplane editor: base axis (XY/XZ/YZ), offset, in-plane rotation, extent, the sketches on it, and "make active"; no radius or generic dimension controls.

### Select modes · topology layer (B-rep-ready)
**Body / Face / Edge / Vertex** (`1–4`) — **Shift-click** a mode button (or `⇧1–4`) to combine modes, e.g. Vertex + Edge. Every figure exposes `topo(f)` → `{verts, edges, faces}` with stable indices: curves give their control points (poly vertices, line ends, circle centre + radius handle, ellipse centre + two axis handles) and a face when the loop is closed *and planar*; bodies derive unique vertices / edges / coplanar faces from the mesh. The B-rep kernel will later replace `topo()` and `moveCurveVertex()` only — selection, gizmo, inspector and overlay are written against that interface.

Sub-selection: click / shift-click any mix of vertices, edges and faces (even across figures). A translate gizmo appears at the selection centre; drag it, or press **G** (with X/Y/Z lock, typed numbers, Ctrl snap). All selected elements move together; an edge moves its two vertices, a face all of its vertices. Inspector shows the selection list, an editable world position (single vertex) or centre (multi), **flatten to plane**, and **delete vertices** (`⌫`).

Polyline vertices can be lifted off the sketch plane (`vz` per vertex; a dashed drop line shows the offset). The closed-loop fill is only drawn while the loop stays **planar** — lifting one corner removes the fill and flags `loop not planar · no face`; lifting a whole edge (two adjacent vertices) tilts the plane and the fill stays.

### Polygon & slots
**Polygon**: click centre → click radius → **scroll** to change the side count (3–20, shown in the prompt) → click / Enter to confirm. While drawing, construction spokes, the central angle arc, side length, radius and interior angle are shown. **Polyline** shows the segment length and the turn angle at each joint (first segment: angle from the plane's U axis). **Polyline Slot**: click a chain of centres, Enter / right-click to finish — the outline is a proper offset of the chain: round caps at both ends, round arcs on the outer side of each joint and mitre intersections on the inner side, so it reads as circles joined by straight slots with no self-overlap.

### Closed loops, fill, resolution
Closed curves (circle, ellipse, closed polyline/rectangle/polygon/slot, arc with *Closed loop* on) get a semi-transparent fill and are clickable inside. Inspector → **Curve display**: *Closed loop* (polyline / arc), *Fill when closed*, *Segments* (per-curve). Document → Presentation → **Curve resolution** multiplies every circle/arc/ellipse tessellation (0.25×–4×); arcs scale their count by sweep so wide arcs stay smooth.

### Green selection · default workplane · plane opacity · vertex bevel
- Selection highlights are now **green** (mint edges/faces, green-tinted matcap) instead of amber.
- A **new document starts with Workplane01 (200 mm, the size of the lattice) on the lattice and active**, so you can draw immediately. Workplanes default to 200 mm.
- Planes render at **10 % opacity** by default; the Inspector → Presentation has a **Plane opacity** slider (0–60 %), saved with the document.
- **Vertex bevel / chamfer** (Blender-style): in Vertex mode select a body corner (or hover it inside the Fillet/Chamfer tool), press **B** for a spherical corner or **⇧B** for a flat cut, drag / type the distance. Stored as a `vbevel` feature on the body (Inspector → Face features, removable, undoable); watertight.

### Smooth shading & studio matcap
- Curved faces (cylinders, cones, fillets, tori) are shaded per **interpolated normal**: every smooth quad is subdivided in screen space (adaptively, up to 6×6) and each cell is lit with its Phong-interpolated normal, so fillets and cylinders read as continuous surfaces instead of Blender-style flat polygons. Planar faces stay flat.
- The matcap is now a true view-space matcap: an analytic “studio clay” sphere (soft upper-left key, right fill, bottom bounce, fresnel rim, tight highlight, cavity darkening) sampled by the view-space normal only — so it looks the same from every camera angle, like a matcap texture.
- Fillet bands are sampled at 12 steps per quarter turn. Subdivision is driven by normal angle (one cell per ≤2.5°), so a 90° fillet always gets ≥36 shading steps regardless of the underlying quad count.
- `node Verification/render.js [fillet|cyl] [matcap|plastic|flat]` renders the viewport offline to `Verification/view.png` (software raster of the panel's canvas calls) — used to eyeball shading without a browser.

### Face features on solids — push / pocket (E), inset (I), gizmo face move
- **E on a planar face** of a solid (hover-click in the Extrude tool, or pre-select faces in Face mode and press E) pushes that face along its own normal: drag or type the distance; negative = pocket. Pushing a whole cap simply extends the adjacent walls (no extra faces); pushing an inset region grows a boss / cuts a pocket with new wall faces (smooth cylindrical walls on round regions).
- **I = Inset face**: click a planar face, drag / type the distance → a ring face plus the shrunken inner face, which keeps its key so you can immediately E it again. Chains like inset → push → inset → pocket are stable.
- **Moving a face (gizmo G / arrows, or E on it) edits the solid directly** — it is *not* a new extrusion unless it has to be: top cap → extrude height; bottom cap → base offset (top stays put); a side face of a polygon profile → the profile edge moves in the sketch. Only faces that were already produced by a feature (an inset region, a boss top) get a push feature.
- Features are stored on the body as `faceOps` and replayed on top of the profile extrude + edge fillets on every rebuild (editing the sketch keeps them); the Inspector lists them under “Face features” with × to remove; Measure counts them. All results are watertight (checked in the smoke suite).
- Not yet: curved-face push, multi-body booleans, push along arbitrary vectors.

### Sticky drawing tools · E = Extrude
- A drawing tool picked from the catalogue or by hotkey **stays armed** after each shape: draw a circle, then another, and another — until Esc, Q (select) or another tool. The prompt says “tool stays active — Esc / Q to stop”.
- **E** now starts Extrude (Ellipse moved to **⇧E**).

## Live dimensions while drawing
Every 2-D tool shows its dimensions as dimension lines while you draw: circle `R` and `⌀`, arc `R` + sweep angle, ellipse `a` / `b`, line length, rectangle width × height, polyline last-segment length, polygon / slot overall width.

## Construction catalogue (Tab / right-click / **Construct** button)
Boxier 12 px corners, 560×440 by default, **resizable** from the bottom-right grip (or the native resize corner); the rail/grid/options fill whatever size you give it.
Popup by default (closes after you pick), **pin** it to keep it open as a panel. Rail on the left (Reference · Sketch Draw live; the rest greyed until we port them), tile grid, click → options slide, double-click / **Start drawing** → tool. Tiles are **gated** by document state: sketch tools are disabled with a "set a workplane" fix-chip until a workplane exists; header chips show the active plane and the sketch curves will go into.

Tools: Workplane (XY/XZ/YZ + offset + rotation, no picks) · Datum Point · Line · Polyline · Rectangle · Centre Rect · Slot · Circle (centre/diameter) · Arc (centre-start-end, CCW/CW) · Ellipse · Polygon (inscribed/circumscribed) · Point. Every curve is registered under a Sketch on the active plane in the Outliner; the live preview draws in plane-space with a snapping cursor readout.

The document starts **empty**; `?demo` in the URL loads the sample scene.

Keyboard: `1–4` select mode · `5` ortho · numpad `1/3/7` front/right/top · `L ⇧L R C A E P` sketch tools · `⇧W` workplane · `Tab` catalogue · `Enter` finish polyline · `G R S` move/rotate/scale (Blender modal) · `H` hide · `Alt+H` unhide all · `F` frame · `D` dims · `⌫` delete · `Ctrl+Z / Ctrl+Shift+Z` undo/redo · `Esc` cancel/deselect.
- **Push commits what the preview shows.** Pushing a face (E / gizmo) first tries the parametric route (height / base / profile-edge edit); the result is compared with the previewed push feature and, if they differ (e.g. the cap has filleted edges, so a taller extrude would swallow the fillet), the previewed push feature is kept instead.

## Analytic slots · tangent wall joints

- **Slot / Polyline slot are analytic**: the outline is straight flanks plus true tangent arcs (semicircle end caps, arcs at outer joints, mitre at inner joints) stored as bulges — no more 48-point polygons. Extruding one gives proper `cylinder` faces for the caps and corners, exactly like a solid modeller; old documents with polygon slots are upgraded on load. The 2D area matches the exact Minkowski (spine ⊕ disc) area within tessellation error.
- **Tangent wall joints are not edges.** Where a flat wall meets a cylinder wall tangentially the B-rep now records a tangent (non-selectable, non-filletable) joint instead of a sharp edge, so fillet/chamfer only offers real edges: the two sharp inner corners of a U-slot and the cap edges.
- Cap-edge fillets/chamfers that run into a cylinder cap terminate cleanly (triangular blend rule), and pushing the slot's top commits exactly what the preview showed.
- **Normal audit (smoke §28)**: every body is checked for outward winding (positive signed volume), unit analytic normals that never oppose their facet (< 12° deviation), constant normals on planar faces and horizontal normals on wall cylinders. Fixed by the audit: pockets no longer "extend" the outer wall with inverted normals (a wall is only extended when it faces the same way), a chamfer that crosses a corner arc is a `cone` with tilted normals, and plane/arc tangent joints in offset rings keep the planar wall exactly planar.
- Concave (reflex) vertical corners fillet/chamfer correctly (material is *added*, exact analytic amount); the two arc/wall joints are tangent joints — no seam lines, no sharp vertices — and clicking one with B/⇧B falls back to the face (or tells you it is a smooth joint) instead of doing nothing.
- **Fillet/Chamfer pick order**: body vertices/edges → body faces → sketch corners → other curves. Previously a click on a body's vertical edge near its base was grabbed by the *profile sketch corner* sitting underneath, so the sketch got a 2D fillet and the solid regenerated with a rounded profile corner instead of the picked edge (smoke §30).

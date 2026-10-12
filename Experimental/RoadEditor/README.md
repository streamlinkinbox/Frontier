# Road Editor

A standalone, browser-based road-network editor styled after the Frontier **Project-Zero /
FrontierEditor** UI (graphite panels, DM Sans, outliner · viewport · inspector). It generates and
edits a Need-for-Speed scale city map — **roads only**, no buildings — with curbs, paving, bridges,
pathways, level crossings and subways.

```
npm install
npm run dev            # vite on 0.0.0.0:5173
npm run build          # production bundle in dist/
npm run test:gen       # headless generator smoke test (node only, no browser)
```

---

## What it generates

| Area | Content |
| --- | --- |
| **Surfaces** | asphalt, aged asphalt, concrete slabs, cobblestone, brick pavers, gravel, dirt, grass, ballast, quay stone — each a procedural diffuse texture (with normals where it matters) laid out in per-lane UVs |
| **Cross-sections** | 26 road profiles in 6 categories (highway · arterial · urban · residential · rural · active), assembled from 15 composable lane types: `drive, bus, bike, cycletrack, parking, shoulder, sidewalk, planter, median, buffer, tram, barrier, verge, gravel, path` |
| **Road edge** | raised curb built as granite top + face strips, sidewalk and frontage ribbon, speed humps and raised crossing tables (a real elevation profile, not paint), bus stops with shelter + yellow curbside box, painted parking bays and hatched no-parking zones near junctions, storm-drain inlets and manhole covers |
| **Markings** | dashed/solid lane rules per adjacent lane pair (US yellow centre / EU white), edge lines, stop bars, continental (zebra) crosswalks with tactile paving at the curb ramps, give-way teeth, hatched channelisation, turn arrows (`arrowS/L/R/SL/SR`), word decals (`BUS`, `ONLY`, `STOP`, `SLOW`, `KEEP CLEAR`, `BUS STOP`, `RXR`, tram and bike glyphs), zebra- and chevron-painted crossing tables |
| **Junctions** | auto-classified as `junction · roundabout · flush T · cul-de-sac · continuation`; corner fillets and radii fitted from each approach's width and rank; paved apron with a triangulated surface that absorbs all approaches; the **Repair intersections** pass splits and welds every at-grade crossing into a shared graph node while keeping crossings separated by more than 2.5 m as bridges/underpasses; roundabouts get a central island with curb ring, truck-mountable cobble apron, grass centre and trees, plus the circulating edge line; tram rails run straight through opposing approaches; per-approach traffic control (signals, stop or yield) with mast arms, three-lamp signal heads and pedestrian signals |
| **Bridges & water** | extra mid-block river corridors produce multiple named road bridges; elevated decks have concrete side walls down to the ground, parapets/railings, pier columns and caps placed clear of the carriageway below (piers are refused where they would land in a live lane); river with stone quay walls, coping and railing (skipped where roads and rail cross), riverbed, and an animated water-normal surface, with the terrain hole cut along the channel |
| **Railway** | a two-track freight/commuter line plus a single-track regional branch, both with ballast, concrete sleepers, rails, overhead-line masts and cantilever arms, lineside fencing, paved crossing panels, crossbucks, gate posts and striped gate arms, advance `RXR` warnings, water bridges, and through-tram rails embedded in the carriageway |
| **Subway** | cut-and-cover box tunnel with twin tracks, station boxes with side platforms, platform-edge walls and yellow caution strips, station columns, and up to 4 street entrances per station snapped onto nearby sidewalks |
| **Props** | 56 039 instanced placements: `lamp, lampPost, lampDouble, lampHigh` (with arms and luminaire), `signalPole / signalArm / signalHead / pedSignal`, `signStop, signYield, signHump, signCrossing, signRail, crossbuck`, `bollard, bollardSign, delineator, hydrant, bin, bench, busShelter`, `treeA, treeB`, bridge `pierCol / pierColWide / pierCap`, rail `sleeper, catenaryMast, catenaryArm, fencePost, gatePost, gateArm` (half-barriers are split into ≤ 7 m arms with a cabinet at each pivot, and each arm gets a counterweight housing behind it), subway `stationColumn, subwayEntrance` |

### Reference build — seed 1337, 4 × 4 km

```
1229 nodes · 1703 edges · 232 km of road · 536 lane-km
824 junctions · 18 roundabouts · 194 cul-de-sacs · 77 flush T · 116 continuations
9.1 km railway (2 lines with level crossings and water bridges) · 5.1 km subway (2 lines × 5 stations)
20.1 km bus lanes · 57.8 km cycle lanes · 97.9 km parking lanes
1.24 M triangles · 0 warnings · 0 non-finite values
generate ≈2.2 s · mesh build ≈1.1 s (single node thread)
```

---

## Controls

Unreal-style free camera: `W A S D` fly, `Q`/`E` down/up, `Shift` boost ×4, `RMB + mouse` look,
`RMB + wheel` fly speed, `MMB drag` pan, `Alt + LMB` orbit around the cursor, `wheel` dolly to cursor,
`F` frame the selection. Look-mode ignores tool keys, so `WASD` never fights the editor.

| Key | Action |
| --- | --- |
| `V` / `1` | Select tool |
| `B` / `2` | Draw tool (uses the profile in the toolbar picker) |
| `X` / `3` | Bulldoze tool |
| `Enter` | Finish the current draw |
| `Backspace` | While drawing: drop the last point · otherwise delete the selection |
| `Del` | Delete selection |
| `Esc` | Cancel draft → close library → back to Select → deselect |
| `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` | Undo / redo |
| `G` | Toggle snapping |
| `Shift + click` while drawing | Snap the segment direction to 15° steps |
| `Shift+A` | Road library |
| `Ctrl + click` | Select the whole street (every segment of that name) |
| `double-click` a road | Insert a bend point at the cursor |

Drawing snaps to existing nodes within 9 m and to an existing centreline within 0.5 m, so streets
connect and junctions form automatically; with snap on, free clicks round to the metre grid. Selecting
a road or junction exposes a **red X / green Y / blue Z transform gizmo**: drag Y to raise or lower
the complete road (or node) and drag X/Z to translate it. Dropping a moved road automatically welds
new at-grade crossings; use **Repair** in the viewport for a full topology pass.

## Panels

* **Outliner** — roads grouped by category → named street → segment, with search, per-group counts,
  expand/collapse, and click-to-focus selection. Long groups cap at 120 streets with a "+N more" row
  that asks you to refine the search.
* **Viewport** — tool strip (Select · Draw · Bulldoze, the active draw profile, new-point elevation,
  **Repair topology**, undo/redo, snap), view-mode switch (Lit · Topology · Underground), a day/night toggle,
  the controls cheat-sheet, a live perf HUD (speed · altitude · heading · FPS · draws · triangles) and a minimap
  with the camera frustum wedge. The selected-road **XYZ gizmo** moves a road as a unit; its green Y arrow changes
  elevation in 0.5 m snapped increments. The elevation field is how bridges happen: draw at +12 m over an existing
  road and the crossing resolves as an elevated deck with piers.
* **Inspector** — *City*: network metrics (length, intersections, roundabouts, cul-de-sacs, bus/cycle/
  parking/railway/subway km), the generator (seed, map size 2–8 km, street density, toggles for river,
  freeway ring + interchanges, railway, subway, tram avenues, roundabouts, old town, suburbs, parks &
  paths, US/EU marking style) and display toggles (markings, props, trees, shadows, time of day).
  *Segment*: live **cross-section diagram** with hover highlighting, per-lane type/width/direction
  editing, add-lane-left/right and mirror, profile picker grouped by category, and a geometry card
  (length, width, bend points, grade, bridge flag). *Node/Junction*: junction kind, roundabout island
  radius, cul-de-sac turning circle, curb radius (auto or manual), traffic-control override
  (auto · signals · all-way stop · two-way stop · yield · uncontrolled) and node elevation.
  *Street*: rename or re-profile every segment of the street at once.
* **Road library** (`Shift+A`) — sliding modal in the Frontier Construct-menu style: category rail,
  search, 26 profile tiles drawn at true scale, *Apply to selection* and *Draw with…* — double-click a
  tile to start drawing that profile immediately.

---

## Architecture

```
src/
  main.jsx  App.jsx  styles.css        shell: outliner | viewport | inspector
  editor/editor.js                     Editor store: tools, selection, drag, undo, generate()
  ui/*.jsx                             React panels; useEditor() subscribes to the store
  road/                                data + geometry, three.js-free
    math.js                            rng, noise, resample, curve helpers
    centerline.js                      Centerline: arc-length frames (t,n,y,curvature)
    profiles.js                        LANE_TYPES, PROFILES (26), layout() widths/offsets
    network.js                         RoadNetwork graph + SpatialIndex
    planarize.js                       intersection detection & node insertion
    junction.js                        approach detection, auto radii, control class
    geom/out.js                        Out: material-keyed p/uv/i buckets + bbox + prop records
    geom/edge.js                       road surface, curbs, sidewalk, humps, markings per edge
    geom/junctionMesh.js               junction triangulation, aprons, roundabout islands
    geom/markings.js decals.js         line/dashed/bar/tooth/hatch + decal-atlas quads
    geom/control.js                    traffic-control resolution & hardware placement
    geom/water.js rail.js subway.js    river & quays, railway, tunnels & stations
    builder.js                         NetworkBuilder: element -> renderable + prop instances
    cityGen.js                         DEFAULT_GEN, generateCity()
  render/                              three.js layer
    textures.js                        procedural texture set + decal/sign atlases
    materials.js                       PBR materials (roads) and prop materials (vertex colour)
    propsLib.js                        prop geometry; front = local +x, [x,y,z,yaw,pitch,sx,sy,sz]
    world.js                           chunks, LOD, shadow sun, sky/PMREM, picking, overlays
    camera.js                          CameraController (fly / look / orbit, speed steps)
```

**Pipeline.** `generateCity()` returns a purely topological `RoadNetwork`. `NetworkBuilder` sweeps
each element into an `Out` — flat per-material vertex/UV/index arrays plus instanced prop records —
and `World` merges those into **512 m road chunks** (paving + curbs in one mesh per material,
markings in a second group) and **400 m prop chunks** (one `InstancedMesh` per prop part). Editing an
element rebuilds only the chunks that reference it (`applyUpdate`), which is what keeps dragging a
handle interactive on a 224 km map.

**LOD & culling.** Road chunks cull on their real bbox; prop chunks on cell distance
(`propDist` 620 m, trees 1.5×). Markings drop out past `markDist` (1600 m). Everything is merged, so
in-view draw calls stay between 150 and 700 while up to 3.7 M triangles are drawn — the scene is
fill-rate bound, not draw-call bound.

**Lighting.** Sky shader + PMREM environment, ACES tone mapping, and a camera-following directional
sun (2048² PCF-soft, texel-snapped focus to stop shimmer) with a deliberately strong key / weak fill
ratio so cast shadows stay readable on dark asphalt. Normal bias (0.25) keeps the merged road ribbons
from acne-ing themselves.

## Scripts

* `scripts/smoke-generate.mjs` — generate + build headlessly, print triangle/prop/material counts,
  assert no NaN and no missing materials.
* `scripts/topology-test.mjs` — proves the repair pass splits and welds an at-grade X crossing while
  preserving a grade-separated bridge/underpass crossing.
* `scripts/winding-test.mjs` — checks every swept quad and arc strip is CCW when seen from outside,
  so the merged mesh can never show a flipped face.

## Notes & limits

* Roads only, by design: blocks are paved/grassed parcels with street trees, no buildings.
* Terrain is a district-coloured displaced grid with a rasterised river cut; `groundAt()` is the
  single height authority, and raised elements (`RAISED` for curbed sidewalks) are consistent across
  edges, junctions, rail and subway so nothing z-fights at the seams.
* Props cast shadows but do not receive them — a fill-rate tradeoff, easy to flip in
  `World.buildPropChunk`.
* Undo keeps up to 100 full-network snapshots; a 4 km city snapshot is ~4 MB.
* Subway stairs are intentionally not modelled below grade — the raised sidewalk would hide them.

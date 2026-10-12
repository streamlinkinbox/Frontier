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
npm run test:merge     # interchange gate: merge quality, clearances, ramp grades
```

---

## What it generates

| Area | Content |
| --- | --- |
| **Surfaces** | asphalt, aged asphalt, concrete slabs, cobblestone, brick pavers, gravel, dirt, grass, ballast, quay stone — each a procedural diffuse texture (with normals where it matters) laid out in per-lane UVs |
| **Cross-sections** | 26 road profiles in 6 categories (highway · arterial · urban · residential · rural · active), assembled from 15 composable lane types: `drive, bus, bike, cycletrack, parking, shoulder, sidewalk, planter, median, buffer, tram, barrier, verge, gravel, path` |
| **Road edge** | raised curb built as granite top + face strips, sidewalk and frontage ribbon, speed humps and raised crossing tables (a real elevation profile, not paint), bus stops with shelter + yellow curbside box, painted parking bays and hatched no-parking zones near junctions, storm-drain inlets and manhole covers |
| **Markings** | dashed/solid lane rules per adjacent lane pair (US yellow centre / EU white), edge lines, stop bars, continental (zebra) crosswalks with tactile paving at the curb ramps, give-way teeth, hatched channelisation, turn arrows (`arrowS/L/R/SL/SR`), word decals (`BUS`, `ONLY`, `STOP`, `SLOW`, `KEEP CLEAR`, `BUS STOP`, `RXR`, tram and bike glyphs), zebra- and chevron-painted crossing tables |
| **Junctions** | auto-classified as `junction · roundabout · flush T · cul-de-sac · continuation`; corner fillets and radii fitted from each approach's width and rank; paved apron with a triangulated surface that absorbs all approaches; roundabouts get a central island with curb ring, truck-mountable cobble apron, grass centre and trees, plus the circulating edge line; tram rails run straight through opposing approaches; per-approach traffic control (signals, stop or yield) with mast arms, three-lamp signal heads and pedestrian signals |
| **Bridges & water** | elevated decks with concrete side walls down to the ground, parapets/railings, pier columns and caps placed clear of the carriageway below (piers are refused where they would land in a live lane); river with stone quay walls, coping and railing (skipped where roads and rail cross), riverbed, and an animated water-normal surface, with the terrain hole cut along the channel |
| **Interchanges** | up to six grade-separated sites on the freeway ring — diamond, cloverleaf, Parclo A4, turbine and trumpet — laid out from a plan template scaled to the street grid; loop, direct-connector, on- and off-ramps with ramp2/rampWide/rampLoop profiles, a paved gore with chevron hatching, barrier terminals at each nose and a Keep-Right assembly on the divide |
| **Railway** | ballast section, concrete sleepers, twin rails, overhead-line masts and cantilever arms, lineside fencing, paved crossing panels, crossbucks, gate posts and striped gate arms, advance `RXR` warnings, and through-tram rails embedded in the carriageway |
| **Subway** | cut-and-cover box tunnel with twin tracks, station boxes with side platforms, platform-edge walls and yellow caution strips, station columns, and up to 4 street entrances per station snapped onto nearby sidewalks |
| **Props** | 56 039 instanced placements: `lamp, lampPost, lampDouble, lampHigh` (with arms and luminaire), `signalPole / signalArm / signalHead / pedSignal`, `signStop, signYield, signHump, signCrossing, signRail, crossbuck`, `bollard, bollardSign, delineator, hydrant, bin, bench, busShelter`, `treeA, treeB`, bridge `pierCol / pierColWide / pierCap`, rail `sleeper, catenaryMast, catenaryArm, fencePost, gatePost, gateArm` (half-barriers are split into ≤ 7 m arms with a cabinet at each pivot, and each arm gets a counterweight housing behind it), subway `stationColumn, subwayEntrance` |

### Reference build — seed 1337, 4 × 4 km

```
1072 nodes · 1400 edges · 206.6 km of road · 489 lane-km
664 junctions · 19 roundabouts · 204 cul-de-sacs · 74 flush T · 112 continuations
4.5 km railway (level crossings) · 5.1 km subway (2 lines × 5 stations)
3 interchange sites · 7 ramps · 2.5 km of ramp · deck sag to +5.6 m at each site
10.0 km carrying bus lanes · 28.3 km cycle lanes · 40.4 km parking lanes
1.11 M triangles · 52 918 props · 0 warnings · 0 non-finite values
generate 1.4 s · mesh build 1.0 s (single node thread)
```

---

## Interchanges: how a ramp merges

An interchange is not stamped out as a picture of one — every ramp is measured against the road it
joins, and is discarded if the join would not be lawful.

**The merge.** `mergeEnd()` in `src/road/interchange.js` samples the *world* centreline of the
mainline and walks the ramp's own edge along it: a short run **flush** (coplanar, welded onto the
mainline's outer edge line), then a **flare** where the gore opens at the painted rate, then the
free curve. At the tip the deck is pinched onto that edge line (`tipTaper` in `geom/edge.js`), so
the two surfaces meet at zero gap instead of overlapping — `geom/merge.js` paves the neutral area
between them, marks it with chevrons, lifts the paint clip and cuts the guardrail so each end
finishes in a crash cushion. A nose that does not touch, or that overlaps by 2 cm, fails the gate.

**The plan.** Archetypes carry `along / flush / flare / gap / R` dimensioned for a ~340 m street
grid and scaled per site by `icK` (median street spacing ÷ 340, clamped 0.62–1.3). Only the
*reach* scales: taper runs are governed by the legal merge rate (≥ 1:6 through the painted gore),
so compressing them would break the blend rather than the fit. Loops solve for the tightest radius
that lands them on the arterial's outer lane edge, travelling the right way; diagonals pick the
carriageway they actually sweep into.

**The profile.** The freeway deck dips to +5.6 m over each site (from +7) so a ramp has room to
lose its height at all — 9 m inside a city block is not buildable at a legal grade by any layout.
Levels are held only where the ramp is coplanar with the road it is attached to; everything in
between is one eased curve, and a built path over 8.4% is deleted rather than shipped.

**What stops a ramp from being built.** Anything that would leave it threading a roadway it does not
join: the conflict test exempts the ring, the street the tail is spliced into, streets that end in
the same junction, and anything cleared by more than the structure depth — and every rejection is
logged with the offender, its station and the level difference:

```js
generateCity({ seed: 4242, size: 4096 }).meta.icLog
// 'turbine: diag: would run through e1k9:Riverside (local) s=343 lvl 2.6 | G=ebz:5th Avenue jct=-'
// 'turbine: would clip 3rd Avenue at s=344m, 2.6m above'
// 'cloverleaf: grade 8.9% over 244m of ramp'
// 'cloverleaf: loop: no landing'
```

`npm run test:merge` — or `node scripts/merge-test.mjs [seed] [size]` — asserts all of it on every
ramp in the generated city and exits non-zero on failure. `scripts/ic-debug.mjs` prints the per-ramp
detail behind a failure: spline deviation from the control polyline, which side of the mainline each
merge picked, and the measured gap at 0/30/60/90/120/150 m from each blended tip.


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
connect and junctions form automatically; with snap on, free clicks round to the metre grid.

## Panels

* **Outliner** — roads grouped by category → named street → segment, with search, per-group counts,
  expand/collapse, and click-to-focus selection. Long groups cap at 120 streets with a "+N more" row
  that asks you to refine the search.
* **Viewport** — tool strip (Select · Draw · Bulldoze, the active draw profile, new-point elevation,
  undo/redo, snap), view-mode switch (Lit · Topology · Underground), a day/night toggle, the controls
  cheat-sheet, a live perf HUD (speed · altitude · heading · FPS · draws · triangles) and a minimap with
  the camera frustum wedge. The elevation field is how bridges happen: draw at +12 m over an existing
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
* `scripts/winding-test.mjs` — checks every swept quad and arc strip is CCW when seen from outside,
  so the merged mesh can never show a flipped face.
* `scripts/merge-test.mjs` (`npm run test:merge`) — the interchange gate: nose contact, gore taper
  rate, monotone blend, per-ramp length/grade/clearance, over-crossing headroom. Exits 1 on failure.
* `scripts/ic-debug.mjs` — per-ramp merge diagnostics for a failing case (see
  *Interchanges: how a ramp merges*).

## Notes & limits

* Roads only, by design: blocks are paved/grassed parcels with street trees, no buildings.
* Terrain is a district-coloured displaced grid with a rasterised river cut; `groundAt()` is the
  single height authority, and raised elements (`RAISED` for curbed sidewalks) are consistent across
  edges, junctions, rail and subway so nothing z-fights at the seams.
* Props cast shadows but do not receive them — a fill-rate tradeoff, easy to flip in
  `World.buildPropChunk`.
* Undo keeps up to 100 full-network snapshots; a 4 km city snapshot is ~4 MB.
* Subway stairs are intentionally not modelled below grade — the raised sidewalk would hide them.
* Interchanges need roughly 250 m between cross streets to fit a lawful ramp grade, so a 2 km map
  gets the freeway ring without ramps; `meta.icLog` says which constraint (spacing, river, diagonal
  or railway) ruled a site out. Ramps are capped at an 8.4% grade and clipped paths are dropped, so
  an archetype may come out with four ramps instead of eight — nothing is built to a bad standard.
* A few city corridors (park loops, late-added boulevards) are placed after the freeway block, so a
  ramp can be drawn beside one of them; footway-level paths are exempt from the clearance gate, since
  they are the one layer that can be re-routed under a deck.

# Frontier — Road & World Studio

A working, browser-based road-network editor built with Three.js and Vite. The included **Westhaven County** project connects a small town, farmland, river crossings and unpaved routes. Its road network is an editable graph, not a collection of overlapping road rectangles.

**This is an editor foundation, not an AAA-ready procedural city pipeline.** The road-surface OBJ is quad-only; the entire displayed world is not a production-quality quad asset. See the explicit limitations below.

## Three connected bridge networks

Use **Bridge networks · 3** or **Find a sample → Connected bridge networks**:

- **Pearl loop** — elevated oval, diagonal approach ramps and a boulevard beneath.
- **Lantern roundabout** — four elevated approaches and a landscaped central deck over a through-road.
- **Dragonfly viaduct** — curved branches sharing one elevated junction.

All three connect to the existing road graph through a shared access street. First installation frames the Pearl loop; each picker card can frame the interchange or all its road connections. The complete starter has **306 road segments** and **33 sample destinations**. Prior saves are backed up and upgraded without replacing their existing roads. See [bridge-network geometry, safeguards and limitations](docs/bridge-networks.md).

## Two cities, with the samples already built

After the initial bridge-network tour, reloads open over **Eastbank City**, a second district on the **same map** as Westhaven. No manual placement, profile application, or demo reset is needed.

- **Eastbank · 2nd city** frames the district; **Westhaven** returns to the original town.
- **Find a sample** jumps to all 13 built road types, all 9 curb/drainage designs, four Eastbank rail alignments, four single bridge examples, and three connected bridge networks: **33 destinations**.
- A **480 m tram boulevard** crosses the district. Four rails continue through its two matching straight-through junctions, with a stationary two-car tram for visibility. Open-ended tram streets can now depart on curved independent rail alignments using **Rail exits**. Track switches and vehicle simulation remain out of scope.
- The second city has buildings, planted streets, cycle tracks, a roundabout, access/service/shared streets, country/highway/trail samples and a road connection to the original county.
- Terrain, map framing, sun coverage and the minimap expand to include both cities. The two-city transport fixture has **145 road segments**; the complete UI map adds the three bridge networks for **306**.
- Existing browser saves are upgraded once, without replacing their original streets. `frontier-before-second-city-v1` holds the pre-addition JSON backup. The full addition is checked before it is accepted; failures keep the existing map and show an error. The version marker prevents duplicate districts after reload.

```sh
npm run test:second-city
SECOND_CITY_PHASE=migration npm run test:second-city
```

`src/sample-city.js` builds and audits the district; `src/tram-network.js` samples the actual junction surface for straight-through rail connections. The legacy 82-road and 100-road layouts remain regression fixtures rather than the current full UI map.

## Run

Requires Node.js 22 or later.

```sh
npm install
npm run dev
```

The development server binds to `0.0.0.0:5173`; `.arena.site` and `.e2b.app` preview hosts are allowed. All fonts and application assets are served locally. There is no runtime CDN, API or account requirement.

```sh
npm test           # Model and mesh regression tests
npm run build     # Production build in dist/
npm run test:browser  # UI smoke checks against an already-running dev server
```

The browser test uses npm-distributed Chromium and bundled libraries for the Linux sandbox. Override the server URL with `FRONTIER_URL`. Screenshots go into ignored `.playwright/` storage.

## Implemented

### Editing

- 2D orthographic plan, perspective 3D fly/orbit navigation, and linked split view using the same project graph.
- Twelve road presets, including garden boulevard, protected cycle street, green tram boulevard, roundabout, access/service streets, and the original urban/rural/off-road types.
- Click-to-draw connected polylines, endpoint/segment snapping and automatic at-grade crossing subdivision.
- Select roads and **drag their endpoints**. Incident edges and new crossings are regenerated together; invalid duplicate/too-short moves are rejected atomically.
- Editable segment names, lane count, lane width, design-speed metadata, sidewalks, bicycle lanes, markings and bridge structure.
- Road deletion, undo/redo, local autosave, project JSON import/export and demo reset.
- Raise/lower terrain brush; surface roads regenerate to follow the height field.
- Draw railway and elevated monorail alignments from the Structures library. Undo their placement and retain them in project JSON.
- Scene-layer visibility, project minimap, quad-edge overlay, topology report and keyboard shortcuts.

### Included world

- A nonuniform, 100-segment starter road network with 35 three-/four-way junctions, a pre-placed connected roundabout and three landscaped Harbor Avenue blocks.
- Rural and urban roads, two river bridges, a lake, a meandering river, farmland, trails, building massing and low-poly trees.
- Marking previews: lane lines, rural center lines, edge lines, directional arrows, crosswalks, stop lines, green cycle lanes and bicycle symbols.
- Continuous curb/pavement ribbons with lowered flared, blended-corner and grooved ramps; tactile warning surfaces with actual truncated dome geometry.
- Granite, beveled-concrete and rolled curb profiles; slab/brick paving; perforated linear grates, open-throat curb inlets and inspection covers.
- Bridge decks with railings, posts, supporting beams and piers.
- Elevated dual-track railway and monorail corridors. Curved monorail departures use portal frames over roads, rather than placing columns in traffic lanes.

## Modern landscape & mobility profiles

The 100-road landscape fixture includes an already-placed roundabout; the current UI also appends Eastbank and opens there. **Street Tour → Roundabout / Garden / Cycle / Tram** frames each example without manual placement. **References** contains the four supplied design images, a researched green-tram thumbnail, guidance links, and a clear distinction between generated features and reference-only concepts.

Select any regular road and use **Landscape & mobility → Street cross-section**, or draw one of the three new presets from the library:

| Profile                | Generated cross-section (excluding outer sidewalks/gutters)                                                                 |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Garden boulevard       | 15.8 m total: two 3.1 m traffic lanes, 3.2 m planted median, two 2 m cycle tracks and two 1.2 m planted buffers             |
| Protected cycle street | 12.4 m total: two 3 m traffic lanes, two 2 m red cycle tracks and two 1.2 m planted buffers                                 |
| Green tram boulevard   | 19.6 m total: two 3.1 m traffic lanes, 7 m flush grass tram reservation, two 2 m cycle tracks and two 1.2 m planted buffers |

- Garden/buffer surfaces rise 0.15 m with beveled curb edges and ends. Planting opens at pedestrian crossings; trees and shrubs are kept back from those openings. These are concept dimensions, not a local engineering standard.
- Tram preview: two track pairs, 1.435 m gauge, reserved from motor lanes. The trackbed stays flush through crossings; rails do not climb over the planting beds. Matching straight-through tram junctions are connected. Open-end curved departures are generated; track switches, platforms, operational train routing and signal-control logic are not.
- Motor markings are offset around the median; car arrows and lane lines are not painted through planted or rail reservations. Cycling has separate red surfaces, bicycle symbols and crossing marks.
- Layout changes, width/lanes, surface meshes, planting positions and both views regenerate together. Incompatible bridge/odd-lane/narrow-sidewalk combinations are rejected without changing history.
- Older Westhaven saves get the roundabout only if its addition passes road and paving audits. Existing edits are not replaced. A pre-upgrade JSON snapshot is retained in `frontier-before-landscape-v1`. If placement is blocked, the save stays unchanged.
- In an older save, **References → Add 3 street examples** can apply the profiles to the three unchanged-position Harbor blocks. It preserves the graph and unrelated streets, makes an undo entry and stores `frontier-before-landscape-profiles-v1`. For moved/custom graphs, apply a profile to a selected road instead.
- `src/starter.js::landscapedProject()` is the current UI starter. `src/model.js::seedProject()` retains the original county as a regression fixture.
- The full protected-roundabout layout and Dutch continuous-footway side-road treatment in the supplied illustrations remain **reference studies, not generated junction types**. No accessibility, railway safety or stormwater-performance certification is implied.

Research and image provenance: [complete-streets design notes](docs/complete-streets.md).

```sh
npm run test:landscape                       # Default ring, tour, reference gallery, 2D/3D
LANDSCAPE_TEST_PHASE=edit npm run test:landscape       # Profile edit, undo/redo, rejection, save/export
LANDSCAPE_TEST_PHASE=migration npm run test:landscape  # Old-save preservation, examples, backup/reload
LANDSCAPE_TEST_PHASE=draw npm run test:landscape       # Draw a new landscaped road, undo
```

## Topology implementation

`src/model.js` stores nodes and road edges. At-grade intersections split both affected centerlines into a common node. Bridges do not split roads underneath them. Coincident/partially overlapping collinear roads are refused.

`src/geometry.js` then:

1. Sorts incident arms around each junction.
2. Reserves pavement-aware circular corner radii and shares the available road-mouth trim length between adjacent junctions.
3. Builds longitudinal road strips: two quads across conventional roads; shared cross-section partitions for landscaped roads, including real raised reservation surfaces without an underlying asphalt overlay.
4. Triangulates each junction polygon, refines the triangles incident to landscaped road mouths to match their cross-section cuts, then converts every triangle into three quads using shared edge midpoints and face centers. The initial triangulation avoids long chains of nearly collinear mouth points.
5. Welds vertices globally at a `1e-5 m` coordinate tolerance. Road mouths and junction patches share their edge vertices.
6. Retains quad faces in the source mesh and OBJ. Triangles exist only in the WebGL display geometry.

The diagnostic report checks:

- Repeated/degenerate faces and zero planar area.
- Duplicate faces and edges used by more than two faces.
- At-grade centerline crossings without shared nodes.
- Junction mouth seams without exactly two incident surface faces.
- Non-convex projected quad faces.
- Self-intersecting junction boundaries.
- Short road segments relative to their width.

The starter project contains **14,668 road/landscape quad faces** and **19,085 welded road vertices** and passes these checks. Regression tests include T, Y, four-way and straight mixed-width junctions, bridge separation, node editing, terrain updates, snapping, validation and OBJ export.

### What “quad-only” does not mean

Subdividing junction triangles produces extraordinary vertices. It does **not** guarantee a regular all-valence-four grid, optimal edge-loop flow, good aspect ratios, or uniformly sized faces. Road surfaces are intentionally open, not watertight solids. Global surface collisions, all possible near-parallel road overlaps, bridge clearance, and every extreme-angle/width combination are **not** comprehensively validated. The report is a set of concrete checks, not certification of production readiness.

## Exports and storage

- **Frontier JSON:** editable road graph, road parameters, terrain brushes, custom rail alignments and project metadata. Browser autosave uses `frontier-project-v1` in localStorage; export JSON for a portable backup.
- **OBJ:** partitioned road and junction surfaces, including cycle tracks, median/buffer surfaces and beveled reservation edges, in meters, Y-up, with four-index faces and segment/junction groups.
- **Selected streetscape OBJ:** curb/pavement ribbons, lowered ramps, drain geometry, adjoining corner pieces and expanded tactile domes. Four-index faces; separate material-named groups; surface UV coordinates included; no texture-image/MTL packing.
- Terrain, markings, buildings, vegetation, bridge supports and rail geometry remain viewport representations, not part of either OBJ export.
- No server persistence, collaboration or cloud synchronization is implied by the linked-view indicator.

## Important remaining work

This implementation does **not** yet meet the full requested AAA city-authoring specification. In particular:

- No spline-handle/curvature editor, superelevation or engineering-grade longitudinal alignment.
- No complete jurisdiction-specific marking/sign library, chevrons, turn-lane channelization, bus systems or parking simulation or speed-hump library.
- Curb/pavement ribbons now share cross-sections and contain real lowered ramps rather than overlaid wedge blocks. They are open surfaces, not a watertight terrain/road/sidewalk solid. Pavement curvature, face folding and same-level walkable-panel overlaps are checked; collision against buildings/landscape, extreme cases and engineering clearances still require production review. Ramp-grade warnings are not an accessibility certification: landing sizes, crossfall, tactile specifications and local standards must be reviewed.
- Bridges use procedural beam/rail/pier previews, not detailed structural bridge assets or validated clearance envelopes.
- Rail can be drawn but not selected and reshaped through dedicated rail-node controls. No track switches or operational signalling. Open-end curved departures and static level-crossing decks/gates/signals are implemented.
- River/lake shape editing and water simulation are not implemented.
- No baked material atlases, production LOD chains, collision meshes, lane traffic semantics, streaming, Unreal/Unity integration or comprehensive performance tooling.
- The terrain can be sculpted into steep slopes; grade limits and terrain-road cut/fill operations are not enforced.
- The starter scene is a compact county study, not a GTA-scale city.

These limits also appear in the in-app Help, Export and Topology dialogs.

## Controls

| Action                                   | Control                                         |
| ---------------------------------------- | ----------------------------------------------- |
| Select / drag selected endpoint          | `V`, then click / drag                          |
| Draw road                                | `R` or choose a preset                          |
| Draw bridge                              | `B`                                             |
| Draw rail / monorail                     | Choose the asset under Structures               |
| Terrain brush                            | `T`; Shift inverts raise/lower                  |
| Pan tool                                 | `H`                                             |
| Delete tool                              | `D`                                             |
| Delete selected road                     | Delete / Backspace                              |
| Toggle snapping                          | `G`                                             |
| Fit view                                 | `F`                                             |
| Finish drawing / close dialog            | Escape                                          |
| Fly/look (default 3D mode)               | Hold right mouse + WASD / mouse movement        |
| Fly vertically / move faster             | Right mouse + Q/E / Shift                       |
| Change fly speed                         | Right mouse + wheel, or viewport speed selector |
| Pan / dolly in fly mode                  | Middle drag / wheel without right mouse         |
| Orbit / pan / zoom (optional orbit mode) | Right drag / middle drag / scroll               |
| Undo / redo                              | Ctrl or Cmd + Z / Shift + Z                     |
| Save                                     | Ctrl or Cmd + S                                 |
| Search assets                            | `/`                                             |
| Shortcut reference                       | `?`                                             |

On narrow screens, the sliders button in the viewport toolbar opens the properties panel.

## Source layout

- `src/model.js` — graph operations, presets, demo project, terrain and import validation.
- `src/geometry.js` — quad generation, welding, diagnostics and OBJ serialization.
- `src/world.js` — Three.js scene, dual cameras, procedural assets, detail LOD and overlays.
- `src/fly-camera.js` — frame-rate-independent Unreal-style editor navigation, input capture and cleanup.
- `src/streetscape.js` — parameterized quad curb/ramp geometry, real drain openings, tactile instances and separate OBJ export.
- `src/street-materials.js` — locally generated concrete, stone, brick and asphalt textures.
- `src/surface-uv.js` — road/ribbon-local UVs, arc-length corner mapping, seam phase matching and OBJ texture coordinates.
- `src/main.js` — workspace UI, inspector, history, drawing tools, dialogs and storage.
- `src/style.css` — responsive editor layout and visual system.
- `tests/geometry.test.js` — deterministic model and topology tests.
- `tests/browser.mjs` — pointer-driven browser smoke checks, export and persistence tests.

## Reference-inspired curbs and Unreal-style navigation

Select a road with sidewalks, then use **Properties → Curbs, ramps & drainage → 6 reference designs**. The same preset chooser is under **Structures → Curbs, ramps & drainage**. The six supplied photographs are retained in `public/references/` as design references, not presented as renders or applied as photographic textures.

The presets cover granite + linear drainage, a flared tactile crossing, an open stormwater inlet with concrete gutter, a lowered blended corner, a brick drop crossing, and a grooved concrete ramp. They apply to the selected road, set a 2.4 m sidewalk, and move the camera to the detail. All new road properties persist in project JSON and participate in undo/redo.

**Inspect at street level** frames a crossing; **Inspect drainage** frames a drain on that segment. Use the **Fly camera / Orbit camera** switch above the viewport. In fly mode, hold RMB to look and move with WASD, Q/E for world-vertical travel, Shift for a 4× boost. RMB + wheel adjusts speed; wheel alone dollies. Releasing RMB, cancelling a pointer, losing focus, hiding the page or switching to plan mode clears held keys. Text inputs and dialogs do not capture movement keys. This is a no-collision editor fly camera, not a character controller.

### Geometry and performance notes

- A longitudinal/cross-sectional quad lattice replaces the raised curb inside each ramp. There is no full-height curb left underneath the lowered portion. Crossing lips are 6 mm; flares and a back landing blend into the surrounding sidewalk.
- Ramp placement follows existing urban crossing stations. Sidewalks narrower than 1.2 m omit ramps. The inspector reports cross-sectional ramp grade and warns above 1:12; terrain grade, landing clearance and regulatory requirements still need review.
- Granite joints use dedicated narrow face bands. Grate bars form a perforated plate with top/bottom faces and slot walls; curb inlets omit the front wall and provide a recessed back wall and a cap underside. These are modeled openings, not dark decals.
- Tactile domes use an all-quad prototype and instanced rendering. Domes within 20 m are rendered in perspective; distant/plan views retain the warning-strip surface without rendering subpixel domes. Export expands all tactile instances regardless of view LOD.
- The road report remains road-only. **Inspect streetscape quads** opens a separate face-area, duplicate, winding, folded-panel, clearance and positive-area overlap report, and reports steep ramps. The wireframe overlay includes the curb/pavement/drain base meshes. Tactile prototypes are tested independently and expanded into the streetscape OBJ.
- Drainage placement is procedural on sufficiently long, non-bridge sidewalk segments, kept away from ramps. No hydrological simulation, pipe network, water flow or structural certification is implemented.

There are **70 model/geometry tests**, including each streetscape preset, physical grate-slot ray tests, inlet recesses, ramp heights, narrow-sidewalk warnings, dome/export indexing and import validation. Additional browser tests cover RMB/WASD, key-conflict protection, blur/release cleanup, navigation switching, reference presets, dimensional controls, persistence and streetscape downloads:

```sh
node tests/streetscape.browser.mjs
# Optional smaller runs in slow software-rendered environments:
FRONTIER_TEST_PHASE=camera node tests/streetscape.browser.mjs
FRONTIER_TEST_PHASE=storage node tests/streetscape.browser.mjs
```

## Tight-corner repair, wider paving and dark UI

The former corner construction offset a tight curve by a sidewalk wider than its radius. That crosses the curvature center and reverses the back edge of the paving, even when the individual quads have nonzero area. The replacement changes the construction rather than concealing the bad faces:

- Circular fillets use exact radial normals, tangent lead-ins and matching normals at straight-road mouths. Near-straight transitions use a tangent-matched curve. Collinear boundary samples are removed before road triangulation.
- The preferred road-edge radius includes the **whole pavement + gutter envelope plus 1.5 m of inner reserve**. When neighboring junctions compete for a short road, their trims share the available length. The actual remaining inner radius is checked; widths are not silently reduced to make an impossible layout pass.
- Default pedestrian widths are **2.4 m neighborhood / 3.2 m avenue / 1.5 m shared lane**. Saved projects with the exact old built-in widths are upgraded once; non-default widths and the road layout are preserved.
- Each edit is preflighted against road topology and streetscape geometry. Newly invalid edits from a clean project are rolled back without adding an undo entry. For impossible acute corners, spread connected road nodes apart or explicitly reduce the paving width. Invalid streetscape OBJ exports are blocked.
- Walkable panels are checked for reversed winding, bowties, collapsed area and positive-area overlap, using spatial hashing and polygon clipping. Shared seams are allowed; vertically separated bridge panels are excluded. These checks do not count intentionally overlaid tactile tiles/grille faces as sidewalk collisions, and are not a general building/terrain collision engine.
- The status bar includes pavement health. **Inspect streetscape quads** gives separate failure counts and affected panel/corner IDs; the road report also links to this report.
- **Charcoal/black UI is the default, not navy.** The header's Light UI / Dark UI button saves the preference separately from the project. Logarithmic depth buffering also fixes the broad road/terrain z-fighting visible from the county overview. Selection handles remain small at street-level camera distances.

### Continuous mixed-road edges

- All junction corners receive an edge return, including country/trail shoulders and urban-to-rural connections. Soft shoulders are 0.9 m wide and now belong to the audited/exported streetscape mesh rather than separate straight-only display strips.
- Each corner has one paving material owner: the paved arm takes priority; otherwise the wider pavement wins, with a stable road-ID tie-break. Its paving, curb and gutter materials continue around the whole corner, while the cross-section adapts to the adjoining widths.
- Different paved styles change at an existing crossing-ramp center where there is one, otherwise at a clean road-mouth material joint. Texture repeats restart at these dividers so the carried corner material stays aligned. Paved-to-shoulder returns taper down near the rural end instead of disappearing.
- Missing returns and mismatched welded mouth profiles are reported and block invalid edits/exports. Partial selected-road exports explicitly retain external boundaries. Detailed channel-to-shoulder subdivisions are covered by seam regression tests too.
- Rural roads now have an **Inspect edge connections** report and shoulder OBJ export. Short, steep edge terminations are reported for review, not advertised as accessibility-compliant.

### Verified 100-road landscape fixture results (before Eastbank is appended)

| Check                                        |    Result |
| -------------------------------------------- | --------: |
| Road surface quads                           |    14,668 |
| Streetscape quads, excluding instanced domes |   141,962 |
| Degenerate / duplicate streetscape faces     |     0 / 0 |
| Folded / inverted top faces                  |     0 / 0 |
| Folded / inverted / collapsed walking panels | 0 / 0 / 0 |
| Positive-area walking-panel overlap pairs    |         0 |
| Completed corner returns                     |       188 |
| Missing returns / mouth seam errors          |     0 / 0 |
| Steep paving-to-shoulder terminations        |         0 |
| Insufficient corner clearances               |         0 |
| Lowest checked inner radius reserve          |   0.290 m |
| Lowered ramps / drains                       | 184 / 108 |
| Ramps with grade warnings above 1:12         |         8 |

The remaining ramp-grade warnings are exposed, not presented as accessibility compliance. All counts describe the starter network, not a guarantee for every arbitrary imported layout.

The automated geometry/model tests include acute angles down to 8°, mixed pavement widths and profiles, near-straight corners, the reported Westhaven street names, deliberately folded/overlapping negative cases, exact seam normals, unsafe-export rejection and saved-width migration. Browser regression exercises theme persistence, rejected widening with intact history, safe widening/undo, diagnostics, camera controls, and the existing road/rail/terrain workflow.

```sh
npm test
npm run test:browser
npm run test:corners
npm run test:transitions
# Camera/preset and save/export regression, in smaller software-rendered runs:
FRONTIER_TEST_PHASE=camera npm run test:streetscape
FRONTIER_TEST_PHASE=storage npm run test:streetscape
npm run build
```

## Directional materials and concrete gutters

Paving is no longer projected onto a world-aligned X/Z grid. Each strip carries face-corner UVs in its own along-road/across-profile coordinates. Corners use cumulative arc length on the front paving rail; transverse joints turn with the bend, and longitudinal joints follow the curb. Concrete slab repeats are approximately 600 mm (adjusted slightly to fit whole texture repeats between mouths). Cross-profile coordinates include the curb face and ramp slope rather than collapsing vertical faces. Whole repeats at the mouths keep the starter network's concrete slab pattern continuous across welded straight/corner seams.

Inlet gutters now use **cast concrete**, not the previously hard-coded brick texture. Concrete slabs are still the default footway; brick paving is available only as an explicit paving/preset choice. Motor-vehicle lanes remain asphalt; modern profiles separately partition colored cycle tracks and planted/transit reservations. Road selection is an outline instead of an orange fill, so selection does not recolor the surface. Access-cover pattern coordinates also follow the local road direction.

Road and streetscape OBJ exports now contain `vt` coordinates and four-corner `v/vt` face references for their base surfaces. Positions remain welded; UV seams have separate indices. Tactile domes retain solid-color geometry without texture coordinates. Procedural texture images and MTL files are not bundled. Multi-arm asphalt junctions use a nondirectional planar mapping because they have no single travel direction.

Material regressions cover rotation/translation invariance, curved UV rows, shared-mouth slab phases, noncollapsed UVs, preserved topology, concrete gutter defaults, opt-in brick, renderer UV retention and valid OBJ indices. The browser test checks the actual selection outline, material edits, save/reload and UV-bearing download, and captures Cedar Street's ramp and Foundry Street's curves.

```sh
npm test
npm run test:materials
npm run build
```

## Roundabouts, access roads and formed water channels

### Place a roundabout

Choose **Roads → Roundabout** at the top of the asset library. Set a **24–50 m centerline radius**, **three or four entry roads**, rotation and clockwise/counterclockwise circulation. Click clear ground in either view; the preview includes the ring and the 32 m entry stubs. Pan if a UI overlay is covering your intended location. The current starter already contains a checked **28 m / four-entry assembly at X −230, Z 100**, connected into the country road. Use **Street Tour → Roundabout** to find it. That location is now occupied; place additional rings elsewhere.

The tool builds a connected graph of twelve circulating segments with large, tangent-matched fillets and three/four entry junctions. It is not an asphalt disk placed over existing roads. The central island remains open terrain, with a rolled curb and pedestrian/apron strip around it. Circulation arrows and entry give-way markings are viewport geometry. Existing roads in the ring footprint are protected from being covered; entry intersections use the normal road splitting/merging and geometry preflight. Crop strips are clipped away from roads and the island instead of crossing through newly placed geometry.

Connect more roads to the entry endpoints using the road tool. Placement is one undo operation; JSON preserves the graph and circulation metadata, and road OBJ export includes the ring and welded entries. After placement the nodes are editable graph geometry, not a locked radius-controlled parametric object. **Frame roundabout** frames the assembly. Deleting a circulating segment prompts to remove the whole tagged assembly; connected roads outside the assembly are kept, and Undo restores it. Entry segments can be removed individually.

Two additional drawing presets are available: **Access street** (two 2.8 m lanes, 2.4 m walks, 20 km/h) and **Service lane** (one 4.2 m shared access lane, 1.8 m walks, 15 km/h, spoon/weep drainage).

### Three new curb / water-channel designs

Select a road, then open **Curbs, ramps & drainage → 9 reference designs**:

| Design                   | Profile and formed bed                                             | Collector                                         |
| ------------------------ | ------------------------------------------------------------------ | ------------------------------------------------- |
| Brick roll + collector   | Rolled brick curb; 0.58 m dished, road-aligned brick-course gutter | Six open grille slots                             |
| Broomed spoon + weeps    | Rounded concrete toe; 0.62 m broom-textured spoon channel          | Four rounded weep openings                        |
| Dished curb + slot drain | Deep dished curb; 0.52 m concrete channel                          | Continuous 1.8 m open slot with narrow metal rims |

The water bed is a shaped quad surface, with additional local depression toward the collector. Holes remove actual top faces and have recessed walls/floors; they are not painted black dots on a sealed surface. The curb/pavement topology and directional UVs are preserved around corners. Choosing a formed curb from the inspector also selects its matching channel; an ordinary open curb inlet uses a vertical concrete profile.

Presets keep a 2.4 m sidewalk and frame the new collector automatically. Collectors require a non-bridge approach longer than 13 m with room clear of crossing ramps. Short approaches retain their formed bed but omit the collector. These are modeled surface drainage details, **not hydraulic simulation, stormwater-capacity validation, or a connected underground pipe network**. The local collector depression does not guarantee drainage on every terrain grade. Roundabout signage, splitter islands, swept vehicle paths and traffic simulation still require further work; the markings are not jurisdictional certification.

The original six reference designs remain available. Concrete is still the default for ordinary inlet gutters; brick is used only when explicitly chosen. The brick and weep cards use the supplied photographs; the slot card is a labeled design diagram.

### New verification

The 70 model/geometry tests include six radius/entry/circulation roundabout combinations, open-island raycasts, entry continuations, atomic footprint rejection, mixed channel corners, short approaches and saved metadata. Raycasts confirm that all three collectors have genuinely open top surfaces leading into a lower recess. The starter network with each new design applied to Cedar Street passes fold, inversion, overlap, degenerate-face and duplicate-face checks.

```sh
npm test
FRONTIER_TEST_PHASE=channels npm run test:road-types
FRONTIER_TEST_PHASE=roundabout npm run test:road-types
npm run build
```

Browser checks exercise actual pointer placement, each new preset, diagnostics, quad/UV downloads, persistence, assembly undo/redo and confirmed removal. The per-phase commands keep the software-rendered test runs shorter.


## Parking, bridges, rail departures and saved-map repair

Use **Parking**, **Bridges**, **Rail crossing** and **Rail exits ↗** in the sample toolbar. The expanded map has **145 road segments**. The sample picker includes both new bridge routes and four Eastbank railway/monorail alignments.

- **Parking-protected street:** 2.2 m parallel lanes with 6.4 m bays, a 0.9 m clear door buffer and 2 m cycle tracks on each side. No shrubs are placed in the door buffer. Bays are set back 10 m from trimmed junction mouths. Cars are static viewport illustrations, not traffic/parking simulation or accessible-parking provisions.
- **South bowstring / North girder bridge:** connected approaches, continuous deck slabs, girders, outer pedestrian barriers, piers and (on the south bridge) arched ribs/hangers. Both new bridges cross a road at a different level; their tested underpass clearance exceeds 5.5 m below the modeled girders. General user-drawn bridges are not clearance-certified.
- **Rail exits:** choose an open-ended tram street, railway or monorail, left/right and a 30–200 m radius. Creates/replaces an independent tangent-continuous departure. Ground rail matches the street's 1.435 m gauge and 3.3 m track spacing. Attached routes update after endpoint moves. This is a departure, not a turnout/switch in a running track. Existing arbitrary polyline rail drawing is still supported.
- **Rail crossings:** inferred from ground rail/road intersections; two examples are prebuilt. Road surfaces are partitioned into crossing decks, rails stay flush, ballast/sleepers are omitted across the road, and static crossbucks, red lights, barriers and approach stop lines are provided. Stop lines are 4.8 m beyond the projected nearest rail. New edits reject crossings below 60°, within 18 m of a road end, or through raised sidewalks/landscaped medians. Such urban crossings need a dedicated lowered-footway design not implemented here. Elevated crossings do not receive gates. There is no operating signal/interlocking system.
- **Repair Eastbank:** restores only tagged generated district roads and the four named sample rail corridors at the original city origin. It preserves Westhaven, terrain and untagged custom roads, validates the complete candidate and rejects conflicts atomically. Edits to the generated district itself are replaced. A full backup and Undo are provided; **Backup ↓** downloads the prior JSON.
- On a pre-transport save, the update first backs up to `frontier-before-transport-v1`. A detected invalid generated district is rebuilt with a visible notice and an undo snapshot; a healthy district is extended without replacement. Manual repairs use `frontier-before-eastbank-repair-v1`. If backup storage or validation fails, the transport update is not applied.
- Road edits now reject every road-audit issue, including short residual stubs. Crosswalks share the paving ramp-placement rules; arrows and cycle symbols are restricted to usable trimmed road lengths.

Research and explicit prototype limitations: [transport design notes](docs/transport-design.md).

```sh
npm run test:transport
TRANSPORT_PHASE=repair npm run test:transport
```


## Reliable hosted preview and startup recovery

For the hosted editor, use `npm run build && npm run preview` (port 5173).
The compiled preview avoids Vite's development-module waterfall and does not request
`env.mjs` or the HMR websocket. Both development and preview servers bind to
`0.0.0.0` and accept Arena's `.arena.site` and legacy `.e2b.app` hosts.
Development with automatic source reload remains available through `npm run dev`.
After source changes, rebuild before using the compiled preview.

Startup shows six preparation stages, yielding for a browser paint between heavy
operations. A rejected main-bundle import or initialization error leaves a recovery
screen with Retry and Download saved map. Recovery exports the original local
`frontier-project-v1` bytes without parsing, resetting or replacing them. Keep the
same preview origin to retain access to browser-local saves. Individual geometry
phases are still synchronous: this is not a worker-based or instant startup.
Material-filtered geometry now allocates only the referenced vertices instead of
copying the complete city vertex table for every material; triangle positions,
normals and UVs are regression-tested against the previous output.

`npm run test:startup` checks the compiled build using an `.arena.site` hostname
mapped locally to the server, verifies fresh start and persisted reload, checks
that only bundled scripts are fetched, and simulates a failed main bundle to
verify the recovery download. It does not authenticate or test Arena's own proxy
and realtime session services. The supplied `main.js` 500 / `env.mjs` 502 were not
reproduced locally; a separate host-allowlist 403 was reproduced and fixed. Arena
realtime 401s and report-only image CSP messages are outside this application's
startup code.

Verification after this fix: 128 unit tests; production build; startup/recovery
browser checks; transport browser checks including rail-exit creation and Undo;
and damaged-Eastbank repair with backup, custom-road preservation and reload.
The repaired fixture is not the exact saved JSON from the reported screenshots.

# RoadForge — WebGPU road network generator

RoadForge is a standalone, dependency-free web application that generates high-fidelity road networks:
spline roads, merged single-mesh intersections, structures (bridges, underpasses, tunnels, interchanges),
railways with trains, procedural PBR surfaces (asphalt, kerbs, paving patterns, markings) and AI traffic —
all rendered with a custom **WebGPU** renderer. There is no node editor: the workflow is *generate the
network → tune it with toggles and settings → texture through the layer stack*, with the layer stack and
inspector on the right-hand side of the screen (FrontierEditor look).

```
RoadForge/
  index.html            development shell (loads src/*.js in dependency order)
  RoadForge.html        single-file build (scripts, stylesheet and fonts inlined) — open directly in a browser
  styles.css            UI stylesheet (FrontierEditor design tokens: DM Sans, graphite panels, 18 px cards)
  src/                  application source (classic scripts sharing the global `RF` namespace)
  tools/                Node test / build tools (no npm install needed)
  fonts/                DM Sans + IBM Plex Mono (OFL)
```

Open `RoadForge.html` (or serve the folder and open `index.html`) in Chrome / Edge 113+, Firefox 141+ or
Safari 26 — WebGPU must be available. Rebuild the single file with `node tools/build-standalone.mjs`.

## Geometry pipeline

| Stage | Module | What it does |
| --- | --- | --- |
| Profiles | `profiles.js` | Lane layouts (local street → 3+3 motorway, one-way carriageways, ramps, loops, tunnel). Real dimensions: 3.3–3.65 m lanes, 2.3 m parking strips, 4.2 m sidewalks, 150 mm kerbs with 15 mm chamfer. |
| Network | `network.js` | Samples each road spline (adaptive, 0.5–2 m stations), classifies nodes (orphan / end / pass / transition / junction) and solves every junction: extended edge polylines of neighbouring arms are intersected, a tangent-continuous fillet is fitted beyond the intersection (R by profile, reduced to a 1–2 m *nose* for gores under 35°), the arms are trimmed back to the fillet tangents and the junction boundary is assembled. |
| Blocks | `blocks.js` | City blocks traced live from the network: every closed face becomes a block bounded by the footway outer edges (junction corners follow the kerb fillets); corners on the outside of the network get roadside blocks of a configurable depth. Blocks move with the junctions; surfaces can be overridden or hidden per block. |
| Road mesh | `roadmesh.js` | Quad strips per lane band with crossfall and crown, kerbs, sidewalks with dropped-kerb ramps and tactile paving, grass verges/embankments, speed tables, potholes (displaced vertex rings), works zones, markings (lane/edge/centre lines, stop lines, crosswalks, turn arrows chosen from the permitted movements, parking T-marks), junction patches as welded Coons fans with through-lane continuation and merge/diverge lines. |
| Structures | `structures.js` | Per-station classification (ground, embankment, bridge, cutting, retained, tunnel). Embankments become decks only where they pass over another road or railway corridor (span = corridor + verge + slope run); box girders ≈ span/22 deep, 0.45 × 0.95 m parapets, Ø1.5 m piers that are slid out of lower carriageways; retaining walls and stencil ground holes for underpasses; tunnel boxes and portals. |
| Rail | `rail.js` | Track (UIC-54 style rail profile, instanced sleepers, ballast prism in a drained trench or on a bridge), level crossings with panels, barriers and crossbucks, catenary, plate-girder rail bridges, subway tunnels, and trains that run on the line. |
| Props | `props.js` | Street lights, traffic signals ("robots"), signs, 6 bollard families, pedestrian railings, W-beam guardrails, billboards, parked cars — placed from the network with real spacing rules. |
| Traffic | `traffic.js` | Lane graph with Bézier connectors, IDM car-following, signal phases, ≤ 25 vehicles. |

Topology rules: everything is built from **quads** (triangles only appear in city-block interiors); all
surface layers are checked to face +Y; junctions are single welded manifolds — there is never a road
drawn through another road.

## UI

* **Top bar** — project name, tools (Select, Road, Rail, Block, Pan), view toggles (Plan, X-ray, Topology
  wireframe, Traffic), live quad/tri/draw-call stats, undo/redo, export (OBJ+MTL with quads, project JSON,
  import, new) and help.
* **Left: Outliner** — templates (8 scenarios), junctions, roads, railways, city blocks and diagnostics, with
  search and visibility eyes.
* **Right: Layer stack** — Network · Surface · Furniture · Traffic · Environment. Selecting a layer opens
  its card in the inspector (asphalt colour/wear/cracks/patches/wetness, kerb material, paving pattern and
  colour, marking standard EU/US/UK/ZA, crossfall, lighting spacing, bollard and railing designs,
  guardrails, billboards, traffic count, trains, time of day, sun azimuth, cloud, fog, exposure).
* **Right: Inspector** — the selected junction (control type, corner radius, position with gizmo), road
  (profile, control points, potholes, speed bumps, works zones, bollards, lighting, structure override —
  bridge / tunnel helpers), railway or block.
* Viewport: left-drag gizmo / handles, right-drag orbit, shift/middle-drag pan, wheel zoom, double-click a
  road to insert a control point, `F` frame, `Del` delete, `P` plan, `W` topology, `X` x-ray.

Templates: *Downtown crossroads*, *Downtown grid* (boulevard, one-way pair, generated city blocks), *Diamond interchange*,
*Cloverleaf* (parametric, R 62 m loops / R 400 m connectors), *Rail district* (level crossing, underpass,
overbridge, subway), *Rural motorway* (embankment, overbridge, works zone, billboards), *Street works* and
*Showcase city* — everything in one map: a bus-lane high street (red surfacing, BUS lettering), a boulevard with
green cycle tracks and cycle symbols, a cycle street, a one-way pair, on-street parking, a park and a plaza,
two level crossings and a rail overbridge, a metro line, a diamond interchange with billboards and a works
zone, speed tables, potholes and six bollard designs.

Lane designs: any road can be given bus lanes and cycle lanes (per direction) from its inspector; the
`High street (2+2, bus lanes)` and `Cycle street (1+1, cycle lanes)` profiles ship ready-made.

## Tools

```
node tools/test-geometry.mjs      # junction solver scenarios (cross, acute merge, Y, skew, 5-arm, bend)
node tools/test-scene.mjs [name]  # builds every template headless: quads, NaN check, down-facing surface check
node tools/template-svg.mjs name  # plan-view SVG of a template's centrelines, trims and nodes
node tools/plan-svg.mjs scenario  # plan-view SVG of generated quads for topology inspection
node tools/build-standalone.mjs   # bundle into RoadForge.html
```

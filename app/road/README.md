# Frontier · Road Editor prototype

A browser prototype of the engine's road authoring toolchain — the corridor
modelling side of the editor, built to the visual language of
`Experimental/FrontierEditor` (Project Zero): graphite panels, uppercase
micro-labels, oversized light metrics, semantic colour only on icons.

Open **`app/road-editor.html`** over HTTP (modules + ES imports need a server):

```
cd app && python3 -m http.server 8123 --bind 0.0.0.0
# → http://localhost:8123/road-editor.html
```

No build step, no bundler, no CDN: `three.module.js` (r169) is vendored in
`app/vendor/`, Inter variable in `app/fonts/`, and every surface texture, road
mesh, marking and prop is generated procedurally at load.

## Layout

```
┌───────────────────────────────────────────────────────────────────────┐
│ Scene · Edit · View · Build · Help     Fly/Walk/Orbit/Drive   Save ▶  │
├───────────┬───────────────────────────────────────────────┬───────────┤
│ Outliner  │            viewport (WebGL)                   │ Details   │
│ layers    │  tools · snapping · view flags · minimap ·    │ Construct │
│ roads     │  legend · camera hint                         │ Generate  │
│ junctions │                                               │  tabs     │
├───────────┴───────────────────────────────────────────────┴───────────┤
│ fps · draws · tris · instances · build ms · coords · seed · mode      │
└───────────────────────────────────────────────────────────────────────┘
```

* **Left** — outliner grouped by road class (freeways, avenues, collectors and
  local streets, bus & cycle, pedestrian & paving, rail/tram/metro, structures),
  plus junctions and open space. Per-object eye icons hide a corridor (it is
  skipped on the next rebuild); the top group toggles the 14 render layers.
* **Middle** — the viewport. Floating tool group (select, draw, edit alignment,
  edit height, paint features, measure, delete), snapping group, view-style
  segment (Lit / Unlit / Mat ID / Wire) and the flag toggles (shadows, grid,
  markings, furniture, traffic, massing, x-ray). Minimap with camera frustum;
  clicking it frames the nearest corridor.
* **Right** — three tabs: **Details** (inspector cards for the selected road,
  junction or area), **Construct** (searchable assembly library with live
  cross-section thumbnails) and **Generate** (parametric city). `⇧A` opens the
  construct picker for the current selection, like the engine editor.

## Camera

Unreal-style: **hold RMB** to look, `WASD` to move, `Q`/`E` down/up, `Shift`
sprint, `Alt` slow, wheel to change speed. `Walk` clamps you to the road level
and eye height, `Orbit` pans the selected corridor, `Drive` puts the camera in
the nearside lane of the selected road at its legal speed (`[`/`]` hop between
corridors, `Esc` to get out).

## What the model covers

43 cross-section assemblies (`profiles.js`) assembled from typed elements —
travel/turn/bus/cycle/parking/loading lanes, gutters, bull-nose and high
kerbs, medians, jersey barriers, footways, shared paving, verges, fences,
retaining walls, paths, rail and tram tracks, tunnel bores, water. Each
assembly carries design speed, crossfall, anchor, minimum radius, markings
and a furniture recipe, so a road can be re-profiled without touching its
alignment.

Markings: centre and lane lines (broken, solid, double yellow/white/red
route), edge lines, zebra/continental/ladder crossworks with tactile
corduroys, give-way teeth, chevrons, hatching, keep-clear boxes, direction
arrows, SLOW/BUS/CYCLE word marks, cycle stencils, parking stalls, rumble
strips, manholes and gullies.

Everything else that makes a street: bridges and viaducts (soffit, edge
beams, parapets, expansion joints, piers, abutments), tunnels and underpasses,
subway bores with stations, escalators, entrances and vent towers, level
crossings with blinking beacons and animated barrier arms, speed cushions and
flat tables, raised crossing tables, driveway aprons that drop the kerb,
bus shelters and flags, parking meters, bollards, benches, bins, planters,
hydrants, guard rails, catenary, ~97 prop prototypes in total.

Surface materials (asphalt, new/dark asphalt, in-situ concrete, granite setts,
brick, herringbone pavers, gravel, mown grass, tile, timber, red and green
asphalt, steel, water) are procedural canvas height fields packed into colour,
normal and roughness maps, tiled by arc-length so stripes and joints run with
the road.

## Files

| file | role |
|---|---|
| `lib.js` | THREE-free maths: builder with per-material buckets, RNG, resampling, RDP, ear-clipping triangulation, hull |
| `profiles.js` | element factories, the 43 assemblies, `solveSection`, `levelAt`, surface and class tables |
| `props.js` | prop prototypes (geometry recipes) and their material table |
| `mesh.js` | corridor meshing, markings, features, structures, junction plates, water, areas, `validateNetwork` |
| `generator.js` | parametric city: lattice, river, railway, metro, bridges, districts, parks, culs-de-sac, massing |
| `textures.js` | procedural surface, decal-glyph, sky-environment and lamp-glow textures |
| `scene.js` | renderer, materials, instancing, camera modes, picking, overlays, traffic, sun and shadows |
| `ui.js` | panels, tools, undo, persistence, import/export, minimap, shortcuts |
| `road.css` | the chrome (Project Zero tokens) |

## Data contract

A scene is `{ roads, nodes, water, areas, massing, params }`. Roads are
`{ id, name, cls, profile, points:[{x,z,y}], … }`; per-road tweaks are stored
by cloning the profile onto the road (`road.profile = { …assembly, _base }`),
which is what the inspector does and what the JSON export round-trips. Export a
manifest CSV from `Scene ▸ Export road manifest` to feed a build step.

## Status

Prototype: geometry, materials, markings, structures, generator and editing are
implemented and exercised; the following are deliberately not wired up yet —
GPU picking, drag-undo granularity, junction handle manipulation, terrain
heightfield (the datum is flat), and per-instance lamp light sources (night uses
glow sprites instead).

# RoadForge — Procedural Road Studio

A standalone road-network authoring demo in `index.html`, styled as a compact Frontier-style outliner / inspector workstation. It has no dependency on the fracture demo, the native engine, a node graph, or remote assets.

## Run

Serve this directory from a local or HTTPS origin so the browser can request WebGPU:

```sh
cd Experimental/RoadForge
python3 -m http.server 4173 --bind 0.0.0.0
```

Open `http://localhost:4173/` in a current Chromium-based browser with WebGPU enabled. The Arena preview is served from a secure origin. A vector compatibility view is provided when `navigator.gpu` or a GPU adapter is unavailable; GPU shading and mesh rendering require WebGPU.

## Included

- Urban grid, suburban Y-network, and grade-separated highway interchange presets, generated from a reproducible seed.
- Editable cubic road splines, real-world lane widths, speed classes, signalized nodes, road splitting, movable junctions, and a compact network map.
- Road ribbons authored as quad strips; intersection footprints are shared convex patches, with adjacent spline ends clipped to the patch boundary. WebGPU triangulates the quads for rasterization; OBJ export includes a matching material palette.
- Procedural world-space asphalt grain, weathering, crack masks, paver joints, thermoplastic lines, sidewalks, curbs, crossings, speed tables, pothole / repair details, city-block paving and parking courts (no buildings), street furniture, autonomous delivery robots, a viaduct, and a rail underpass with a moving train.
- A draggable surface layer stack, right-side outliner and inspector, local project autosave, JSON project export, and OBJ / MTL mesh export.
- Lane-following AI traffic and parking props with a combined hard cap of 25 visible vehicles; the live AI count is clamped to that budget.

## Controls

- `Q` select, `W` move, `R` draw a road, `J` split a road / add a junction, `M` measure.
- Drag the amber intersection gizmo to move a junction and its connected splines together.
- Drag empty viewport space to orbit; middle/right mouse or `⌘` / `Ctrl` drag to pan; scroll to zoom; double-click or `F` to frame.
- `7` top view, `G` ground-grid toggle, `Space` pause / resume traffic, `⌘` / `Ctrl` + `Z` undo, `⌘` / `Ctrl` + `E` export.

The interface is adapted from the dark, restrained outliner / inspector language of the referenced Frontier experimental editor; this prototype uses ordinary toggles, settings, and a layer stack rather than a node editor.

# Frontier

A WebGPU terrain generator with **hybrid heightfield + SDF/voxel** geometry. It uses a layer stack and toggles instead of a node graph.

The app lives in [`terrain/`](terrain/) and is plain HTML, CSS and ES modules, with no build step.

```bash
cd terrain
python3 -m http.server 8080      # any static server works
# open http://localhost:8080 in a browser with WebGPU (Chrome/Edge 113+, Safari 18+, Firefox 141+)
```

## Using it

The **inspector** (left panel) has four tabs:

| Tab | What it holds |
|---|---|
| **Terrain** | Height layer stack: generators (fBm, ridged, billow, Voronoi, mountain, dunes, island, canyon, coast, erosive noise, constant), filters (domain warp, terraces, curve/levels, smooth), SDF primitives (spires, arches) and simulation (hydraulic droplet erosion, thermal erosion, flow/rivers). Each layer has a blend mode (replace, add, subtract, multiply, max, min), opacity and a mask (height, slope or noise). |
| **Volume** | Hybrid settings (heightfield → voxel threshold, voxel size, transition width, region preview), plus volumetric detail that only exists in voxel regions: strata, overhangs/undercuts, roughness, rills and fractures. |
| **Surface** | Material stack. Each layer is driven by height, slope, curvature, protrusion, AO, flow, wear and deposition masks, with noise breakup, a detail "stamp" type (rock, sand ripples, vegetation, snow, scree, soil), bump and roughness. |
| **World** | Resolution (256 to 2K heightfield, plus a lower preview resolution while dragging), world size, height scale, seed, render scale, MSAA, shadows, diorama skirts, sun and sky, ocean, post, and **export**. |

Other controls:
- **Layer stacks:** drag to reorder, eye icon to toggle, double-click to rename, and duplicate/delete from the row menu.
- **Undo/redo:** Ctrl+Z / Ctrl+Shift+Z.
- **Persistence:** state is autosaved to `localStorage`.
- **Viewport:** LMB orbit, RMB pan, wheel zoom, WASD move, F frames the terrain.
- **View modes:** the top bar switches between lit, albedo and each analysis map (height, slope, curvature, protrusion, occlusion, flow, wear, deposition, hybrid mask, normals).
- **Presets:** Alpine Range, Sea Cliffs, Stone Forest, Desert Mesas, Dune Sea, Icelandic Highlands, Tropical Island and Canyonlands.
- **Exports:** in the World tab — 16-bit heightmap PNG, normal map, analysis-map PNGs (curvature/AO/flow/mask and deposition/wear), OBJ (heightfield plus voxel mesh, with normals) and screenshots. Save/open preset JSON from the Presets menu.

## How the hybrid works

Everything runs as WebGPU compute shaders.

1. **Heightfield stage** (`wgsl/height.js`)
   - Layers are evaluated bottom→top into one float heightfield.
   - SDF primitives are placed on the CPU and stamped on the GPU. Their footprint is written into the heightfield, so later erosion layers see them.
   - Hydraulic erosion, thermal erosion and flow write **wear** and **deposition** maps.
   - Because all of this happens on the shared heightfield before any representation split, erosion affects heightfield and voxel regions identically.
2. **Derive stage** (`wgsl/derive.js`)
   - Computes normals, slope, curvature, protrusion, AO and flow.
   - Builds the **hybrid mask**: steepness / texel stretching relative to the threshold, plus primitive overhang regions. The mask is max-blurred and smoothed into a soft voxel weight.
   - Per-column stats give the vertical bounds for meshing.
3. **Volume stage** (`wgsl/volume.js`, `wgsl/mesh.js`)
   - Inside masked bricks (16³ voxels), the density is `heightfield SDF ∪ primitive SDFs`, plus volumetric detail weighted by the mask.
   - It is meshed with marching cubes on the GPU, emitting per-vertex normal, AO and blend weight.
   - Away from cliffs the density *is* the heightfield's SDF, so the voxel surface coincides with the heightfield there. The heightfield grid is discarded under voxel regions, and normals are blended by weight, so the seam is invisible.
4. **Render** (`wgsl/render.js`)
   - Draw order: sky, heightfield grid (vertex-displaced, with the voxel-covered parts clipped), voxel mesh, strata skirts, Gerstner-style ocean (shore foam, depth absorption, refraction tint, footprint-filtered waves) and the ACES post pass.
   - Shadows come from a cached shadow map that is redrawn only when the terrain or sun changes.
   - Texture detail is faded by pixel footprint to avoid aliasing.

Edits are routed to the cheapest stage that has to re-run (`schema.js` → `LEVEL`). A material tweak re-renders, a volume tweak re-meshes, and a height-layer tweak regenerates. While dragging, regeneration happens at the preview resolution.

## Layout

```
terrain/
  index.html            shell: viewport canvas, floating inspector, HUD
  css/app.css           design tokens + components
  js/main.js            App: state, history, routing, camera, export, debug params
  js/ui.js              inspector + controls (layer stacks, sliders, segments, color)
  js/schema.js          declarative layer/parameter schema (drives both UI and GPU packing)
  js/presets.js         default state + presets
  js/engine.js          WebGPU device, resources, pipelines, stage scheduling, rendering
  js/camera.js, math.js, png.js, mctables.js
  js/wgsl/              common noise, height, derive, volume SDF, marching cubes, render
```

## Debug URL parameters

| Param | Effect |
|---|---|
| `?res=512` | Override heightfield resolution |
| `?rs=0.75` | Render scale |
| `?nomsaa` | Disable MSAA |
| `?still` | Render only on change (no continuous animation) |
| `?tab=world` | Initial inspector tab (`terrain`, `volume`, `surface`, `world`) |
| `#Sea%20Cliffs` | Load a preset by name |

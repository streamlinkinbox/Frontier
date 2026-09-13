# FRONTIER · SDF Terrain Studio

A realtime, **fully volumetric (SDF) open-world terrain generator** with
physics-based particle erosion, a node graph, paintable rain, and procedural
satellite-style surfacing.

**There is no heightmap in this engine.** The terrain is a true 3D signed
distance field with per-voxel material attributes. Overhangs, arches, caves,
sea stacks and vertical cliffs are representable — and every erosion operator
works directly on the SDF in 3D.

## Quick start

No build step, no dependencies. Just serve the folder:

```bash
cd sdf-terrain
python3 -m http.server 8080
# open http://localhost:8080/
```

Requires a browser with **WebGL2** (Chrome / Edge / Firefox / Safari 15+).

Headless bake (Node ≥ 18):

```bash
node bake.mjs my-graph.json --res 128 --seconds 20 --obj terrain.obj --raw terrain.raw
```

## How to use the studio

1. **Pick a preset** (Alpine / Canyon / Karst / Volcanic / Coastal) or build a
   graph from scratch (`New`, double-click the node canvas to add nodes).
2. **Tune field nodes** — sliders regenerate with an instant low-res preview,
   then a full-quality pass. 🎲 randomizes seeds.
3. **Press `B` (Paint)**, choose 🌧 Rain, and **paint a storm onto a
   mountainside**. Drops fall, excavate impact craters, pick up sediment, carve
   rills/gullies, and deposit fans — live, in the viewport.
4. **Tune erosion nodes** (`Erode: Hydraulic/Thermal/Aeolian/Chemical`) — they
   drive the realtime sim, no regen needed.
5. **Export**: `OBJ`/`PLY` mesh with vertex colors, `PNG` screenshot, or the
   graph JSON for the headless `bake.mjs` pipeline.

Camera: drag = orbit · right-drag = pan · wheel = zoom · `V` orbit · `B`
paint · `Space` play/pause.

## Architecture

```
js/noise.js     Seeded value/Perlin/simplex + fBm/ridged/billow/mountain-MF,
                Voronoi, domain warp, terrace, strata, rock hardness
js/sdf.js       Volume: SDF f32 + attrA (sediment/wetness/talus/emitter) +
                attrB (hardness/strata/cavity/precipitate); SDF sculpt stamps
                (smooth carve/deposit, oriented impact craters); sphere tracing
js/nodes.js     Node defs, graph → field compiler (closures of (x,y,z)),
                FX collection, 5 presets, import/export + validation
js/erosion.js   Hydraulic particles + thermal talus + wind + chemical,
                all SDF-native; mass-conserved sediment; adaptive budgets
js/fieldgen.js  Progressive (cancellable) volume generation + cavity bake
js/render.js    WebGL2 SDF raymarcher + procedural SAT shader + rain overlay
js/mesh.js      Surface-nets extraction + CPU SAT vertex colors + OBJ/PLY
js/ui.js        Node editor, inspector, paint/orbit viewport, bake, export
bake.mjs        Headless CLI: graph → eroded volume → mesh/raw
```

Volumes live on CPU (`Float32Array` SDF + `Uint8` attributes) and mirror to
`TEXTURE_3D` (`R32F` + `RGBA8`). Erosion marks 8³ dirty bricks; the renderer
re-uploads only those each frame (capped) — the sim can run at full rate.

## The erosion model (why it isn't "smoothing")

Heightfield eroders fail because they move *heights toward their neighbors*
(thermal blur) and can't represent 3D flow at all. Every operator here instead
**moves mass through 3D space**:

- **Hydraulic** — each drop is ballistic (`gravity + wind + drag`) until SDF
  contact. Impact excavates an **oriented crater stamp** with
  `radius ∝ ∛(KE / rockStrength)` and grazing-angle elongation, throws an
  ejecta rim, and converts removed SDF volume → carried sediment (minus a
  fines fraction). Flow follows **gravity projected onto the 3D tangent
  plane**, with Bagnold-style transport capacity: overloaded drops drape
  levees/fans/deltas (SDF *addition*), hungry fast drops plough grooves
  (SDF *subtraction* → rills, gullies). Drops that run off an edge detach and
  free-fall — waterfalls carve plunge pools. Evaporation/infiltration end the
  journey with a dry-out fan. Nothing is ever averaged.
- **Thermal** — over-steep surface voxels (vs. per-material angle of repose:
  loose talus rests shallower, hard bedrock steeper) shed **discrete mass**
  that travels downhill to the first stable air voxel. Builds talus cones,
  preserves crests.
- **Aeolian** — upwind SDF marching gives per-voxel **sheltering**; exposed
  rock abrades ∝ shear² (soft/loose first), lee zones accumulate drift sand.
- **Chemical** — dissolution ∝ `wetness^1.4 × softness × concavity` (karst
  pits deepen; convex brows gain evaporite precipitate; leached rinds soften).

Sediment/wetness/talus trails persist in `attrA` and feed the shader, so flow
paths visibly darken, fans lighten, and talus aprons render as scree.

## The SAT look (no downloaded textures)

`render.js` raymarches the SDF directly (sphere tracing, SDF-cone AO,
laplacian curvature/cavity, soft sun shadows) and shades each hit from volume
attributes: warped **strata-bedded bedrock**, slope-driven cliffs, moisture
vegetation, sediment/talus/beach, elevation snow, wet darkening + specular,
cavity dirt, plus a sea plane with depth tint, sun glitter and foam. The five
biome palettes live in the `SAT Shade` node; `mesh.js` mirrors the same logic
on CPU for export vertex colors.

## Scaling this to open world

This studio is the **tile authoring + physics core**. The production path:

- **Chunked sparse volumes** (per-tile `Volume` + shared node graph, seeded by
  tile id), streamed by distance; erosion sim runs on active tiles with
  cross-border particle migration.
- **Compute port**: `erosion.js` maps 1:1 to compute shaders (particles =
  structured buffers, stamps = volume atomics or gather splats); the field
  compiler already isolates pure `(x,y,z)` evaluation for WGSL/HLSL codegen.
- **Mesh path**: surface nets → chunk meshes with SAT-baked textures for
  runtime; the raymarched viewport remains the ground-truth preview.

## File map

| Path | Purpose |
|---|---|
| `index.html` / `css/studio.css` | Studio shell + theme |
| `js/*.js` | Engine (all `node --check` clean, core is DOM-free) |
| `bake.mjs` | Headless bake CLI |
| `package.json` | `{type: module}` + helper scripts |

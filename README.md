# Frontier

SDF terrain generator with a node-based editor: narrow-band signed distance
fields, physically-motivated erosion (hydraulic / thermal / alluvial / wind),
worley-tunnel caves, and Gaea-style splatmap texturing — viewport on the left,
node graph on the right.

```
npm install
npm run dev        # live app
npm run typecheck
```

Tests (headless, no GPU needed):

```
npx esbuild src/test/headless.ts --bundle --platform=node --format=cjs --outfile=.cache/h.cjs && node .cache/h.cjs   # full cook pipeline
npx esbuild src/test/visual.ts   --bundle --platform=node --format=cjs --outfile=.cache/v.cjs && node .cache/v.cjs   # ASCII river/cliff maps
npx esbuild src/test/ui-smoke.ts --bundle --platform=node --format=cjs --external:jsdom --outfile=.cache/u.cjs && node .cache/u.cjs  # editor DOM wiring
```

Optional end-to-end browser test (needs a real Chrome; not available in CI sandboxes):
`npm i -D puppeteer && node src/test/browser-smoke.mjs http://localhost:5173/ /tmp/shot.png`


## Architecture

```
src/core/        maths, runs in the cook worker
  volume.ts      narrow-band SDF volume (distances saturated outside ±6 voxels)
  reinit.ts      Fast Sweeping eikonal re-initialisation (keeps |∇d| = 1)
  heightfield.ts 2.5D bridge: extract top surface / write eroded surface back
  caves.ts       worley tunnel voids + analytic carve primitives
  erosion/       flow routing (pit-fill, D8, accumulation), hydraulic,
                 thermal (talus/strata), alluvial (fans/aprons), wind
  splat.ts       mask extractors + RGBA splatmap packing
  mesh.ts        table-free dual-contouring-lite isosurfacing (QEF vertex snap)
  resolution.ts  the feature guard (see below)
src/graph/       node registry, graph model, default graph
src/worker/      cook worker: topological evaluation of the graph
src/ui/          three.js viewport + node editor + inspector + chrome
```

### SDF discipline (why it doesn't blur)

* The master representation is a **narrow-band SDF**: `d < 0` inside rock,
  saturated at ±6 voxels outside the band. Memory and operator cost stay
  O(band), not O(volume).
* **Caves live in a separate void volume.** Erosion only ever touches the
  ground volume, so a river can never smear a cave wall. `Carve` combines them
  with a sharp boolean (`max(ground, −void)`) at mesh time.
* Erosion solvers are 2.5D: the top surface is extracted per column, eroded,
  then written back with `verticalRedistance` + **Fast Sweeping Method**
  re-initialisation. FSM is an exact distance transform — no smoothing kernel
  anywhere — so cliff faces and overhangs come back distance-exact.
* Smooth blends (`smoothK`) are opt-in per boolean and re-initialised after.

### The resolution guard

Every operator that can blur the field declares the metre-size of the smallest
feature it needs (`resRequirement` in `src/graph/registry.ts`). Before a cook,
the guard converts that to voxels at the current grid spacing and **blocks the
cook** when a feature would land under 3 voxels across — e.g. a 14 m river
channel at res 64 on a 1024 m domain is 0.9 voxels: refused, amber badge on the
node, reason in the inspector, one-click "Raise resolution to N".
The inspector's *Resolution Budget* panel shows voxel size, grid, memory and
the per-node feature verdicts live.

### Node categories

* **Generators** — Simplex / Perlin / Value / White / MultiFractal (ridged) /
  Badlands / Cellular (Voronoi) / Combine.
* **SDF** — To SDF, Cave System (worley tunnels), Carve Sphere/Box,
  Union Voids, Carve.
* **Erosion** — Hydraulic (stream-power rivers with sediment capacity and a
  metre-width channel splat so rivers *read*), Thermal (angle-of-repose
  collapse with strata hardness → cliffs + scree), Alluvial (floodplains,
  fans, cliff-base aprons), Wind (aeolian abrasion / lee deposition).
* **Texturing** — Mask (height/slope/curvature/flow/cavity/moisture/strata/
  exposure), Splat Layer, Splatmap (packs ≤8 layers into RGBA sets, blended in
  the viewport shader by world-XZ projection).
* **Output** — mesh + splatmaps to the viewport.

### Editor

Matches the reference screenshots: dotted-grid canvas, rounded node cards with
IN/OUT port columns, bezier wires coloured by port type, floating context
toolbar on selection (collapse / mute / frame / lock / rename / fit / delete),
searchable library panel bottom-right, canvas settings (dotted/blank) top-right,
"Built %" progress pill bottom-left, viewport tool rail + brush bar (sculpting
is intentionally inert this milestone), top toolbar with save/load/undo/redo,
wireframe, splat shading, PNG snapshot and auto-cook.

Shortcuts: `Ctrl+Enter` cook · `Ctrl+Z` / `Ctrl+Shift+Z` undo/redo ·
`Del` delete node · double-click canvas = library · wheel = zoom.

## Roadmap

* sculpt brushes writing into the SDF band (rail is already laid out)
* GPU compute backend for 512³+ interactive rates
* splatmap PNG export / albedo-roughness-normal bake
* adaptive chunked LOD volumes

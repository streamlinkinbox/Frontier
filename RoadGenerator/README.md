# frontier. roads — WebGPU road network generator

A standalone, dependency-free WebGPU web app that generates high-fidelity road networks:
spline-drawn roads, single-mesh quad-topology intersections, motorway interchanges, bridges,
underpasses, railways and subways, precise road markings, street furniture, damage and
construction zones, on-street parking, city block plots (no buildings) and a small AI traffic simulation.

```
npm install            # esbuild + naga-wasm (dev only)
npm run dev            # http://localhost:8080  (serves RoadGenerator/index.html)
npm test               # geometry / pipeline tests (node:test)
npm run check:wgsl     # validates every shader module with naga
npm run build          # RoadGenerator/dist/road-generator.html – single-file build
```

Requires a browser with WebGPU (Chrome/Edge 113+, Safari 18+, Firefox 141+).
`dist/road-generator.html` runs straight from disk (no server, no modules).

## Workflow

1. **Generate** a network from the left panel (presets: Downtown grid, Organic, Highway with
   diamond interchange, Full cloverleaf, Demo town) or draw roads/rails yourself.
2. **Edit** – click any intersection, road or rail; move a whole intersection with the gizmo.
   Everything rebuilds live (status hairline in the header).
3. **Texture** – the *Materials* tab of the layer stack holds a per-material procedural
   texture layer stack (base, noise, cracks, paving pattern, tactile paving, weathering…).
   Edit any layer in the inspector; the texture rebakes and streams to the GPU.
4. **Simulate** – *Sim* mode runs up to 25 AI vehicles plus trains; signals cycle.

### Controls

| Key / mouse | Action |
|---|---|
| `1` / `Q` | Select tool – click objects, drag nothing |
| `2` / `G` | Move tool – gizmo on the selected intersection (XZ plane, X, Z, Y handles) |
| `3` | Draw road (click points, `Enter` to finish, `Esc` to cancel; snaps to existing nodes) |
| `4` | Draw rail |
| RMB drag | Orbit · MMB / Shift+RMB drag: pan · wheel: dolly |
| `W A S D` + `E` / `C` | Fly |
| `F` | Frame selection / everything |
| `X` | X-ray (reveals subway tunnels) |
| `Space` | Toggle simulation |
| `Delete` | Delete selection (removes a node together with its roads) |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo · `Ctrl+S` save JSON |

## Architecture

```
RoadGenerator/
  index.html          app shell (header · tool rail · create panel · viewport · layers + inspector)
  src/core/           pure geometry, no DOM / GPU
    math.js           vectors, mat4, Hermite spline with chord-scaled tangents, closest-station queries
    mesh.js           MeshBuilder (quads first, tris only where needed), orientation helpers
    profile.js        road profiles (local, avenue, boulevard, carriageway, ramp, lane …) and kerb kits
    network.js        editable graph: nodes, roads (splines), rails; split/merge/JSON
    junction.js       per-node trim solve (fillet / convex / straight), cut balancing, Coons patches,
                      merged intersection meshes with quad centres (Y, +, T, 5-way, bends, noses)
    build.js          whole-network build → one road mesh + plan + audit (manifold / quad ratio)
    markings.js       lane lines, edge lines, arrows, zebra & stop lines, gores, parking bays, bump chevrons
    primitives.js     box, lathe, loft, sweep, bevel box
    props.js          streetlights, signals, bollards (5 styles), guardrails, barriers, billboards, vehicles, trains
    furnish.js        placement: signals, lights, bollards, barriers, parking, construction, damage decals
    blocks.js         city blocks: planar faces of the road graph → plots hugging the footway backs (no buildings)
    structures.js     bridges (box girder, hammerhead piers, parapets), viaduct decks under elevated
                      junctions, retaining walls, underpass trenches, ground with cut-outs
    rail.js           ballast, sleepers, rails, level crossings (booms, crossbucks), subway tunnels
    traffic.js        lane graph, car-following, signal phases, trains (≤ 25 vehicles)
    city.js           presets: grid, organic, diamond, cloverleaf (rotated quadrant template), demo
    textures.js       procedural texture layer stacks → albedo / normal / ORM
  src/gpu/
    shaders.js        WGSL: shadow, gbuffer, decals, SSAO + blur, lighting, post (ACES + FXAA), lines, mip
    renderer.js       deferred WebGPU renderer, 2-cascade shadows, instancing, picking, texture arrays
    camera.js         orbit / fly camera
  src/ui/             widgets, scene (layer visibility + instancing), panels (layer stack, inspector), app
  tests/              node:test suites: junctions, full pipeline, and a renderer boot test that runs the
                      real app against a validating WebGPU mock (tools/gpu-mock.mjs)
tools/
  serve.mjs           static dev server · build.mjs single-file bundle · check-wgsl.mjs (naga) · wire-png.mjs
  gpu-mock.mjs        headless WebGPU API mock enforcing bind-group/layout compatibility, attachment vs
                      binding conflicts, texture–sampler pair rules, usage flags and buffer bounds
```

### Intersection topology

Roads are Hermite splines; each node solves the trim distance of every arm from the kerb-line
geometry of its neighbours (`T = R / tan(θ/2)`), straightens the approach over a blend, and
builds **one** merged mesh: corner kits swept along fillets or convex Bezier sweeps, Coons patches
per arm, and a quad centre (single Coons patch for matched 4-way nodes, midpoint spokes otherwise).
Acute approaches (ramps) produce long merge noses with painted gores. Every preset is asserted to
be manifold with a quad ratio above 0.97 and zero degenerate faces.

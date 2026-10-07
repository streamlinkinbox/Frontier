# Frontier · Cliff Forge

A procedural **quarry / terracing cliff** generator that runs in the browser.

Everything is **geometry only** — there are no image textures, no normal maps and
no noise textures anywhere in this project. All of the surface detail is real
triangles: a displaced heightfield for the ground, a ruled-surface highwall per
bench, and low-poly rock chunks generated from displaced icosahedra and glued
onto the faces. Colours live in vertex attributes, so the whole cliff exports
cleanly to any engine.

```
cliff/
├── index.html            UI + canvas
├── src/
│   ├── main.js           three.js scene, sun/sky/env, camera, UI, export
│   ├── build.js          generateCliff(params) -> meshes + stats
│   ├── quarry.js         the terracing builder (walls, benches, road, rocks)
│   ├── terrain.js        base heightfield + ground colour
│   ├── rocks.js          procedural rock-chunk library
│   ├── meshbuilder.js    world-space triangle accumulator
│   ├── noise.js          seeded simplex noise + fBm
│   ├── palette.js        rock-type presets (strata, sun, sky, fog)
│   └── export.js         OBJ / glTF / PNG export
├── test/
│   ├── smoke.mjs         headless geometry validation (node)
│   ├── export.mjs        OBJ export validation (node)
│   ├── preview.mjs       ASCII preview (node)
│   ├── render.mjs        software PNG preview (node)
│   └── png.js            tiny PNG encoder
└── vendor/               three.js r186 (module + OrbitControls + GLTFExporter)
```

## Run it

```bash
cd cliff
python3 -m http.server 8080     # or any static file server
# open http://localhost:8080
```

No build step, no bundler, no network access at runtime — three.js is vendored.

## How a cliff is made

1. **Base mesh** — `terrain.js` builds a heightfield plateau: a shallow bowl
   under the pit (so benches read as level cuts), rolling hills, and a rim of
   higher ground that hides the edge of the world. Triangles whose centre falls
   inside the pit outline are dropped, leaving a hole for the quarry.

2. **Pit plan** — `planPit()` makes an irregular, star-shaped outline from
   angular noise. Every terrace is that outline inset by `benchWidth`, so the
   benches stay parallel no matter how frayed the rim is. Terrace heights are
   level cuts of `pitDepth / benchHeight`.

3. **Faces** — each bench face is a finely tessellated ruled surface that drops
   only `batter` (~1 unit) horizontally, i.e. a near-vertical highwall. It is
   displaced by
   * per-stratum **hardness** — differential erosion, so hard beds bulge and
     soft beds recess, which also produces undercuts,
   * low-frequency **lumps** and medium lumps,
   * **block cells** for a chunky fracture look,
   * **vertical joint planes** (noise stretched along Y).

   Displacement is clamped so faces can never punch through the bench above.

4. **Benches & floor** — annular floors between the toe of one face and the
   crest of the next, with rubble displacement; the pit floor is a full disc
   with a sump dish.

5. **Haul road** — a spiral trench cut through the faces. It is baked into the
   wall grid itself (same topology, vertices simply moved onto the graded
   surface), so the seam is watertight by construction. Wheel tracks are drawn
   into the vertex colours. An approach road runs out across the plateau and
   fades into the ground colour.

6. **Rock chunks** — `rocks.js` builds a library of 14 rocks: an icosahedron
   lumped with fBm, then *fractured* by collapsing clusters of vertices onto
   their best-fit plane (flat fracture planes, angular silhouette). Chunks are
   scattered on the faces, at the bench toes, on the pit floor and out on the
   plateau, each tinted with the strata it sits in.

7. **Strata colour** — vertex colours keyed to a wavy "layer coordinate"
   (`(y - base) / bandThickness + warp(x,z)`), so sedimentary bands follow the
   rock around corners instead of being straight world-space lines. Recesses
   are darkened and deep benches fall into shadow to fake AO.

8. **Render** — three.js: ACES tone mapping, a gradient sky dome that is also
   pre-filtered into an environment map (`PMREMGenerator`), a shadow-casting
   sun with a tight ortho frustum around the pit, hemisphere fill and linear
   fog matching the horizon.

## Controls

| | |
|---|---|
| **Rock type** | Limestone / Sandstone / Granite / Slate — changes strata palette, sun, sky, fog and sensible defaults |
| **Seed** | any text; the same seed always rebuilds the same cliff |
| **Pit radius / depth** | overall size |
| **Bench height / width** | terracing — the number of benches is `round(depth / height)` |
| **Outline fray** | how irregular the pit rim is |
| **Rock roughness** | face displacement amplitude |
| **Hard-band bulge** | strength of differential erosion (bulges / undercuts) |
| **Strata thickness / contrast** | sedimentary banding |
| **Chunk density / size** | how much rubble is glued on |
| **Views** | Overview · Rim · Face · Top |
| **Export** | GLB (binary glTF, with vertex colours), OBJ (`v x y z r g b`), PNG |

Keys: `G` generate · `R` randomise · `space` auto-orbit · `H` hide panel.

## Headless checks

```bash
node test/smoke.mjs      # every preset + edge cases: finite geometry, unit
                         # normals, pit depth, determinism, no bench collapse
node test/export.mjs     # OBJ structure + vertex-colour range
node test/preview.mjs quarry-01 limestone iso   # ASCII preview
node test/render.mjs quarry-01 limestone /tmp/cliff.png   # PNG preview
```

`test/render.mjs` software-rasterises the generated meshes with an approximate
version of the in-app lighting, which is how the geometry was iterated on
without a GPU.

## Extending

* **New rock type** — add a `PRESETS` entry in `src/palette.js` (strata colours,
  `shaleEvery`, tints, ground colours, sun/sky/fog) and an `<option>` in
  `index.html`.
* **Other cliff styles** — `buildQuarry()` is self-contained; a canyon or
  coastal style would be a second builder producing the same
  `{ terrainGeo, cliffGeo, stats }` shape that `main.js` consumes.
* **Higher/lower quality** — `params.terrainRes` (ground cell size) and
  `rockDensity` are the two big levers; wall tessellation is derived from the
  pit radius and bench height.

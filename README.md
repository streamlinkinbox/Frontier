# Frontier · Terrain Lab

Interactive browser-based cliff mountain and terraced quarry generator, built with Three.js and Vite.

## Run

```sh
npm install
npm run dev
```

`npm run build` creates a production build; `npm test` runs geometry regression tests.

## Mountain and quarry modes

The default **Cliff mountain** mode constructs a positive rocky massif: an offset summit, unequal ridges and shoulders, near-vertical faces, large scar wedges, and a broad scree apron. Its base uses explicit piecewise ridge planes, not quarry terraces or a noise-displaced cone. Family-specific rock instances fit the base surface; coherent bedding and independently seeded scree complete the formation.

Mountain controls: summit height (45–180 m), footprint radius (40–110 m), major ridges (3–9), and cliff steepness. Footprint is the nominal radius before directional stretching. Presets: **Cliff massif**, **Slate ridge**, and **Sandstone escarpment**. The slope control is a shaping percentage, not an engineering slope angle.

Switch to **Quarry** to use the existing terraced generator. Switching generates the selected mode immediately; sliders and presets are applied with **Generate**. Each mode retains its structural slider values for the current session; geology, fracture, debris, and seed are shared. No settings persist after a page reload.

Both modes support base-only inspection, wireframe, camera controls, PNG screenshots, and full OBJ exports. Export names and metadata identify the generated landform, even when controls have unapplied changes. Hidden rock dressing is still included in exports.

## Quarry: base cliff → rock formation → talus

1. **Shape the cliff first.** A continuous terraced base has large polygonal buttresses, setbacks, collapse scars and shared inclined bed seams. Benches connect to the actual irregular cliff boundaries, rather than hiding a straight extrusion under scattered blocks.
2. **Fit matching rocks.** Twelve reusable, multi-ring rock meshes per family have modeled shoulders, ledges, recessed seams or cleavage edges. Rocks are embedded in the shaped surface and aligned to its tangent and slope. Geological beds guide limestone/sandstone placement; inclined plates define slate.
3. **Add loose debris.** Gravity-style talus placement at cliff feet uses family-specific fragment proportions and an independent seed stream. Disabling debris leaves the base and attached rocks unchanged.

Rock families change **base profile, bedding scale/dip, mesh topology, proportions, placement, and debris**, not only color:

- **Bedded limestone:** broad slabs, connected beds, projecting ledges and recessed seams.
- **Massive sandstone:** thicker beds, broader buttresses, alcoves and blocky weathered shoulders.
- **Cleaved slate:** inclined narrow plates and thin, blade-like scree.

No image/noise textures or texture maps are used. Macro relief is geometry; the optional Rock Surface shader uses analytic procedural fields for colour, roughness and fine normal perturbation. These are procedural geological approximations, not scanned rocks or a geological/erosion simulation.

## Controls

Terrace count, bench height/width, pit radius, fracture intensity, rock family, debris and repeatable seeds are adjustable. Click **Generate quarry** to apply changes.

Use the **mountain button** in the viewport toolbar to inspect the base cliff without attached rocks or debris. Orbit, pan, top view, wireframe, auto-rotation and PNG screenshots are also available. Rendering pauses when the camera is stationary.

**Export mesh** downloads a Y-up, meter-scale OBJ with every rock instance expanded and generation settings in its comments. It always includes the complete base and dressing, even while inspecting the base-only view. OBJ contains geometry, not a material/texture bundle; the intersecting rock pieces are not boolean-unioned into a single watertight solid.

This is a visualization tool, not an engineering or slope-stability model. The quarry has closed benches; haul-road routing is not modeled. Mountains are single-massif procedural approximations, not multi-peak terrain or erosion simulations.

## Low-poly cliff workflow

**Low-poly · sculpted cliffs** is now the default. The mountain base uses an irregular coarse triangle grid with large exposed facets, no small bedding ridges, and sparse embedded rock masses rather than rows of blocks. Rocks are ray-fitted to the actual coarse base. **Layered · detailed formation** retains the previous mountain workflow. Quarry mode also supports coarser faces and fewer rocks.

- **Facet detail:** mountain base resolution (about 250–2,100 triangles including the skirt), not a decimator. Full exported triangle counts include every rock instance and are displayed in the panel.
- **Rock distortion:** seeded vertex perturbation, shear, taper and bend before constructing low-poly rock hulls. Applies to low-poly rocks and sandstone in either style.
- **Sandstone elongation:** mixes ordinary boulders with much longer, distorted forms. This changes real geometry in both mountain and quarry modes.

The newer vertex distortion supersedes the earlier no-noise-fields description for individual rocks: seeded vertex noise is now intentional. No **noise textures**, normal maps or displacement textures are used. This is a custom Three.js generator inspired by a rock-generator workflow, not Blender's add-on or an exact reproduction.


## Escarpment silhouette and sandstone correction

The default mountain silhouette is now **Long escarpment · uneven crest**: an elongated, broken crest, broad steep front, receding back slope, and sloped ends. **Cliff silhouette** also offers **Pointed massif · previous shape**. Both work with Low-poly and Layered mesh styles; the Slate ridge preset selects the pointed shape, while Long escarpment and Sandstone wall select the new silhouette.

Sandstone templates now have restrained elongation with added volume rather than needle-like stretching. World-space dimensions are checked **after placement scaling**, limiting the longest/shortest bounding dimension ratio to 2.6 in both mountain and quarry modes. This prevents thin placement cells from turning boulders into shelves. Mountain sandstone is embedded deeper in the base. Distortion and elongation remain adjustable; no noise textures are used.


## Mapless rock surfaces

**Rock Surface** controls update live, independently of unapplied geometry settings. Toggle **Procedural weathering** to compare against the original flat-colour material.

- **Iron oxidation:** localized ochre/brown mineral pockets and discontinuous fracture accents. No metallic surface response: rust/minerals remain dielectric.
- **Weathering & runoff:** sparse, narrow gravity-aligned trickles with nonperiodic starts, finite lengths, taper and breaks; pale powder on upward ledges and irregular surface pits.
- **Grain strength / Grain size:** independent relief and size controls (2–2000 mm; default 240 mm / 24 cm). Jittered, domain-warped angular aggregate cells, finer grain and coarser pits contribute separate scales. Coverage varies by patch rather than applying one uniform bump everywhere. The slider controls the larger mineral-chip scale, with the rough aggregate substrate at roughly one-third that size and a fine grain layer retained underneath. These deliberately enlarged aggregate chips are an artistic readability control, not literal sand particle sizes. Derivative-based filtering fades unresolved detail to prevent distant glitter.
- **Layered crystal flakes:** a separate live strength control (default 60%) for three overlapping, independently seeded cellular layers: large basal chips, tilted mineral plates and small sand-like flecks. Each cell has a stable polygonal outline, mineral tint, bevel edge and tilted normal; foreground flakes cover older ones instead of adding another bumpy noise field. Inspired by the layered structure of automotive paint flakes. The base rock remains matte/dielectric; flake finish can now be art-directed independently using physical material controls. World-space triplanar projection avoids UV seams; each unresolved layer fades independently. This is shader relief, not additional geometry or a portable OBJ material.
- **Flake physical finish:** live metallic, roughness, specular intensity, clearcoat, coat roughness and IOR (1–2.5). A `MeshPhysicalMaterial` uses Three.js's physical lighting model with cell-coverage-masked parameters: the rock outside the flakes stays dielectric and uncoated. Independent seeded finish values vary roughness per cell. Metallic highlights use the flake colour; dielectric specular uses IOR/specular intensity. Clearcoat uses Three's standard fixed coat IOR (~1.5), while the IOR slider controls the underlying flake. Specular intensity/IOR chiefly affect nonmetallic flakes, as expected for the physical BRDF.
- **Flake colour variation:** each cell has independent stable RGB hashes, not just different brightness. The slider blends from geology-tinted colours to stronger individual hues; the colour stays attached to its flake when the camera moves.
- **Raised grains / flakes:** controls the seeded percentage of flakes with tilted/bevel bump normals. Other flakes stay flush and suppress the substrate bump beneath their coverage. The default mixes both populations; 0% makes flakes flat, 100% gives all flakes relief. Colours and finish use independent seeds so flat flakes can still be colourful/shiny. This changes shading normals only, not actual vertex height, silhouette or OBJ geometry. Layer overlap and antialiasing blend normals at edges.
- **Peeling & fresh breaks:** paler fresh substrate patches plus up to 240 exposed lifted wedge flakes (12–44 cm).
- **Mineral crystals:** up to 120 exposed pockets, each with 12–20 opaque faceted prisms, a larger central crystal and smaller tilted companions (6.5–30 cm tall). Actual closed geometry and a dielectric, lower-roughness material catch light; no noise-based sparkle layer. These are generic mineral clusters, not mineralogical identification.

The shader uses deterministic, rotated world-space analytic value and jittered-cell fields calculated directly in GLSL with no sampled texture, no UV requirement and no baked maps. Instance transforms are included, so the material works on both the continuous base and instanced rocks. World coordinates use metres. Microgeometry is sampled from both the continuous base and attached rock faces, stays independently seeded, and is hidden in base-only view. A cached CPU triangle BVH rejects buried pocket/flake origins and projects crystal companions onto the nearby exposed surface. This is placement ray testing, not a boolean surface-union algorithm. Tiny detail does not receive the cliff-wide shadow map, avoiding pixelated self-shadow artifacts at close range.

**Inspect surface up close** (magnifier) moves to a surface under the current view, preferring a visible crystal pocket when available. Reset camera returns to the full formation. The scale ruler changes to centimetres at close range. Centimetre-scale grains and flakes are not visible from the whole-cliff view.

These are artistic, geology-informed approximations, **not** oxidation kinetics, water-flow simulation, measured cavity/AO masks or real peeling erosion. Iron pockets and fracture masks are analytic approximations; runoff follows gravity direction but is not traced from actual crack openings. The coarse low-poly silhouette remains unchanged.

### Export limitations

OBJ includes the base, all rocks and any generated flakes/crystals, even in base-only view. It does **not** contain the shader or its material appearance. PNG screenshots preserve the rendered look. **Save surface recipe (.json)** exports seed, geometry settings and current live surface controls; it is a settings record, not an interoperable Blender/engine material and there is currently no recipe-import UI. Reimplement the shader in the destination renderer or bake maps externally if that is later permitted.

### Layered stone height relief

**Layered stone depth** adds a mapless world-space scalar height field: four scales of shallow continuous stone relief, sparse open angular fractures and a fine erosion rind. **Grain height / bump** independently adds stronger aggregate and fine-grain height variation. Normals are derived from surface derivatives of that metre-valued height, rather than just arbitrary normal offsets. Flat mineral flakes mask grain height beneath them; stone relief remains underneath the full finish. Both controls update live and are recorded in surface recipe v7. The reference photos guide the character only; they are not loaded as textures. This is height-derived bump shading, **not tessellation, vertex displacement, parallax or silhouette relief**; OBJ still contains only the existing geometry. Layer depth zero removes this added stone height field, and grain height zero removes the added grain height (existing grain-strength normal detail remains separately controlled).

The v6 stone finish replaces looping isocontour cracks with independently seeded open fracture segments, local chips and occasional short branches. Groove depths are shallow and the height-normal slope is bounded at extreme settings. Height normals use the geometric face normal before combination with grain/flake detail, avoiding noisy derivative frames. **Matte reference stone finish** sets readable grains, multi-scale relief, subdued mineral colours and nonmetallic/uncoated flakes without rebuilding the formation or changing oxidation, weathering, crystal or peeling abundance. Existing finish sliders remain available for the stylized layered-flake look.


The v7 relief correction restores explicit height amplitude at four independent stone scales (broad stone, smaller shoulders, sandy rind and fine grit). Grain height is now independent of Grain strength instead of being multiplied by it. The matte reference finish selects Grain strength 65%, Grain size 14 cm, Layered stone depth 85%, Grain height 85% and 25% mineral flakes; it no longer removes most of the detail. No looping crack contours were restored. These are deliberately readable, art-directed bump heights, not geometry displacement or measured rock scans. Added regressions retain the relief-band slope budget and verify that zero grain strength does not disable grain height.

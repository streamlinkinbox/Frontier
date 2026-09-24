# Native Moon inspector

## Scope and source

Moon is the next native stage after Atmosphere / Sky. **Stars → Clouds → Fog (including local and volumetric)** remain next.

The original C++ roster has four independent slots and six atlas presets: Luna, Ember, Glacier, Sulfur, Shroud and Shard. Its 39-property interface is retained, with eight new roll/pitch properties, totaling **47 fields in the existing six-group sheet**. A compact named dropdown inside the Moon settings header selects the active instance without repeating four complete inspectors. The rejected selector-card row above settings is removed. A separate horizontal catalogue displays the six textured bodies, with their names beneath each sphere. Visible and Follow sky occupy their own settings card above it. The engine panel consumes generic editor properties and solved read-only values; it does not include Project-Zero headers.

Durable integration: `Tools/Build/Patches/MoonInspector.patch`, applied after the Sun, Lens and Atmosphere patches to target `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`. It adds routing/CMake registration, splits Moon sheet construction out of the large generic builder, and updates the project, CPU evaluator and shader together.

## Real moon catalogue and previews

The user rejected the illustrative crater moons. They have been removed. `MoonAtlasPreview.h` now samples **the actual registered atlas bytes** through the engine's `EvaluateMoons` CPU path. The selected body's tint, gamma, base tilt, authored roll/pitch, phase and brightness are honored. Phase, light, size and orientation cards, the position marker, and header thumbnails all show textured bodies.

The six-body catalogue uses full-phase textured spheres in one horizontal row, never a popup or grid. Clicking a sphere or its caption changes the selected instance's body. At narrow widths, the arrows scroll the strip without changing selection; at wide widths all six bodies fit and browsing arrows disable at the boundaries. The four independently configurable renderer slots are preserved. Internal `M1…M4` property keys remain compatible but are not public selector labels. Current instance names can repeat if multiple instances use the same body.

`Tools/Build/Patches/MoonCatalogue.patch` adds a generic borrowed atlas view to the editor sheet. Project-Zero lends its already-registered level-zero RGBA8 data; the editor does not load project files or invent an alternative moon. A process-unique revision invalidates previews on atlas reassignment. Missing/unassigned/1×1 placeholder views are unavailable, not white substitute spheres. Pixel ownership remains with `TextureIndex` and must stay valid through the editor frame; no asynchronous read/decode safety is claimed.

The editor owns one 256² RGBA pose image and six 96² RGBA catalogue images (about 472 KiB), managed per ImGui context and released at shutdown. Previews use a fixed display radius and Reinhard + sRGB tone mapping to stay readable. They are **real textured CPU-rendered body previews, not full scene-camera captures**; scene atmosphere, occlusion and halo composition are not reproduced. The size comparison remains deliberately compressed.

The position globe/coordinate lines and rotation guides still follow `moon-controls.jsx`. They are control diagrams, not replacement moon surfaces. Charcoal rounded cards, DM Sans and native SliderPills remain. Browser pixel parity is not certified.

## Bindings and conventions

| Control | Actual behavior |
| --- | --- |
| Visible | Per-slot visibility; hidden entries are omitted from the moon draw list. |
| Follow sky | Direction and phase come from `Solved.Moon` / `Solved.MoonPhase`. Native phase/position controls display solved values and disable editing without erasing stored manual values. |
| Phase | UI days in a 29.53-day cycle map to original normalized engine phase. Engine zero is new; existing renderer conversion shifts to reference zero-full convention. |
| Bright / Glow | Original dimensionless multipliers, not calibrated lux. Phase-weighted brightness is an explanatory metric, not a new light solver. |
| Size | Positive finite angular diameter, minimum 0.1°. Native numeric entry has no artificial upper cap; the slider grows to accommodate the authored value. Natural-size reset is 0.52°. Preset changes still reset size to that body's existing preset. |
| Azimuth / elevation | Original direction inputs, independent of surface orientation. Mouse drag and focused arrow-key input operate the spherical dial. |
| Roll | Screen-clockwise roll of surface and phase terminator. Uses the previously unused `MoonDirection.w` lane, in radians. |
| Pitch | Surface rotation without flattening the disc. The reference's forward pitch maps to inverse texture lookup: `preset tilt − authored pitch`, in radians. |

Position and orientation support two-axis pointer drag. Arrow keys adjust either axis; Shift increases the step to 10°. Orientation has a native reset button. Controls are scoped by selected slot.

CPU and shader both apply the same inverse screen-clockwise rotation before surface lookup/phase shading. The Moon uniform block remains **288 bytes, binding 22**, with offsets **0 / 16 / 32 / 96 / 160 / 224**. No descriptor layout or record size expansion is needed.

### Oversized moons

The old UI cap was 40°. Merely removing it would encounter the original `sin(radius)` projection singularities. For diameters above 180°, CPU and shader now use a bounded angular-distance radial map, continuous at that boundary. Values above 360° intentionally cover the sphere with an enlarged central surface region. This is an **art-direction extension**, not physically meaningful lunar astronomy. Ground/horizon visibility remains governed by the existing scene sky path. NaN/Inf edits are rejected; the representable finite-float range is the practical limit.

## Verification

```sh
python3 Exhibits/Workbench/Moon/RunNativePanel.py
python3 Exhibits/Workbench/Moon/RunNativePanel.py --debug
python3 Exhibits/Workbench/Moon/RunNativePanel.py --sanitize
python3 Exhibits/Workbench/Moon/CheckShaders.py --compiler /path/to/glslang
python3 Exhibits/Workbench/Moon/ServeNativePanel.py --port 5181
```

The native runner reconstructs the existing pinned integration, runs Sun/Lens/Atmosphere regressions and then the Moon proof. `--reuse-build` is only for a matching existing build of that optimization mode. Commands, source/image hashes, proof logs and selected-function stack measurements are in `Exhibits/Gallery/MoonNative`.

Checks cover all four slots, the 47-field inventory, scalar round trips, presets, missing-atlas/visibility gates, solver-follow behavior, native slider/drag/keyboard/numeric-input/reset interactions, renderer roll/pitch lanes, real CPU shading changes with asymmetric synthetic albedo, clockwise roll direction, large-size finite behavior through the maximum finite float, projection continuity at 180°, responsive scroll/2× captures and teardown.

The shader gate compiles the actual 18-entry shader table and reflects the actual viewport SPIR-V block. Here glslang 16.6.0 was built from source commit `e1b562a8bed273a02f30b59b66a5d499793cede5`, with optimizer/HLSL disabled. The compiler, CMake wheel, shader binaries and temporary source trees stay in ignored cache. Compilation/reflection are **not GPU execution or numerical GPU parity**.

Release/debug use a 256 KiB Linux process-stack limit and an 8 KiB selected-frame gate. Sanitizers use the default stack and leak checking. Whole-application execution, full atlas rendering, Windows/MSVC stack certification and browser pixel matching remain unverified. Early renderer-record tests deliberately use an empty atlas fixture; the UI proof then decodes all six pinned atlas files with the real TextureIndex, checks borrowed-byte identity and distinct body previews, and captures the real textured inspector. No GPU upload is represented as proven.

Unrelated inherited workspace files are not swept into this stage's commit.

### Recorded results — 2026-09-23

| Mode | Moon | Sun | Lens | Atmosphere / Sky |
| --- | ---: | ---: | ---: | ---: |
| Release | 146 PASS | 326 PASS | 74 PASS | 62 PASS |
| Debug / O0 | 146 PASS | 326 PASS | 74 PASS | 62 PASS |
| ASan + UBSan | 146 PASS | 326 PASS | 74 PASS | 62 PASS |

Shader gate: **18/18 compiled**, actual viewport reflection confirms the unchanged **288-byte Moon block at binding 22**. All three Moon source/image hash manifests match the final files.

Selected release / debug frames: slot builder **416 / 2,592 bytes**, Moon sheet builder **480 / 1,568 bytes**, native panel **848 / 1,120 bytes**. Release/debug pass the 256 KiB process-stack test. These are measured Linux/GCC results, not Windows estimates.

### Catalogue correction

The latest proof also exercises direct horizontal catalogue selection, single-row layout at wide and narrow sizes, and previous/next scrolling without changing the selected body, checks all six real texture decodes and byte identities, checks distinct rendered images, and verifies missing data produces transparent pixels rather than fabricated moons. Texture file hashes from the pinned target are included in each Moon manifest. The table above and current logs include these additional checks. The atlas-preview function uses 240 / 288 bytes in release / debug.

### Horizontal catalogue layout correction

The catalogue is now a dedicated rounded card below **Moon settings**. The settings card contains the Visible / Follow sky quick tiles and a compact active-instance dropdown; the catalogue contains the textured-body strip and browse arrows. Names remain below the images. There is no instance-card row above settings; settings and all following cards have moved up 48 pixels. The old popup grid and centered single-body selector are removed entirely.

Native proof coordinates/captures were updated for the additional card. Proofs explicitly check there is no catalogue popup, all six bodies fit in one row at wide size, and narrow-layout arrow browsing changes scroll position without replacing the selected body's preset. The row removal does not change Moon renderer bindings. The shader gate has now been rerun after the Stars integration: 18/18 compiled, with Moon still 288 bytes at binding 22.

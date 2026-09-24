# Native environment billboards and scene synchronization

Open `Exhibits/Gallery/NativeBillboards/index.html` for the actual native captures and draggable render comparisons. The report is an evidence viewer, not a browser implementation of the editor.

## Implementation

- `Engine/Editor/ViewportBillboards.h`: constant-pixel screen-facing icons, projection through the rendered camera basis/aspect, stable-key hit testing, nearest-overlap resolution, selected label, input ownership and clipping.
- `Projects/Project-Zero/Source/EditorInspectorSequence.h`: supplies the current camera and actual environment roster. Identity is `(2ull << 32) | (CelestialEntity + 1)`, never a cached row ordinal.
- `Tools/Build/Patches/NativeBillboards.patch`: integrates the overlay into the reconstructed native ViewportPanel, EditorHost, RenderScheduler and GameExecution. The viewport is enabled for this task. A billboard click selects the row before fetching its inspector, consumes the scene tap and cancels an older pending mesh-pick answer.
- The existing **Markers** toolbar button controls drawing and hit testing. Hidden rows/ancestors suppress their markers, without destroying authored visibility state.
- Local cloud/fog markers use the actual `Centre` properties. Global systems do not have a physical centre; their clearly labelled editor proxies sit on a camera-facing shelf. They are not movable physical objects. Local markers are intentionally editor overlays, not mesh-depth-occluded surfaces.
- Fog rows now explicitly resolve the same native Fog artwork used by the outliner/catalogue.

## Renderer fix found during verification

The CPU volume march previously received `Observation.LocalHours * 3600`. A Static Sun therefore froze cloud advection despite a nonzero Wind speed. `WeatherSeconds` now advances with simulation ticks independently of the solar clock, and supplies both CPU cloud/local-volume marching and the precipitation emitter's time argument. Hidden Wind supplies zero speed to the CPU media renderer. This does **not** claim that every precipitation visibility/wind binding has been audited.

## Actual rendered test

`NativeSceneProof.cpp` constructs real mesh columns at multiple distances, a plinth, ground and an off-camera area fill in `SceneStructure`. It renders the actual `VisibilityRaster`, configured through `CelestialSequence::ApplyTo`. No alternate sky gradient, generated scenery, painted cloud layer or browser inspector is substituted.

The fixture includes atmosphere, global/local clouds, height/aerial fog and local volumetric fog. Assertions compare inspector commit/fetch, renderer settings and RGB differences. Wind is verified by cloud motion while the Sun remains Static; zero Wind produces identical pixels at both times. Native ImGui mouse events select markers and edit Wind/Fog sliders. The Fog mouse test verifies the entire input → inspector → model → pixels path.

### Results

- **154 release checks passed.**
- **154 AddressSanitizer / UndefinedBehaviorSanitizer checks passed**, including leak detection.
- Fresh reconstruction from the pinned source and existing native overlay chain passed; the previous Construct regression also passed its 111 checks.
- `GameExecution.cpp` syntax check passed after integration.
- Evidence viewer: six capture tabs, seven A/B comparisons, slider, image loading, mobile width and browser exception checks passed.

Mean absolute RGB difference, on the 0–255 scale:

| Edit | Mean difference |
|---|---:|
| Atmosphere Mie | 2.813072 |
| Cloud coverage | 13.849276 |
| Height fog | 13.070196 |
| Local volumetric fog | 0.052258 |
| Local cloud density | 0.863317 |
| Wind advection with Static Sun | 8.917162 |
| Zero Wind, different times | **0.000000** |
| Native mouse Fog edit | 33.699998 |

The local effects occupy a small region, so their whole-frame mean is correspondingly small. The report retains the native CPU renderer's sampling/shadow artifacts rather than enhancing the images.

## Reproduce from the consolidated checkout

The native source now lives directly in this repository. No original Frontier repository or engine overlay assembly is required:

```sh
python3 Tools/Setup.py --verify
```

Captures and output are written to `build/native-proof/evidence/`. The CMake target reads the checked-in source, not cached compile-command JSON. The original `RunProof.py` and recorded `Proof.json`/`Sanitized.json` describe the earlier historical reconstruction; they are retained for provenance rather than used by the current build. See the root README and `Docs/Consolidation.md` for current setup and platform limits.

## Important remaining boundary

**Windows/Vulkan execution is not verified.** The production GPU application currently has a separate cloud-shadow staging/upload route. The CPU A/B scene does not establish that all volumetric cloud/fog/wind inspector fields reach that GPU route. Do not interpret the selection integration or a passing C++ syntax check as proof of GPU weather synchronization.

This fixture also does not establish precipitation-particle rendering, rainbow rendering, lens compositing, moon textures or star-catalogue rendering. Their editor icons are not evidence of those effects appearing in the scene. Those paths need their own upload/render integration and execution checks before claiming full environment parity.

# Native Wind, Precipitation and Rainbow

## Scope

Adds three C++/ImGui panels after the Fog integration on immutable target `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`. Water Bodies and Fluids are deliberately untouched. No Surface Wetness or Frost/Ice panel is introduced.

The read-only gallery at `Exhibits/Gallery/WeatherNative/index.html` contains actual native draw captures. It is not a browser simulation of the inspector. Serve it with `python3 Exhibits/Workbench/Weather/ServeNativePanel.py` (0.0.0.0:5185).

## Backend audit and controls

### Wind / Air

The existing `WindField` is already consumed by volumetric advection and precipitation. This panel edits that shared model rather than creating a separate preview-only wind.

- Compass drag edits **bearing and magnitude together**. Outer radius is 40 m/s. Bearing is where the wind blows **toward**: north = +Y, east = +X.
- Left/right arrows change bearing; up/down change speed; Home sets calm without discarding bearing.
- Native sliders retain Speed, Bearing, Shear, Veer, Gust, Turbulence and Steadiness. Beaufort and 1 km flow readouts derive from the real model.
- The gust envelope samples `WindField::SampleGust` over phase 0–2π, vertically 0–2×. This is a fixed-phase model diagnostic, not scene imagery.
- Existing backend semantics are preserved: Steadiness **scales variation**; zero suppresses gust and curl swirl. The panel explicitly explains this counterintuitive field name instead of silently inverting it.
- No invented Enabled switch: the underlying wind settings have none.

### Precipitation under Clouds

The backend has five actual types: Rain, Drizzle, Hail, Snow and Sleet. Rain, Snow and Hail lead the native type tiles; Drizzle and Sleet remain accessible rather than being dropped from authored data.

- Enabled, Follow Wind, Spawn from Clouds and Ground Collision use independent native tiles.
- Rate (mm/h), density, particle size, wind drift and accumulation bind to the existing simulation.
- Particle-size reference uses `PhysicsFor(type).RadiusMetres × sizeScale × 2`, reported in mm. The magnified 0–30 mm ruler uses a fixed 120-pixel span. Rain uses an oblate drop, snow a branching flake, hail/sleet irregular milky ice shapes. These are size/type illustrations, not a fake particle-density camera preview.
- Type-specific terminal speed and drag come from the actual physics table. Size scale changes particle size; it does not invent a new size-dependent fall-speed algorithm.
- Live count, deepest settled depth and above-weather status come from `PrecipitationSystem`. The wind-target readout is base flow at 2 m multiplied by drift; it does not pretend to be the complete turbulent trajectory.
- Existing cloud-source, altitude and collision gates remain. Settled snow/ice retires into the existing bounded depth field instead of retaining landed particles forever.

Audit limitation: the CPU simulation is exercised here, but a scene-viewport particle drawing path was not verified. The inspector states this. No rendered precipitation scene or GPU particle execution is claimed.

### Rainbow

The target already has `AtmosphericOptics::Rainbow`, GPU `RainbowAlong`, and the `PostBow`/`PostBow2` constant-record bindings. There is **no Rainbow-specific bake or image-playback path**. Top Bake and Use baked image tiles are disabled and explained, not simulated.

- Enabled, intensity, width, secondary strength, Alexander's Band and minimum rain path are bound to real settings. Minimum Path was previously authored but not exposed by the generic sheet.
- Preview evaluates the actual 14-wavelength CPU optical kernel: sun 10° above the horizon behind the viewer, rain visibility 1, rain path 500 m. The fixed angular projection clips below-horizon rays, applies the kernel's Alexander background attenuation, then display-encodes the RGB values.
- The preview is deliberately independent of current precipitation, so the optics remain inspectable when the scene is dry. A separate readout reports **authored rain visibility** for the renderer. It does not claim measured rain intersections or actual particle coverage.
- Rain feeds full visibility at 10 mm/h; drizzle reaches half visibility at 10 mm/h. Snow, hail and sleet feed zero: this kernel models liquid-water bows, not ice halos.
- Intensity/width/secondary/path/switch changes invalidate a 768×448 RGBA texture. The cache is per ImGui context, heap allocated, registered with the existing native texture API and unregistered/deleted at shutdown. No 1.3 MiB pixel array sits on the stack, and unchanged inputs do not regenerate optical pixels.

The main CPU scene raster does not gain a Rainbow post pass in this change. CPU optical preview and GPU constant-record staging are verified; shader compilation is not device execution. The diagnostic's explicit background attenuation is not a claim that every GPU background path reproduces that diagnostic composite.

## Integration and correctness fixes

`Tools/Build/Patches/WeatherInspector.patch` carries target changes to CMake, `EditorInstance`, `InspectorPanel` and `CelestialSequence`. New source files:

- `Engine/Editor/WeatherInspectorPanel.{h,cpp}`
- `Engine/DisplayPresentation/WeatherDiagnostics.h`

The shared Sun reconstruction runner applies Weather after Fog and copies/compiles these files with strict warnings. Editor metadata holds primitive snapshots and telemetry; project state remains owned by `CelestialSequence`.

- Dedicated Wind, Precipitation and Rainbow builders avoid a large generic builder frame. Precipitation behaviour is split into its own builder.
- All scalar edits reject nonfinite values and clamp to declared ranges; invalid type enums are rejected.
- Reusing a sheet clears weather metadata.
- Global celestial disable now stops precipitation simulation. Hidden precipitation no longer contributes rain visibility to Rainbow constants. These were missing gates in the audited backend.
- The native roster previously contained only Atmosphere, Sun, Sky, Stars, Moons and Lens Flare. It now also exposes Wind, Cloud Layer, Precipitation, Local Cloud, Height Fog, Atmospheric Fog, Local Volumetric Fog and Rainbow. Precipitation is depth 2 immediately below its Cloud Layer parent; the other new rows are depth 1. The original six indices remain stable. Roster-to-entity ownership and parent child counts are tested.
- The generic-sheet regression now uses the unknown `Count` sentinel because Wind is no longer generic. The Fog proof's metadata-reset check is updated likewise.

## Verification

Run sequentially; reconstruction shares `.cache/cpp-sun-full`:

```sh
python3 Exhibits/Workbench/Weather/RunNativePanel.py
python3 Exhibits/Workbench/Weather/RunNativePanel.py --debug
python3 Exhibits/Workbench/Weather/RunNativePanel.py --sanitize
python3 Exhibits/Workbench/Weather/CheckShaders.py
python3 Exhibits/Workbench/Weather/ServeNativePanel.py
```

`--reuse-build` requires an already reconstructed matching mode. It is for iteration, not a substitute for the full prior-stage regression chain.

| Gate | Result |
| --- | --- |
| Weather Release | 1,684 checks passed |
| Weather Debug | 1,684 checks passed |
| Weather ASan + UBSan | 1,684 checks passed, leak detection enabled |
| Prior inspectors, all three modes | Sun 326; Lens 74; Atmosphere 62; Moon 146; Stars 157; Clouds 582; Fog 127 passed |
| Shader regression | 18/18 compile to SPIR-V |
| Source/capture manifests | 15 Moon/Stars/Clouds/Fog/Weather manifests match |

Checks include real particle-pool state for all five types, finite falling trajectories, size scaling, drift, bounded pool, cloud rejection, orbit rejection, accumulation, global/visibility gates, wind model and CPU staging, spectral radiance and path gates, Rainbow ABI fields, hierarchy, scalar bounds/round trips, native pointer/keyboard controls, optical texture stability/invalidation, disabled bake, narrow scrolling, 2× capture and context teardown. The check count includes per-particle invariants; it is not 1,684 distinct test cases.

### Measured stack

GCC `-fstack-usage`, per-function bytes (not total nested call-chain usage):

| Entry | Release | Debug | ASan/UBSan |
| --- | ---: | ---: | ---: |
| Weather recorder | 704 | 160 | 4640 |
| Wind sheet builder | 368 | 2304 | 3072 |
| Precipitation sheet builder | 336 | 1408 | 1920 |
| Precipitation behaviour builder | 400 | 2016 | 2560 |
| Rainbow sheet builder | 168 | 1680 | 2240 |

Inlining explains differences between recorder frames across modes; individual panel functions are included in the emitted stack reports where present. Selected recorded entries pass an 8 KiB per-function gate in every mode. Release and Debug run with a 256 KiB Linux process stack; sanitizers use their default stack. No Windows/MSVC runtime or stack certification is claimed.

Commands, proof outputs, source hashes, prior proof summaries and unretouched native captures are in `Exhibits/Gallery/WeatherNative/`. Pixel files are losslessly re-encoded only. Water/fluid work and inherited unrelated files are excluded from this delivery.

# Atmosphere native inspector

## Scope

Both original outliner entities route to one native inspector; all **18 original atmosphere/sky fields** remain bound to the existing project state. This is the Atmosphere / Sky stage, followed by **Moon → Stars → Clouds → Fog (including local and volumetric)**. No fog fields are silently treated as part of the sky bake.

The engine panel depends on generic `EditorSheet` data, not Project-Zero. Project-Zero supplies borrowed image metadata and applies property/bake requests. Durable integration: `Tools/Build/Patches/AtmosphereSkyInspector.patch`, applied after the existing Sun and Lens patches to target `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`.

## How the baked image is shown

The existing atlas is **256 × 512 RGBA16F**, with a 256-square octahedral radiance map above a 256-square transmittance map. Both bake and successful `.environment` load retain the exact half-float bytes used by the existing upload path; there is no second approximate preview integration.

- **Atmosphere (default):** a vertical altitude profile sampled from the retained HDR bake in the azimuth opposite its recorded Sun, repeated horizontally. This gives a clean gradient without a sun-like spot; it is not a whole-sky image. Baked Sun direction is carried with the image, so stale images do not silently use the live Sun position.
- **Panorama:** an above-horizon panorama, 360° azimuth × 90° elevation, displayed at 4:1. Zenith at top, horizon at bottom. Preview-only Reinhard `L/(1+L)` then sRGB encoding; original HDR values are untouched.
- **Transmittance:** the corresponding hemisphere from the lower atlas half, sRGB encoded without the radiance tone map.
- **Full sphere:** 360° × 180° equirectangular radiance at 2:1. The black below-ground region is expected, not missing data.
- Sampling decodes real half floats and bilinearly samples the selected octahedral layer, clamping within that layer. This is a readable re-projection, not a claim of final scene-render parity.

The smooth atmosphere includes broad circumsolar scattering, **not a separately composited sun disc**. Moon/stars, analytic twilight, animated clouds and fog are not baked here. The final shader also has analytic horizon/solar-cone handling which this raw-bake viewer does not reproduce. Ground colour is original renderer albedo, not baked pixels.

## Native controls and lifecycle

Rayleigh, Mie, anisotropy, ozone, both density scale heights, atmosphere height, twilight glow/white line/civil option, sky tint/brightness, ground tint, baked-fetch option and readouts retain native meanings/ranges. Main values use `ControlPanel`'s native SliderPill. Extra native settings are in a collapsible rounded card. The baked-fetch tile uses green ON / red OFF styling.

The source-derived scattering, haze and ozone diagrams use the HTML reference curves, spectrum stops and vertex-colour gradient fills. Haze and ozone diagram inputs map original native ranges to the reference's 0–100 illustrative domain. They are explicitly **not calibrated bake plots**. Rasterization/font differences and some card arrangement remain; no browser pixel-parity certification is claimed. Reference fog cards are intentionally deferred.

`Bake sky` queues a request consumed at the existing GameExecution bake/upload phase. A manual request bypasses the lazy 30-quiet-tick/file-load path and requests a fresh bake. The existing save/register/upload/assign sequence remains responsible for residency. If that renderer phase is inactive, the request stays queued; this is not a new background worker.

Staging mismatch marks the old preview stale rather than fabricating live pixels. Renderer fetch still requires the existing enabled/seated/resident/matching conditions. **A new CPU bake or successful load clears the previous slot**, so an old GPU texture cannot be represented as the newly baked image. Only subsequent slot assignment restores that contract; the existing void `UploadTextures` API provides no new upload-success certification.

The project retains a 1 MiB half-float copy. Editor display buffers are heap-backed managed ImGui textures: three 640 × 160 images plus one 640 × 320 full sphere (about 1.95 MiB total). Revisions are process-unique atomic generation IDs, preventing same-address/new-project cache collisions. Borrowed pixels must stay valid throughout the editor frame; this does not add concurrent bake/read safety. Each ImGui context owns its cache, released on context shutdown.

## Verification and limits

Run:

```sh
python3 Exhibits/Workbench/AtmosphereSky/RunNativePanel.py
python3 Exhibits/Workbench/AtmosphereSky/RunNativePanel.py --debug
python3 Exhibits/Workbench/AtmosphereSky/RunNativePanel.py --sanitize
python3 Exhibits/Workbench/AtmosphereSky/ServeNativePanel.py --port 5180
```

The driver reconstructs the pinned target, runs Sun and Lens regressions, then builds the Atmosphere proof. File persistence uses the actual `SpaceCodec.cpp` / `SpaceExport.cpp`; their scene declarations require headers from Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf` (v1.3.290), acquired in ignored cache. No Vulkan device/library is used by this proof.

Proof artifacts are in `Exhibits/Gallery/AtmosphereSky`: commands, source/image hashes, selected-function stack reports, native captures and per-mode logs. Coverage includes both routes, 18-field inventory, scalar round trips/clamps, native bake request and slider pointer input, actual bake and save/load bytes, rejected stale files, slot invalidation, staleness/fetch fallback, known bilinear/layer/edge samples, half conversion, scroll/narrow/2× and teardown. Release/debug run with a **256 KiB Linux stack limit**, with an **8 KiB selected entry-point frame gate**.

This is headless native UI + CPU algorithm/record verification, **not full-app execution, GPU residency/playback, Windows/MSVC stack certification, shader parity or certified browser matching**. Captured “resident” status uses an explicitly assigned test slot. The generic `BuildOtherSheet` stack issue remains outside this scoped panel's frame gate. The new GameExecution branch is integrated as a source patch, not exercised by the headless renderer loop.

The runner extends the existing workspace Sun/Lens workbench; unrelated inherited files are not swept into this scoped change.

### Recorded results (2026-09-23)

| Mode | Atmosphere / Sky | Sun regression | Lens regression |
| --- | ---: | ---: | ---: |
| Release | 62 PASS | 326 PASS | 74 PASS |
| Debug / O0 | 62 PASS | 326 PASS | 74 PASS |
| ASan + UBSan | 62 PASS | 326 PASS | 74 PASS |

Selected release / debug frames: combined sheet **400 / 5,184 bytes**, panel **1,184 / 896 bytes**, image-cache update **208 / 208 bytes**. Release and debug passed the 256 KiB process-stack run. Sanitizers used the default stack, with leak detection enabled. These are Linux/GCC measurements, not Windows estimates.

### Naming / overview correction

The user-facing title and bake controls are now **Atmosphere**. Original native property keys and both existing entity routes stay compatible. `AtmosphereOverview.patch` adds the baked Sun direction for the new default altitude profile. The separate Sun disc was never in this bake: the previous panorama's bright patch was forward atmospheric scattering. That unaltered panorama remains an optional diagnostic view; the default is the requested clean atmospheric gradient. The current proof logs also check the opposite-Sun sampling direction and capture the panorama separately.

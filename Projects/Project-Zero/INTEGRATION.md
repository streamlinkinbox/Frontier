# Project-Zero integration: GPU transplant + showcase scene (default)

Opening the Project-Zero executable renders the showcase: a soil plain
scattered with one hundred analytical shapes (boxes, spheres, cones,
cylinders, pyramids, tetrahedra, wedges — near and far, one unique
material + colour each), under a sunset sky with full moon, stars,
broken cirrus, and marching ground mist — plus camera lens flare,
halo, and an anamorphic streak, all enabled by default. Everything is
lit strictly by ReSTIR DI + ReSTIR GI in `.slang`, nothing else; the
flare is a post-process camera artifact, not a light.

The product renderer is the `arena/01a08d16-frontier` Vulkan + Slang
stack, transplanted verbatim: sun/sky/light/ReSTIR all run on the GPU
through the branch's existing shaders. The CPU path tracer in this
patch is only the no-GPU sandbox stand-in — it renders the same
showcase scene for verification, nothing more.

## Apply (engine checkout)

From a fresh checkout of `SultanAladin/Frontier-` at engine `main`
(shallow `f17fb6f`, 2026-09-14 — the same base the delta was generated
and apply-checked against), with the `streamlinkinbox/Frontier`
`arena/01a08d16-frontier` branch fetched. Two commands:

```sh
git checkout arena/01a08d16-frontier -- Engine EngineContent Scripts Patches imgui.ini Projects/Project-Dyno Projects/Project-Zero
git apply PZIntegration/0002-project-zero-gpu-transplant.patch   # this file ships here
```

The checkout is byte-identical by construction (git does the copying,
so the transplanted engine is verbatim). The delta is 21 files /
~4250 lines: the showcase solver additions, the scene/camera/sunset
wiring, the CPU reference, and the explicit build lists. It keeps the
transplanted `ToolchainSequence.ps1` untouched (zero changes — every
file it compiles stays on its branch path).

## Build and run

Product — Windows MSVC + Vulkan SDK + Slang (double-click build):

```bat
Projects\Project-Zero\Construct.bat            :: builds the Vulkan .exe, opens the live window
Projects\Project-Zero\Construct.bat -Rebuild -Run
```

`Construct.bat` forwards to `Build/ToolchainSequence.ps1` (arguments
pass through: `-Configuration Debug|Release`, `-Rebuild`, `-Run`).
First build fetches the submodules/packages the script lists
(`imgui` on `docking`, GLFW, Jolt, cgltf, stb, …) — that step needs
network; afterwards the build is offline. The window opens on the
showcase at sunset with the sun-only flare in frame; fly with
`WASD` + mouse (Unreal-style fly-through), `ESC` quits. `--scene
cornell|outdoor|shaderball|showroom|drop` (or a `.gltf` path) selects
the other levels; the default is `showcase`.

CPU reference — headless, no GPU needed (sandbox stand-in):

```sh
cd Projects/Project-Zero && make
./bin/Project-Zero-CpuReference                                   # sunset showcase, 640x480
./bin/Project-Zero-CpuReference --sun 18.3 --yaw 0 --pitch 2      # moonlit night + stars + moon
./bin/Project-Zero-CpuReference --flarevar 1                      # anamorphic streak variety
```

Windows: `Build/Construct.ps1 [-Run]` builds
`bin\Project-Zero-CpuReference.exe` with `cl.exe` (`/std:c++20 /O2
/W4`, explicit 7-file list — it never globs `Source/`, where the
Vulkan entry point lives). CMake: `cmake --build build --config
Release --target Project-Zero-CpuReference`.

Flags: `--sun H`, `--yaw D` `--pitch D` (default `220 -2`, facing the
sunset), `--fog clear|morning|backlit`, `--width W` `--height H`,
`--bounce N` (default 8), `--passes N` (default 2), `--flare 0|1`,
`--flarevar 0|1|2|3` (cinematic/anamorphic/starburst/halo). Output
lands in `./Diagnostics/`: `ProjectZero_Showcase.ppm` always, plus
`.png` when Python is found. The PPM is byte-identical run to run
(`852e74ea…192` for the default frame; determinism verified by repeat
runs and by a pristine-tree fresh-apply rebuild producing the same
hash).

## What the delta contains

- `Source/RayTracingSolver.h/.cpp`: the branch solver plus
  `ConstructShowcaseScene` (deterministic `mt19937(2026)` field, 101
  per-object spans), the Z-up showcase primitives (`AppendBoxUp`,
  fixed-tessellation sphere/cone/cylinder/pyramid/tetra/wedge —
  arity-distinct overloads, the branch's `Append` behaviour is
  untouched), and a median-split BVH behind a fast path that only the
  showcase triggers (Cornell/Outdoor never build one, so their
  brute-force path is byte-for-byte the branch's).
- `Source/GameExecution.cpp`: `--scene showcase` alias, the
  export-once regen block (mirrors the Outdoor block:
  `ConstructShowcaseScene` → `BuildTriangleIndex` /
  `BuildMaterialDescriptors` → `SceneCodec::Encode` with
  `Name = "Showcase"` + spans, so each object becomes a named glTF
  node), the `Showcase` camera branch (`0, -14, 2.2`, yaw 220°,
  pitch −2° — the CPU proof framing), and the default level changed
  from CornellBox to Showcase (the Cornell box stays one `--scene`
  path away, untouched as the reference).
- `Source/CelestialSequence.cpp`: one line — the staged local hour
  moves from `15.5f` (mid-afternoon) to `17.93f` (sunset). Moon,
  speed, and every other celestial default stay branch-verbatim.
- `Source/CpuReferenceMain.cpp` (new): the headless CPU entry point —
  the v2 main with the windowed mode removed (no window, no worker
  thread, one frame then exit).
- `Source/RendererHost.*`, `Source/SkyFogIntegrator.*`,
  `Shaders/*`: the CPU path tracer, repointed at the Engine math
  (`TracingIndex.h`, `Engine/GeometricRaster/CameraProjection.h`,
  `Engine/DeviceExchange/OrientationClassifier.h`) with a mechanical
  `RayRecord` → `RayStructure` rename (identical layout). No
  algorithm changed.
- `Engine/DeviceExchange/TriangleSpan.h` (new) + one-line includes in
  `SwapchainExchange.h`, `TracingIndex.h`, `SceneCodec.h`: the
  4-field `TriangleSpanRecord` moves out of the Vulkan-requiring
  `SwapchainExchange.h` so the CPU reference can parse the solver
  header. Same struct, same namespace, visible everywhere it was —
  the GPU build is unaffected (this also fixes `SceneCodec.h`'s
  latent include-order dependency on the record).
- `Construct.bat`, `Build/Construct.ps1`, `Makefile`, `CMakeLists.txt`:
  the `.bat` forwards to `ToolchainSequence.ps1`; the other three
  become explicit 7-file CPU lists (`Project-Zero-CpuReference`).
- `Source/FlyThroughSolver.*`, `ToolchainSequence.ps1`, and every
  other transplanted file: branch-verbatim, zero delta.

## Architecture (unchanged from v2 unless noted)

- `Source/SkyFogIntegrator` owns the single showcase atmosphere. It
  compiles the shipped `.slang` core as C++ in one translation unit
  (no ODR risk: the header never names `.slang` types) and exposes
  sun/moon direction + radiance, sky radiance, the fog march, aerial
  perspective, and the panel post chain. The solar ephemeris and the
  panel defaults are verbatim copies of the gated harness sources,
  cited in-file; the showcase staging (sun hour 17.93, azimuth −60°,
  moon in the northern sky, broken cirrus, boosted stars) only stages
  the mirror algorithm's inputs, never its code.
- Frame convention: the engine world is Z-up, the render core is Y-up
  (panel frame). `RenderFromWorld` / `WorldFromRender` rotate between
  them. All sky/fog math runs in the render frame; only ray endpoints
  and light vectors cross the boundary.
- `RenderShowcaseFrame` keeps the six-phase structure: visibility,
  direct sun *and moon* illumination with shadow segment tests (both
  directional, no new sampler types), ReSTIR GI candidates (bounce
  misses contribute sky ambient), spatial reuse with Jacobian shift
  (clamped ≤ 10, M-cap 20×), the bilateral filter, then composition:
  surface + aerial perspective, fog march along the primary ray, sky
  for misses.
- Phase 7 applies the panel `post` lens flare mirror in linear HDR
  before the post chain. `FlareSpecification.slang` transcribes the
  reference `lensFlare` term by term (8-slot chromatic ghosts, the
  chromatic halo ring, the blue streak, starburst spikes, the hot
  core, `FlareVariety` weights 0 cinematic / 1 anamorphic /
  2 starburst / 3 halo) with the panel's own defaults (ghosts 5,
  halo 0.55, streak 0.8, chroma 0.65). Flare is sun-only, like the
  panel: the moon and stars never carry flare, and night frames are
  flare-free. `--flarevar` selects the variety (default 0).

## Verification record (sandbox, g++ 12, no GPU)

- Include audit: all 321 quoted `#include`s across the transplanted
  tree resolve — 310 relative to the including file, 11 via the
  script's `-I` flags (`imgui.h` and backends, `DisplayPresentation`
  via `-I Engine`, the Dyno pair via `-I Project-Dyno/Source`).
- CPU reference: clean build under `-std=c++20 -O2 -Wall -Wextra`
  (zero warnings) via `make`, via a direct `g++` line, and via a
  pristine-tree fresh-apply rebuild — all three produce sha256
  `852e74ea…192` for the default 640×480 frame, and repeat runs are
  byte-identical. C++17 and C++20 builds agree.
- Export-path signatures checked against the branch headers:
  `BuildTriangleIndex` / `BuildMaterialDescriptors` take `const
  RayTracingSolver&`, `SceneEncodeConfiguration` carries `Name` +
  `Spans` — the showcase regen block matches exactly, and spans
  decode to one named glTF node per object.
- v2 → v3 baseline shift, quantified: the Engine math swap moves
  last-ulp ray rounding, visible only where the image has
  near-discontinuities (the streak core): 99.847% of bytes identical,
  max abs diff 73 LSB on a single streak-core pixel, 41 of 307200
  pixels differ by more than 2 LSB. Scene, camera, sky, and flare
  are otherwise pixel-identical (view-verified).
- GPU build: not runnable in this sandbox (no Vulkan SDK / GPU), but
  every GPU translation unit is branch-verbatim except the five
  additive showcase edits and the two behaviour-neutral span-record
  moves, all reviewed line by line; braces/parens balance and the
  export-call arity were machine-checked.
- Proofs: `PZIntegration/showcase_{sunset,night,anamorphic}.png`
  (default sun-facing view, moon night view, anamorphic variety).

## Notes and limits

- The `.slang` files compile as C++ through the prelude for the CPU
  stand-in (their Slang validity is untouched); the GPU build
  compiles the branch's own shader sources with `slangc`.
- The moon, clouds, and star boost are showcase staging with no
  panel source (new features, default-off in the mirror gates).
  Placement/density are artist tuning, not physics errors.
- History: `0001-project-zero-showcase.patch` (commit `39443ad`,
  CPU-windowed era) is superseded by the transplant + `0002` and
  kept for the record; apply only ONE of them (`0001` onto bare
  `f17fb6f`, `0002` onto `f17fb6f` + transplant).

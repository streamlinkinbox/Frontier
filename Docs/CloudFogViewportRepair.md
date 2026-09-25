# Cloud/fog GPU viewport repair

The missing **view-ray cloud/fog rendering** identified in [the audit](CloudFogViewportAudit.md) is now implemented. This is production renderer code, not an outliner-only change or a baked sky image.

## What changed

- `CelestialSequence::PackPostRecord` now uploads live global cloud, local cloud, local fog, height fog and aerial fog settings, including visibility/Enabled gates, bounds, colors, density, coverage, wind, simulation time and quality budgets.
- `WeatherConstantRecord` appends 304 bytes to binding 24. The post block is now **512 bytes**; all existing star/flare/rainbow offsets remain unchanged. Host allocation, shader declaration and reflected SPIR-V layout agree.
- `WeatherMedia.slang` evaluates the existing density/noise/profile models in Z-up coordinates. It splits the view ray at cloud-slab and local-box boundaries, skips empty gaps, and integrates overlapping media with shared extinction and weighted scattering. A combined medium shadow march provides cloud/fog self-shadowing.
- The primary surface distance clips the march, including emissive/unlit early-return paths. Sky rays traverse the relevant volume intervals. Height and aerial fog use the existing analytic model, rather than unrelated screen-space opacity.
- Weather is composited **after storing clean lighting history, before tone mapping**. Animated wind and volume edits therefore do not smear old clouds into the history or reset surface GI every frame. Lens flare remains on top. Ordinary sky/post history resets now reach the dispatch in the same frame they are requested.
- The post/weather uniform uses command-owned staging and queue-ordered `vkCmdUpdateBuffer` with read/write barriers, rather than overwriting the shared UBO while a previous frame can still read it.
- **GI off:** the map-only `ShadowResolve` shader lacks celestial bindings. When weather is active, the existing compute fallback is selected with the GI feature flag still off (zero secondary bounces). This restores weather in that mode, but uses direct shadow rays instead of the map-only path. Disabling all weather restores the previous map-only route.
- Both Windows and CMake shader dependency lists include the new weather shader and its dependencies, so include edits trigger recompilation.

## Defaults and controls

Global clouds remain enabled by default. Local clouds, local fog, height fog and aerial fog remain disabled by default, to avoid unexpectedly filling existing scenes with fog.

For each local/fog entity, enable its **inspector switch** as well as its outliner eye/ancestor visibility. Outliner membership alone is not an Enabled switch. Local volumes retain their authored world positions and half-sizes; move the camera or adjust the bounds so the view ray intersects them.

## Verified here

- **209 checks PASS in Release and ASan/UBSan:** actual weather shader functions syntax-adapted to C++/GLM, not a separately rewritten renderer. Tests cover all six cloud profiles, local density, global/local pixel changes, surface-depth clipping, off-ray bounds, disabled/zero-length identity, overlap extinction, wind and analytic fog agreement with the CPU model.
- **22 upload/ABI/visibility assertions PASS in Release and ASan/UBSan:** actual `CelestialSequence` packer, including all five effects, a live atmosphere positive control, system/eye gates, independent local volumes and hidden wind.
- **22/22 production shaders compile to Vulkan 1.2 SPIR-V**, including the actual ReSTIR viewport shader.
- Actual shader reflection: Weather offset **208**, **19 rows**, stride **16**; PostConstants size **512**, binding **24**; star effects still at offset **192**.
- Source guards check post-history composition, actual surface-depth use, queue upload, GI-off routing and same-frame history reset.
- Real-header C++ syntax checks pass for `SwapchainExchange.cpp` and `GameExecution.cpp`.

Evidence is in `Docs/WeatherEvidence/`: `wiring-fixed.txt`, `wiring-sanitized.txt`, `host-syntax.txt`, `shader-mirror-release.txt`, `shader-mirror-sanitized.txt`, `shader-compilation.txt`, and `shader-layout.txt`. The old `wiring-audit.txt` deliberately remains as before-fix evidence.

**Not verified here:** Vulkan dispatch on a GPU, Windows linking/application execution, GPU validation-layer output, or hardware frame time. No working Vulkan ICD is available in this environment. Shader compilation and CPU execution are not presented as GPU execution.

## Scope and remaining limitations

- This restores camera-visible participating media, not full multiple-scattering transport. Secondary/refraction rays and weather contributions to surface GI are not extended by this change.
- Existing staged cloud shadows on solid surfaces remain separate from the live visible cloud layer. The new medium self-shadowing uses live global/local density, but this does not claim to synchronize the old staged ground-shadow model.
- The march is full resolution and bounded: 100 km view distance, at most 512 samples per occupied interval segment, 1–128 quality steps and 1–16 shadow taps. Long spans may undersample. This deliberately differs from the CPU reference's unbounded distant march; pixel equivalence is not claimed.
- Overlapping volumes are integrated as a mixture; the CPU reference orders whole volume spans and is only approximate for overlaps.
- Weather executes inside the existing compute shading dispatch. Its time is included in that kernel's timing; the separately reserved VolumeMilliseconds counter is **not** a measurement of this code.
- GI-off weather uses the compute fallback described above, so rendering cost and direct-shadow appearance can differ from the map-only route. Performance needs measurement on the user's GPU.

## Rebuild and check on Windows

From the repository root, rebuild both executable and shaders together (old shader binaries have the wrong uniform size):

```powershell
.\Projects\Project-Zero\Build\ToolchainSequence.ps1 -Rebuild
.\Build\Project-Zero.exe
```

Add `-FluidOpenMP` if that is part of your usual build. Use the normal shaded viewport, not a diagnostic buffer view.

1. Look above the horizon: global clouds should be present. Change coverage/density; hide/show the cloud row.
2. Enable Local Cloud, put its bounds in front of the camera, and vary density. Hide global clouds to confirm local cloud independence.
3. Repeat for Local Fog. Put an opaque object in front of the volume: fog/cloud behind that object must not overlay it.
4. Enable Height Fog and Aerial Fog independently; compare nearby and distant geometry.
5. Hide the Environment ancestor: its cloud/fog effects should disappear. Restore it, then switch GI off and repeat.
6. With a static Sun, enable Follow Wind and change wind speed. Clouds should move without permanently retaining their previous silhouettes.

## Reproduce focused checks on Linux

Dependencies are the repository's normal bootstrapped headers, plus a development-only GLM checkout and glslang. GLM is not a runtime dependency.

```sh
python3 Exhibits/Workbench/WeatherWiring/RunAudit.py
python3 Exhibits/Workbench/WeatherWiring/RunAudit.py --sanitize
# GLM 1.0.1, commit 0af55ccecd98d4e5a8d1fad7de25ba429d60e863:
python3 Exhibits/Workbench/WeatherWiring/RunShaderMirror.py --glm /path/to/glm
python3 Exhibits/Workbench/WeatherWiring/RunShaderMirror.py --glm /path/to/glm --sanitize
python3 Exhibits/Workbench/WeatherWiring/CheckGpuWeather.py --compiler /path/to/glslang
```

`Tools/Build/BuildGlslang.sh` can build a local compiler when no SDK compiler is installed. Generated binaries, adapted test source and SPIR-V remain under ignored cache/build directories.

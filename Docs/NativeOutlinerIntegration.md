# Native outliner texture integration

## Outcome

Stage 3 adds native ImGui image commands to the existing C++ outliner, using strict IconArt RGBA. The native editor CPU proof exits **0**, produces **8 fresh screenshots**, and retains its existing inspector and viewport. No web UI was changed. This is **CPU execution**, not evidence of a Vulkan/device run or full application build.

- `IconPresentation` owns presentation separately from `IconArt`.
- `NativeOutliner.patch` applies to pinned target `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`. It changes the outliner, optional typed artwork field, host startup, project celestial assignments, and CPU proof. Target source is staged separately in `.cache/cpp-outliner`; **252 original baseline sources remain byte-identical**.
- Existing row geometry, names, selection, visibility, hierarchy and drag/reparent code are not replaced. Only the glyph block draws a 24-logical-pixel image in the existing 24-pixel slot. This proof exercises filtering and existing editor controls; it is not a new exhaustive drag/reparent interaction suite.
- Assignments use typed symbols and semantic glyph/category enums, never editable names. Explicit `EditorInstance::Artwork` overrides defaults. Atmosphere has no corresponding imported symbol; unapproved Height/Atmospheric Fog replacements are deliberately not introduced. These keep legacy glyphs. Other unmapped custom glyphs likewise remain legacy.
- Supported artwork retains intrinsic RGB, aspect and straight alpha. Row fading affects opacity only. Unsupported assignments draw the strict pink cross and expose the result/diagnostic on icon hover. **82 of 151 assets are supported, 69 remain blocked**, including Sun. Diagnostic partial artwork is never selected.

## Texture ownership and bounds

Attach once after context creation (constructors and ApplyTheme cover either host ordering). NewFramePre prepares a 16-column atlas with two transparent gutter pixels. Drawing only looks up metadata and emits AddImage; it neither decodes SVG nor uploads a separate texture for each row.

The physical tile size is `ceil(24 * clamp(max(framebufferScale), 1, 4))`, 24–96 pixels. Resolution changes replace the atlas, not the artwork API. There are at most two live atlases: current and retired. A new DPI request waits while retirement is pending, keeping the last valid image usable. Maximum atlas pixel storage is 12,800,000 bytes, plus IconArt's separately bounded 16 MiB cache and small metadata. Displays above 4x reuse the 96px raster rather than growing memory without limit. This is framebuffer DPI scaling, not independent per-viewport mixed-DPI atlas selection.

The adapter registers `ImTextureData` through this pinned ImGui version's internal user-texture API. Existing Vulkan renderer texture handling is the intended upload path; it has been inspected, not executed here. Old textures use WantDestroyNextFrame and remain alive until the backend acknowledges Destroyed. Context shutdown unregisters and frees CPU metadata. **Shut down the rendering backend before DestroyContext**, as the target already does. The adapter asserts that no GPU backend allocation remains when freeing metadata. ImGui internal API changes require revalidation on upgrades.

## CPU proof and validation

`CpuDraw.h` genuinely samples arbitrary managed RGBA32/Alpha8 textures with bilinear filtering, vertex tint/alpha, clip rectangles, framebuffer scale, display origin, and vertex offsets. Triangle coverage uses a top-left rule to avoid double blending image-quad diagonals. It rejects unknown raw texture IDs unless the scene texture is explicitly supplied.

The editor integration routes new managed artwork through this sampler. Legacy font and scene rasterization remain as in the original proof, preserving its before-reference behavior. This is intentional: the old rasterizer's inclusive triangle-edge behavior is not a fidelity oracle for the new artwork.

Results:

| Validation | Result |
|---|---|
| Native EditorHost CPU proof | exit 0, 8 newly generated images |
| Focused presentation proof, release | 5,791 checks passed |
| Focused presentation proof, ASan + UBSan + leak detection | 5,791 checks passed |
| All 151 symbols, 1x and 2x | sampled output agrees with strict rasters within 1 channel value |
| Reuse and 90-frame rapid DPI churn | stable atlas reuse; at most two live atlases |
| Fixtures | clipping, tint/alpha, origin, 2x scale, vertex offsets, Alpha8 |
| Original editor lexical/no-allocation/chrome source gates | passed |
| Changed CelestialSequence and SolidArc host | syntax checks passed; not full linkage |
| Original baseline verification | 252 files unchanged |
| Target CMake patch | apply-check passed; not a CMake/full app build |

The original camera-green assertion encoded the retired monochrome tint, so the new editor gate checks visible camera-row ink instead; the separate pixel comparison checks exact artwork colour/alpha. New icons also exposed a false positive in the footer **mean brightness** probe: a bright icon above the footer matched its range. The gate now uses median brightness to reject localized icon/text outliers; its positional tolerances are unchanged. Initial failed output is retained as `InitialSamplerFailure.log` (the failure persisted when restoring the legacy sampler, confirming the probe issue).

## Reproduce

After the existing pinned dependency preparation and IconArt library builds:

```sh
python3 Exhibits/Workbench/IconArt/RunOutlinerProof.py
python3 Exhibits/Workbench/IconArt/RunPresentationProof.py
python3 Exhibits/Workbench/IconArt/RunPresentationProof.py --sanitize
```

The two focused modes need their respective `.cache/icon-art/{release,sanitized}/libthorvg.a`; `BuildProof.py` and its `--sanitize` option build them. `RunOutlinerProof.py` always creates a fresh isolated target copy, applies the integration patch, clears expected screenshots, builds and runs the actual EditorHost proof, and records hashes/logs. It does not run or rewrite the original baseline.

For target integration, copy the IconArt/IconPresentation sources, approved icon directory, CPU helper and existing ThorVG software-build support. Apply `NativeOutliner.patch` and the updated `IconArt-TargetCMake.patch`. Review Slate redistribution terms before distribution, as previously documented. The new CMake source entry links IconPresentation alongside IconArt; the CPU executable was compiled directly with g++, not CMake.

## Inspect

`Exhibits/Gallery/NativeOutliner/index.html` is a read-only viewer of native output, **not** a browser simulation of the native interface. It links the original before-reference. Commands, hashes, build output, successful proof logs and sanitizer output accompany the images.

Next stage: Sun-first native inspector work, while treating unsupported Sun artwork as an unresolved compatibility issue—not silently accepting its diagnostic render.

# ThorVG lifetime and presentation audit

Audited C++ revision: `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`.
ThorVG selected for this proof: upstream **1.0.0**, `6715f99ac106b6d4587f384a78c59fe957cfd2a3`.

## Findings before modifications

1. `.gitmodules` names ThorVG but this C++ revision has **no ThorVG gitlink**. The archive does not contain its files. ImGui, stb and toml++ do have immutable gitlinks; the baseline uses those exact revisions.
2. `SwapchainExchange.cpp:482` calls `tvg::Initializer::init(0u)` after window creation. `:669` calls `term()` at shutdown. The init result is not checked there. New icon ownership must not unconditionally terminate the host's runtime.
3. ThorVG 1.0.0's initializer counts references. `IconArt` holds one balanced reference, calls `sync()` before exposing pixels, and destroys pictures/canvases before dropping it. All calls and init/term are serialized on the calling UI thread. Returned immutable RGBA vectors are independent of ThorVG objects.
4. The target CMake ThorVG list is not compatible with this upstream release: it names `cpu_engine`, `tvgLoaderMgr.cpp`, `tvgSvgBuilder.cpp` and `THORVG_CPU_ENGINE_SUPPORT`. The reviewed release uses `sw_engine`, `tvgLoader.cpp`, `tvgSvgSceneBuilder.cpp` and `THORVG_SW_RASTER_SUPPORT`. `target_compile_definitions` also spells `DTVG_STATIC`, which would define the wrong name; the reviewed build uses `TVG_STATIC`.
5. The Windows script references `BuildThorVG.ps1`, but that dependency builder is absent from the audited archive. Its existing prebuilt library cannot be assumed identical to this proof. Do not claim the Windows application has been built here.

## Existing image presentation

- `SwapchainExchange::BringSceneViewSet` (`:3549–3569`) registers the existing scene image with `ImGui_ImplVulkan_AddTexture`; recreation waits idle and removes the prior descriptor. That is not yet a general icon upload facility.
- `SwapchainExchange::UploadTextures` (`:2377`) is the scene's bindless texture path, not an interchangeable ImGui icon identifier.
- `RenderScheduler.cpp:190` supplies the scene identifier to the editor.
- Original CPU proof `RasterizeList` (`EditorProof.cpp:86–94`) recognizes the scene identifier and otherwise samples the font sheet. Unknown icon identifiers would therefore incorrectly sample fonts.

**Next phase:** add a separate presentation owner for icon textures, retaining `shared_ptr<const IconRaster>` while CPU draw commands reference them. Extend the CPU sampler to resolve arbitrary registered image identifiers and fail on unknown ones. The device presentation must upload the very same straight-alpha RGBA8 bytes, use a compatible UNORM format, and retire descriptors/images only after outstanding work finishes. Neither change belongs inside `IconArt`; neither was implemented in this phase.

## Pixel representation and validation

`IconArt` uses ThorVG `ABGR8888S`, explicitly extracts R/G/B/A bytes from each packed word, and returns tightly packed straight-alpha RGBA8, sRGB colour interpretation. Numeric fixtures verify red/blue ordering, half-alpha red remaining red=255 rather than 128, transparent margins, non-square aspect fitting, clipping, gradient direction and Gaussian blur. Display scale determines physical raster dimensions; no application zoom is baked into logical layout.

## Dependency allocation correction

The pristine ThorVG 1.0.0 initializer defines global scalar `operator new` through `malloc`, and unsized `operator delete` through `free`, but no matching sized delete. AddressSanitizer exposed an allocation/deallocation mismatch in libstdc++ filesystem code before icon rendering began. The original report is preserved as `Exhibits/Gallery/IconArt/ThorVGAllocatorFailure.txt`.

`Tools/Build/StageThorVG.py` stages an initializer with only those two global replacements removed, retaining standard C++ allocation. `ExternalPackages` is **not edited**. The exact change is in `Tools/Build/Patches/ThorVG-StandardAllocation.patch`. Both release and sanitized icon proofs compile the staged translation unit. No sanitizer check is disabled. The reviewed source suffix must match or staging refuses to proceed.

## SVG compatibility is a release gate, not an assumption

The pinned SVG loader does not implement all effects used in the approved artwork. `IconArt` conservatively rejects unreviewed element types, text and external/reference resources in strict mode. It returns an explicit result/diagnostic and a visible pink-cross substitute. Optional diagnostic rendering keeps the unsupported designation even if the incomplete image looks plausible.

Current corpus: **151** assets (141 gallery SVGs plus compact UI assets and three text-free folder variants). **82** are admitted and within the recorded reference-comparison tolerance; **69** are blocked. The principal exclusions include `feDropShadow`, `feTurbulence`, `feDisplacementMap`, colour/compositing filter primitives, and text requiring a font setup. The unchanged approved originals are preserved. No raster substitute is silently substituted for approved vector art.

These 69 cannot be called visually ported yet. Before outliner integration, agree on a more capable reviewed ThorVG release, explicit effect support, or separately approved compatibility artwork. The high-detail Sun/Moon and several folders/effects need that decision. `Compatibility.tsv` identifies every affected asset.

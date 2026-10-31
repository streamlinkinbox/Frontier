# Project Tracker

Single progress and violation tracker. Detailed evidence belongs in Exhibits, not competing progress reports.

## EngineUpdate status

| ID  | Finding or task                        | Status      | Next action                            |
|-----|----------------------------------------|-------------|----------------------------------------|
| 001 | Checkout differs from source baseline  | Partial     | UI imported; rest deliberately absent. |
| 002 | Root agent rules used obsolete workflow| Implemented | Keep entry instructions consistent.    |
| 003 | Prototypes live in Documentation       | Open        | Inventory paths and launcher links.    |
| 004 | Inherited Internal tree remains        | Open        | Audit ownership before further ports.  |
| 005 | Old maps/backlog conflict              | Partial     | Historical pointers added; reconcile.  |
| 006 | Windows binary and GPU checks         | Partial     | Cross-built; Windows runtime pending.  |
| 007 | Four skills retained at old paths      | Intentional | Preserve until approved relocation.    |
| 008 | UI foundations and SVGs ported         | CPU checked | Windows integration still unverified.  |
| 009 | Six ImGui patches                     | Checked     | Applied, verified, reverted/reapplied. |
| 010 | Shared activation lifecycle           | CPU checked | Integrate into further scene classes.  |
| 011 | Scene renderer                        | Excluded    | Blank new Render directory by request.|
| 012 | Imported naming/formatting violations | Open        | Audit imported APIs before renaming.   |
| 013 | Source/artwork permissions            | Open        | Owner review before redistribution.   |
| 014 | Control Centre and quick panel        | CPU checked | Windows interaction validation next.  |
| 015 | Open fonts and catalogue              | Checked     | Four families, 30 faces, OFL retained. |
| 016 | Appearance persistence                | Checked     | Reload and invalid-save checks passed.|

## Verification

`Exhibits/ControlCentre/README.md` contains current commands, results, evidence, and limitations.
All six local tests passed in Debug and with address/undefined-behaviour sanitizers.
All 151 SVG symbols remain ready. Native Control Centre images are CPU raster output, not Windows GPU captures.
Windows x64 cross-compilation succeeded using Zig 0.13.0 / Clang 18.1.6 obtained through PyPI.
The unsigned Windows executable is packaged with fonts, SVGs, licences, and integrity metadata under ignored Binaries.
Native MSVC and Windows runtime/GPU execution are still unavailable. No Linux product binary is delivered.
No GitHub Actions are authorised.

## Known rule findings

The port preserves existing APIs for this bounded batch; it is not a clean naming/formatting certification.
Examples requiring repair: `VectorGlyphRecord`, short local names, comment vocabulary,
namespace brace layout, header banners, and non-approved decorative glyphs in inherited source comments.
The earlier official Windows example is now GLFW-hosted; its original display-adapter provenance remains explicit.
The four mandatory skill documents have not been weakened or edited to waive these findings.

## Scope history

The earlier documentation-only validation script was removed when that phase was clarified.
Subsequent user instructions authorise source porting, local validation code, proof images, and commit/push.
No full baseline replacement or inherited C++ relocation has occurred.

The selected theme/control configuration APIs now use `Configure...` rather than the banned `Assign...` verbs.
The new appearance files use Allman braces, flat namespace indentation, and descriptive file headers.

## Drawer and content correction

- Replaced the floating settings window with the reference top-edge trapezoidal drawer and inherited spring motion.
- Actual upward mouse swipe closes the sheet; added flick, axis lock, resize, focus-loss, and scaled-geometry tests.
- Renamed ContentDirectory and moved shared content into EngineContent/Icons and EngineContent/FontArchives.
- Native discovery reads metadata from TOML; Unicode file loading and corrupt-face rejection are exercised.
- GLFW 3.4 is pinned under ExternalPackages and used by the Windows host. toml++ remains the C++ format dependency.
- CLAUDE.md records Vulkan/Slang primacy, GLFW window ownership, content conventions, and TOML runtime formats.
- Windows runtime/GPU checks remain pending; no Actions are authorised.

#### Native Float32 vector surface map recovery (2026-10-19)

- Pattern scope remains approximately 68%; Stamp and full Pattern parity remain deferred/open.
- Added `VectorSurfaceCompute` with a typed `rgba32f` Vulkan storage image; the runtime derives format from SPIR-V,
  requires storage/sampling/linear-filter/transfer capabilities, and refuses unsupported devices rather than downgrading.
  Existing vector coverage and selection remain RGBA8.
- Compiles ordered layer/channel contributions from editable source, including visibility and layer masks. Separate
  RGBA32F maps cover colour, height, normal, roughness, metalness, opacity and occlusion. Contribution alpha is a
  separate mask; disabling a channel produces a neutral map and zero contribution. Layer-specific golden-thread
  metalness remains isolated from fabric metalness.
- Shared PFM encoding preserves IEEE Float32 bits, emits little-endian bottom-first `PF`/`Pf`, and does not clamp,
  gamma-transform or renormalize samples. The ZIP contains 14 actual GPU-readback fixture files, 32x32 each; it is
  not a document export. `Exhibits/TextureStudio/NativeSurfaceEvidence.html` presents the actual capture and GIF library.
- Seven real Vulkan channel dispatch/readback assertions pass strict and ASan/UBSan builds on SwiftShader. A cache-path
  regression now verifies 512x512 retired-slot submission and reuse of equivalent programs; it caught and fixed a reset
  that discarded the selected surface program before dispatch. The regression verifies authored gold/fabric separation,
  default neutral normals, finite Float32 samples, masks,
  PFM byte layout and strict/sanitized byte identity. Shader compiles with Slang WASM 2026.19 and reflects 24-byte
  surface push constants plus `Rgba32f` image storage.
- Native Texture Workspace now exposes the Pattern editor with persistent TOML, Vulkan coverage/selection, and a distinct
  dockspace. `PatternWorkspaceValidation` passes 606 checks on SwiftShader; its capture/result artifact is linked in the
  native evidence HTML. This runner did not execute a Windows desktop session.
- Float32-to-PBR bridge progress: 95%. `VectorSurfaceMaterialBridge` compiles five channels from a Pattern document,
  records their Float32 dispatches and image-to-host readbacks in one caller-owned submission, and exposes maps only after the
  caller's completion serial retires. No queue wait is inserted. Equivalent documents preserve the map cache; changed channel
  programs reconfigure and regenerate it. Five pyramids upload as RGBA32F authoring textures to the existing PBR shader.
- The native Texture Workspace host now routes its 512x512 Pattern document through that bridge on the shared Vulkan
  submission. Retired maps replace the selected material's live-preview channels; the material view shows the result without
  persisting source changes. The Windows x64 host cross-links, and packaging verifies all 13 surface/vector SPIR-V entries;
  native Windows GPU execution/capture remains a release gate.
- `MaterialRasterizationValidation` passes 224 checks on SwiftShader using 512x512 Float32 authoring maps and 128x128 PBR
  evidence captures. It exercises a single-submit five-map transaction, rejects premature retrieval, verifies
  unchanged/changed document caching, uploads actual GPU output, and compares the PBR centre capture with an independent
  scattering reference. The integrated bridge tests separate fabric and gold-thread layers: changing fabric Roughness to
  0.61 leaves gold Roughness at 0.18 and gold Metalness at 1.0 in the Float32 maps; reducing fabric SurfaceRelief from
  0.8 to 0.35 changes its derived tangent normals and PBR capture while keeping the gold-thread normal response unchanged.
  The three-frame C++ Vulkan GIF and provenance were regenerated from this 512x512-map run and remain embedded in
  `Exhibits/TextureStudio/NativeSurfaceEvidence.html`.
- `MaterialAtlasValidation` confirms changed Float32 authoring maps invalidate and repaint the same PBR preview selection,
  and that stale preview readiness is hidden until the replacement submission retires.
- Native Pattern channel enable toggles and strength controls are active for selected shapes; multi-selection applies each
  edit to every selected shape, and the document history preserves channel toggle Undo/Redo. Strength accepts 0–1 for
  ordinary channels and 0–4 for Normal; stitch/weave Height and Normal strengths cannot be reduced to zero. `PatternWorkspaceValidation`
  passes 626 checks, including selected-shape roughness-strength and surface-relief edits that leave the unselected layer unchanged
  and reach compiled GPU contributions. `VectorSurfaceValidation` confirms a GPU roughness disable removes only the fabric contribution
  while preserving the independent golden-thread roughness response.
- Native layers now persist a `surface_structure` classification (`freeform`, `stitch`, or `weave`). Selecting stitch/weave
  in the Pattern inspector enables and locks both Height and Normal channel toggles; document validation rejects missing,
  disabled, or zero-strength required contributions. TOML round-trip and invalid-weave rejection are covered in the native
  workspace validation, which passes on SwiftShader. This is explicit layer metadata and enforcement; automatic stitch/weave
  geometry generation and Windows interactive capture remain outside this phase.
- Native TOML now maps the web textile keys directly: `stitch_role` values `holes`, `thread`, and `highlight` resolve to
  NeedleEntry, StitchThread, and UpperThreadPass; `weave_role = "yarn"` resolves to WeaveYarn. The Pattern inspector can
  author these classifications, and validation enforces their parent stitch/weave structure and required channels. TOML
  compatibility exercises all four mappings; full Pattern JSON geometry import is still open.
- `VectorSurfaceValidation` runs the weave-classified fabric through all seven actual RGBA32F Vulkan channel dispatches,
  rejects disabled Height before compilation, verifies a per-layer Roughness strength of 0.23, and scales fabric relief by
  0.5 in GPU readback while preserving separate gold-thread height and normal response. `PatternWorkspaceValidation` confirms the inspector edit reaches the compiled
  contribution program. Both SwiftShader CTests pass; native Windows interactive preview capture remains open.
- Native evidence gallery now includes a three-frame C++ Vulkan PBR GIF: baseline, actual GPU-authored Float32 fabric/gold
  maps, and revised fabric relief/roughness with the gold response isolated. The linked JSON records source, executable and frame hashes; the fixture is SwiftShader, not a
  Windows desktop session.
- Still open: execute and capture the interactive Pattern 1:1-to-material-preview workflow on the native Windows host,
  including a per-shape channel edit; convert web Pattern JSON geometry and construction into native vector instructions;
  calibrate SurfaceRelief against measured height units and profile performance.
  Stamp remains deferred. The persistent/scene-level Apply action stays disabled until integrated native GPU submission and
  capture pass.
  GIF display is 8-bit evidence only; numerical Float32 checks are authoritative. No pixel stream is used.
- Reproduce: install pinned UI dependencies via `python Automation/PrepareUI.py`; compile the surface shader with
  `node Automation/CompileVectorSurface.mjs <Slang-WASM-JS> <shader-directory>`; run
  `python Exhibits/TextureStudio/Code/VerifyVectorSurface.py --driver <Vulkan-driver> --shader <VectorSurfaceCompute.spv> --destination <scratch>`.

#### Session rebuild on `arena/db2f9953-frontier` (2026-10-20)

- Restored pinned UI deps + fonts in a fresh sandbox, configured with `-DFRONTIER_HOST_VERIFICATION=ON`,
  built all host targets (GCC 12.2, Debug) and ran the full CPU suite: **21/21 CTests passed** (~70 s).
- Repaired four stale validation harnesses (no production-code changes): font inventory 4/30 → 5/35
  (`ControlCentreValidation`), per-domain icon rasterization (`InterfaceValidation`, Texture/Fluid symbols are
  denied to the Editor domain by design), canvas hover before preview F/arrow keys plus a non-vacuous F check
  (`MaterialPreviewConfigurationValidation`), and the full `###`-suffixed dockspace window name plus a null guard
  (`DockingValidation`, previously a null-`ImGuiWindow` SEGV). Docking also passes under ASan/UBSan.
- New `Exhibits/index.html` hub embeds the recorded HTML/native captures, per-workspace port status vs the Alloy
  reference (`Experimental/TextureStudio`), and this rebuild log; served from `Exhibits/` for preview.
- Still blocked in sandbox: SwiftShader/Vulkan readbacks (no `slangc`/driver), Windows link/run, Alloy `npm`
  verification. Carried UI defects (Project Browser 1:1, zoom FPS, layer cards, toggles, dropdown reuse,
  slider/ramp/gradient editors, rotation math, drag latency) remain queued after this rebuild.


## Windows preview 39483329514 (2026-10-20, branch arena/db2f9953-frontier)
- Commits: ba9b105 (settle-gated Pattern Float32 bridge, preview clip rects, dark-slate default stroke) + 42c9e54 (CI trigger for this branch).
- CI run 39483329514: Windows + Publish jobs success; Release unsigned preview at frontier-windows-39483329514.
- Linux host verification on ba9b105: full ctest 21/21 green; VectorSelectionValidation rotation/resize 1431/1431 pass (CPU).
- Confirmed open shader gap: PreviewPbrSurface samples albedo only; height/normal have no bindings (next round).

## Windows preview round 2 (branch arena/db2f9953-frontier, commits ba9b105..2fd6ea4)
- ba9b105: settle-gated Pattern Float32 bridge (drag perf), preview clip rects, dark-slate default stroke.
- 6b13015: parked material drafts across selection, full-bleed cover-fit preview + title scrim, exposure card, auto-size layer cards.
- 2fd6ea4: draped cloth preview shape, six-seamless-preset Pattern gallery, redesigned close dialogs.
- Linux host verification: full ctest 22/22 green (incl. 638 pattern-preset checks, cloth fold assertions).

## Windows preview round 3 (branch arena/db2f9953-frontier, commit e8755ee)
- Gesture-gated Pattern bridge (no regen while a gesture/pen stroke is in flight).
- Single-axis orbit rewrite (yaw about world-up, sway-tested old-fail/new-pass).
- Styled tooltip cards everywhere in Material; Open Library dialog restyled.
- TOML-backed preset gallery (six shipped .vector.toml files, built-in fallback).
- Apply-to-material gating with persistent snapshot per material.
- Linux host verification: full ctest 22/22 green (669 preset checks).

## Windows preview round 4, batch 1 (branch arena/db2f9953-frontier, unreleased)
- Pattern gallery preset thumbnails: CPU-drawn ImDrawList vector previews replaying each preset's
  instructions with VectorCoverage.slang corner/rotation conventions; click-to-open + hover tooltip.
- Project Browser Details rail: full projectspace metadata (identity/template/format/version, content
  roots, level/catalogue/pin counts, startup level, workspace theme/accent/font/scale) + Open button.
- Editor activation: opening a project launches EditorWorkspace (proven WindowsUI pattern ported into
  WindowsFrontier) with a top bar (back, project name/stats, Control centre / Theme / Settings buttons
  driving the ControlCentre drawer), a Viewport summary tab, and return-to-browser teardown.
- Materials: procedural detail textures (Automation/GenerateSurfaceDetailTextures.py) wired into five
  presets — tour/practice golf-ball dimple normal+ORM (TextureMask 6, 4x repeat), three solar-module
  cell/busbar albedo+ORM+normal sets (TextureMask 7, 2x repeat, shared ORM/normal).
- Correction to the round-2 note: the material preview already renders through MaterialSurface with full
  texture bindings (normal/height included) whenever a material residence exists; PreviewPbrSurface is
  only the albedo-only fallback. Golf/solar relief was a data gap, not a shader gap — no shader port needed.
- Height remains pattern-authored (AuthoringMaps[2] via Apply-to-material); .material files have no height slot.
- Linux host verification: full ctest 22/22 green (Docking close-all updated for the Viewport tab;
  MaterialLibraryValidation now compiles all five texture-wired presets incl. PNG decode).
- Still open: rotation-axis mapping vs user expectation, asset-browser HTML 1:1 (HTML file not in repo),
  remaining surface-detail ports (orange-peel, scratches, weaves need shader/UBO targets), zoom-FPS check.

## Windows preview round 4, batch 2: orbit fix (branch arena/db2f9953-frontier, unreleased)
- User report: yaw spins fine, but vertical drags roll "as if on Z" and the pivot feels off-centre.
- Root cause: orbit composed yaw-after-pitch (Ry*Rx), so pitch increments rotated about the yawed local
  X axis (a view-axis roll at 90 yaw) and yaw leaned the object up-axis sideways by sin(yaw)*sin(pitch).
- Fix: LookAt-style orbit (Blender/three.js convention) — yaw about screen-vertical world-up through the
  pivot, pitch about the fixed screen-right axis. Pure-yaw and pure-pitch poses are bit-identical to
  before, so the approved feels are untouched; only combined yaw+pitch changes.
- Correction to the round-3 sway test: zero-ellipse turntable yaw is incompatible with a screen-vertical
  object up-axis (proven: fixed front height + vertical up forces no yaw). Replaced with three
  old-fail/new-pass assertions: up-axis never leans, pitched tips never roll, pitched yaw traces the
  standard screen ellipse; plus a pivot-exactness guard across all five shapes.
- Linux host verification: full ctest 22/22 green; old matrix fails the new lean/roll/ellipse assertions.

## Windows preview round 4, batch 3: Alloy preset parity (branch arena/db2f9953-frontier, unreleased)
- User rejection: the six native-invented presets (and their gallery) did not match the Alloy Studio
  HTML. "Alloy Studio" is the web TextureStudio itself (schema alloy.pattern.v1); source of truth is
  Experimental/TextureStudio/src (PatternEditor.jsx gallery, patternDocument.js catalog of 103 families).
- Converter upgrades (Automation/ConvertWebPattern.mjs): per-copy instructions stay lean when they fit
  the 128-instruction budget, else tile-wrapped copies concatenate into one tile-space contour range
  (identical union under the same fill rule); minimal adjacent same-paint merging rescues the 65-layer
  Graduated lattice; half-drop/single-axis tiling accepted as documented straight-XY equivalents (baked
  tile pixel-identical, neighbour arrangement differs); miter/bevel joins and butt/square caps substitute
  round; anisotropic strokes use the larger axis; non-sized flowers import. All 178 family x palette
  variants convert (was 143 OK / 35 fail); deviations attach to each conversion for the sidecar.
- GPU/host budget raise: cubic segments 4,000 -> 32,768 (128 KB -> 1 MB storage buffer; shader needs no
  change since segments are an indexed buffer), vector source 600 KB -> 4 MB (codec, gallery loader,
  workspace save/load, import validation). Instruction cap stays 128 (shader stack array). Catalog peak
  is 18,954 segments (weaves); 25,600 is the editing-API maximum, so the 32,768 boundary is pinned by a
  struct-level coverage-validation test instead of the join fixture.
- Batch export (Automation/ExportPatternLibrary.mjs): 103 families / 178 palette TOMLs (37 MB) +
  Library.toml sidecar (families, 8 collection groups, 4 palettes, per-palette files, 24 deviation
  notes) under EngineContent/PatternArchives/TextureStudio/starters/. Serialized floats round to 7
  decimals (float32-parse within 1 ulp, invisible). The six invented TOMLs are deleted; the C++ fallback
  builders remain only for a missing preset directory.
- Gallery ported 1:1 from PatternEditor.jsx: kicker, two collection shortcuts, collection dropdown,
  search (name/group/description/study), conditional print-palette dropdown, heritage note, result
  count, two-column cards (76px cover-fit previews, wrapped names, description tooltips, selected
  ring), 24/page paging, trailing notes. Decoding is lazy per page (96-entry FIFO cache). Two
  documented deviations: DM Sans has no arrow glyphs, so paging/shortcut arrows render as ASCII
  < >; "Generate a layout" generator section is deferred (self-contained follow-up, not presets).
- Card tessellation: paint-order layer walk resolving Over/boolean composition; cubics flatten,
  RDP-simplify (closed-loop farthest-point split) and ear-clip to triangles once per preset; strokes
  replay as single polylines; fills submit batched with off-thumb culling. Fixed two real bugs found
  by headless harnesses: RDP collapsed closed loops (degenerate chord), and 16-bit ImDrawIdx overflowed
  past 64K verts per window draw list — 32-bit indices are now a PUBLIC compile definition on
  FrontierImGui (tracked; the vendored imconfig.h is untouched because CI recovers imgui fresh).
- Validation: PatternLibraryPresetValidation is Library.toml-driven (pins 103/178, decodes and
  coverage-validates every file, 1954 checks); new PatternLibraryGalleryValidation pins collection
  counts (30/16/10/10/10/10/10/7) and search counts, tessellates all 178 variants into finite geometry
  (830K triangles); headless card-render harness draws all 103 cards with draw-list invariants intact.
- Linux host verification: full ctest 23/23 green; converter node --test 7/7; ASan/UBSan clean on
  decode, tessellation and card rendering.
- Still open: rotation-axis follow-up already shipped in batch 2; asset-browser HTML 1:1 (HTML file
  still not in repo); "Generate a layout" section; remaining surface-detail ports; zoom-FPS check.

## Slate layer-stack port (2026-10-21, Slate arena/4006465a-slate cb8dcf0 onto db2f9953@34537e1)

- HTML first: `Experimental/TextureStudio` gains the Alloy material workspace (MaterialPanel PBR layer
  stack with per-channel sources, MaterialSpecification authoring document, FieldSpecification coded
  generators, FieldProjection Float32 atlas, editable camouflage palettes); catalogue 127 -> 131.
  Verified by production build and 7/7 Node model checks; Playwright specs ported but not run
  (browser download blocked in the sandbox).
- C++ port: coded fields in MaterialProceduralBaking (double-precision JS parity, 8 new ops on
  Albedo/Normal, palette text params), `Engine/ContentInterchange/MaterialAuthoring` document model with
  strict normalize, `[Authoring]` TOML round trip in SurfaceMaterial, pool text edits
  (ConfigureProceduralText), panel palette colour editors, deterministic catalogue export of the four
  camouflage presets with coded Albedo channels. Evidence: `Exhibits/TextureStudio/Results/LayerStackPort/`.
- Linux host verification: full ctest 26/26 green (new MaterialAuthoringValidation pins JS goldens,
  normalize rejection, link/designer projection, TOML stability, catalogue camouflage bakes).
- Boundaries still open: coded fields bake no ORM channel; woodland fabric weave relief is not yet
  composited under the camouflage field; Float32 field atlas stays HTML-only (native path is the CPU
  baker); panel renders strata rather than the full HTML authoring sheet; Windows runtime unverified.

## Inherited tree reclamation (2026-10-22, branch arena/1b8b280c-frontier)

- Reclaimed the inherited engine code trees from this branch (`Internal/`, `Executables/`,
  `Applications/`, `memory/`, `PaintingSurface.bat`; 736 files). Complete trees remain on `master`
  and in history; nothing in the current build referenced them (verified by configuration census
  and a full host build + test run).
- Retained the inherited design documents under `Documentation/Plans/References/InheritedEngine/`
  (170 files: `Prototypes/`, `Research/`, root-level plan/reference notes, and the notes that lived
  inside the reclaimed trees). `Documentation/FolderStructure.md` records the review outcome.

## Orbit staging and LED radiance correction (2026-10-22, branch arena/158c63c8-frontier)

- Specimen-group pitch (0.18 rad, baked into ball+pedestal) removed: pedestal lathe is level
  (rim height spread .34 -> ring-exact), Pivot/Radius re-measured. Preview key light, sky up and
  studio softboxes are world-locked through the orbit (identity at the default pose).
- LED radiance sampled through the first stratum's UV transform instead of raw coordinates.
- MaterialPreviewConfigurationValidation pins ring level + rigid light/up carry; shaders compile
  via the pinned Slang toolchain; host suite 26/26.

## Environment archives and layered inspector (2026-10-22, branch arena/7aa23edd-frontier)

- Baked five neutral equirect studio environments (`EnvironmentBake`) into
  `EngineContent/EnvironmentArchives/`; the shader samples them as a world-frame texture array
  (set 0 binding 3), replacing the tinted analytic presets and the analytic softbox integral.
  Key light + strength are derived per archive at scan time; equirect PNG drops import by rescan.
- Viewport: environment dropdown is now a card with CPU sphere previews, Exposure and Light
  intensity; the standalone exposure card is removed.
- Inspector: recipe controls categorized per stratum under LAYER headers (groups preserved as
  cards), uncovered stratum scalars editable per layer (incl. Thickness), profile-less generic
  editors on all layers. Host suite 26/26 (645,546 preview-config checks incl. card gestures);
  Windows GPU confirmation pending the user's hardware test.
- Still deferred: sand op binding, thin-film, glass depth, camouflage/ballistic underlays.

## Environment-switch freeze correction (2026-10-22, branch arena/71dc0cca-frontier)

- Windows test log: `PreviewConfiguration[2]` (now the environment layer) was still pinned to
  zero by `ValidateMaterialScattering`, so every non-default environment threw inside
  `ConfigurePreview`/`ConfigureSelection` and the viewport froze on its last good frame.
  Contract now bounds the layer slot; pinned by CPU checks (645,550).
- Light intensity missed the smoothed-camera discrete copy (applied only on settle snap); fixed.
- Environment sampling at mip 2 (firefly stipple), strength target 0.3 -> 0.2, key weight
  0.4 -> 0.32 (tonemap flooding), catalogue cap 24 -> 8 (pyramid layer contract). Suite 26/26.

## Fuzz/sheen BRDF and inspector card layout (2026-10-22, branch arena/b36a0fa5-frontier)

- Fabrics (velvet, wool, denim, cotton, silk, satin, …) and the leather/paper/foliage recipes that
  already carried `sheen`/`sheenRoughness`/`sheenColor` sliders were missing the grazing-angle rim
  lobe the HTML reference renders; native output looked flat. Added a Charlie-distribution +
  Ashikhmin-visibility sheen term (glTF `KHR_materials_sheen`-style), identical math in CPU
  `ScatteringSequence.h` and GPU `ScatteringEvaluation.slang`, additive on the opaque stratum only.
- ABI: `ScatteringStratum` gained `Sheen`/`SheenFinish` float4s (64 -> 96 bytes); cascaded through
  `PreviewScattering` (672 -> 928) and `MaterialScattering` (1504 -> 1760), every offset re-asserted
  in `SurfaceMaterial.h` and the GPU-reflection parity guard in `Automation/CompileSurfaceShaders.mjs`
  (this guard caught a stale hardcoded size before it could ship, the exact failure mode PR #7's
  dormant-contract freeze taught us to watch for).
- `SheenColor`/`SheenIntensity`/`SheenRoughness` are now live TOML keys, wired end to end: the colour
  swatch and the recipe sliders both write through `MaterialLibrary::ConfigureStratumColour` /
  `ConfigureStratumCoefficient`. The "Sheen colour" swatch is data-driven — any recipe profile that
  exposes a `sheen` control gets it, not a hardcoded per-material list.
  37 shipped `.material` files seeded with their HTML-authored default sheen values (fabrics, leather,
  paper, foliage) so the lobe is visible without hand-tuning. `MaterialLibraryValidation` gained a
  coefficient/colour/compile round-trip check for the new fields.
- Inspector cards: added an 8*Scale inset so cards no longer sit flush against the panel edge and
  scrollbar; fixed the per-card row count (profiled Finish cards no longer pad in 3 phantom rows,
  unprofiled Finish cards now count their real 6 generic sliders) so a card's background no longer
  under/overshoots its own controls; every card (not just the first per layer) now carries a small
  "LAYER N" tag so grouping stays legible while scrolling past several cards in the same layer.
  Host suite 26/26.
- Deferred, unchanged: sand op binding, thin-film, glass transmission depth, camouflage/ballistic
  coded-field underlays.

## Save-dialog modal chrome (2026-10-22, branch arena/8c806870-frontier)

- The "Unsaved material", "Open material library" and "Material destination" (create/duplicate)
  dialogs in TextureWorkspace, plus Pattern's "Unsaved pattern" close prompt, inherited the docked
  workspace's zeroed `WindowRounding`/`WindowPadding` and rendered as flush, square-cornered boxes
  with text crowding the edges.
- Added a local `MaterialModalScope`/`PatternModalScope` RAII helper per panel (mirrors the existing
  `MaterialTooltipScope` and the Editor's `ConfirmationDialogue` convention already used elsewhere)
  that scopes 20x18 padding, 10px rounding and a faint 1px border to just these popups, restored
  before the surrounding workspace's zeroed style resumes. No layout/behaviour change otherwise.
  Host suite 26/26.

## Fork merge: c7egoist/Frontier arena/ec6ca01d-frontier (2026-10-23, branch arena/c8d99312-frontier)

- Merged `c7egoist/Frontier@arena/ec6ca01d-frontier` as-is, same handling as the earlier
  `c7egoist`/other-session fork merge (PR #8): its unique commits beyond the shared `5a8c1b8`
  ancestor are a standalone-engine GGX material response port and an atmospheric-environment MIS
  integration, plus the `Exhibits/CompleteRenderer`, `EnvironmentPipeline`, `IlluminationChain` and
  `IndirectIllumination` reference exhibits and the `Frontier/Engine/Rendering/*` portable renderer
  tree they build on. Each exhibit carries its own standalone `CMakeLists.txt` and is not wired into
  the root build (same as the prior fork's `Frontier/`, `VisualProof/`, `Docs/` overlay) — merge was
  clean with no conflicts, since this branch's commits since the shared ancestor don't touch the
  files this session's sheen/inspector/dialog work changed.
- Verified: full root build + `ctest` 26/26, confirming the merge is inert for the shipped build.

## Pattern layer stack visual parity + selection-bounds regression coverage (2026-10-23, branch arena/649fea90-frontier)

- HTML Pattern Studio's layer rows (`PatternEditor.jsx`) now reuse the Texture workspace's
  `tp-layer-row` chrome verbatim: a drag handle, a per-layer preview thumbnail (new
  `PatternLayerThumbnail.jsx`, rendering the layer's own vector contour sized to just that shape's
  own bounds via `patternSizedPath`, falling back to the shape-kind glyph for kinds with no
  contour), the name/kind/opacity meta line, then eye/lock icon buttons — same classNames, same
  look, as requested, instead of the retired bespoke `se-layer-row` styling. Retired the now-dead
  `se-layer-row`/`se-layer-name`/`se-layer-toggle` CSS across `shapeEditor.css`, `studioTheme.css`
  and `artworkAuthoring.css`; added narrow overrides so the legacy `.pe-layers button > span` 12px
  swatch sizing doesn't clobber the ported thumbnail/kind-icon/name slots.
- Investigated the reported native (C++) Pattern Studio selection-hitbox issue ("highlight spans the
  whole canvas instead of hugging the selected shape"). Traced `QueryVectorSelectionExtent` /
  `QueryVectorAuthoringSelectionBounds` (`VectorAuthoringSelectionEditing.cpp`) and the pen tool's
  `EncodePath` (`CubicPathEditing.cpp`): both already compute tight, shape-local bounds for a single
  selected primitive or hand-drawn/library cubic-contour path in every scenario exercised, including
  a tile-normalized library-path simulation. Found and fixed a real gap instead: `VectorSelectionValidation.cpp`
  (1431+ assertions covering this exact selection-bounds/resize/reorder surface) existed but was
  never wired into CMake/ctest, so it never ran in CI. Added it as a real ctest target and added two
  new regression checks pinning a hand-drawn motif and a tile-normalized imported path to their own
  tight bounds, not the full 512 canvas. Host suite 27/27 (up from 26). If the on-canvas symptom
  still reproduces after this, it is most likely the multi-select case (selecting an entire
  array-placed group, whose true combined bounds legitimately span the tile) rather than a bounds
  bug — flagged for the user to confirm against the next preview.
- Ported the same layer-row preview into the native Pattern panel. `ConstructLayers()`'s row no
  longer draws a static placeholder swatch: it now renders that layer's own shape, cropped to its
  own tight extent (new `EncodePatternLayerThumbnailSvg`/`RasterizePatternLayerThumbnail` in
  `PatternThumbnailRaster.h/.cpp`, sharing the existing per-instruction SVG emitter with the gallery
  encoder so the two never drift). A second, smaller GPU atlas (`PatternWorkspacePanel::LayerAtlasTile`
  = 64px, 12×12 grid) carries every row's thumbnail for the open document and rebakes only when the
  document's edit revision moves; `WindowsTextureStudio.cpp` wires it up exactly like the existing
  library gallery atlas (`PatternLayerAtlas`/`PatternLayerAtlasDescriptor`, stage/upload/confirm/
  finalize, `Pattern->QueryLayerThumbnailAtlas`). Before the atlas is baked (or if no host wires it,
  e.g. in a headless/test build) the row falls back to a CPU vector replay of just that one layer via
  the existing `TessellatePresetDocument` tessellator, and only falls back further to a bare outline
  if that layer has no renderable geometry at all. `PatternThumbnailAtlasValidation` gained two new
  headless test blocks (single-layer thumbnail crop + the open-document layer atlas revision/bake
  contract) — 58 checks, up from 45, no new exhibit/CMake target needed. Host suite stays 27/27.
  The Vulkan wiring in
  `WindowsTextureStudio.cpp` is Windows-only and cannot compile-check in this Linux sandbox (same
  constraint as the rest of that file); it is a line-for-line structural mirror of the already-shipped
  `PatternAtlas` plumbing, reviewed by hand, and will be confirmed by the next Windows CI build.
- Pattern library motifs: the composable procedural modifier stack the user asked for (their own
  worked example — Diamond weave as one ring array/inset, mirrored top/bottom, then aligned to the
  tile edges) now exists in both HTML and C++, proven against the "Diamond weave"/"Diamond Weave"
  starter in each:
  - HTML: new `Experimental/TextureStudio/src/patternProcedural.js` exports three pure, reusable
    operations over a plain layer array — `arrayInset(layerFactory, kind, base, {count, sizeStep,
    stepX, stepY, colors, roles})` (repeats/shrinks one base layer, optionally role-tagging each
    copy), `mirrorMotif(motif, {axis, through})` (reflects x/y about a line, negating rotation) and
    `alignToEdges(motif, anchors)` (translates a motif cluster to tile-relative anchors, optionally
    recolouring by the `role` tag an anchor's `recolor` map names). `patternDocument.js`'s "Diamond
    weave" starter branch now calls these three operations instead of three nested hand-written
    loops; verified bit-for-bit identical output against the old flat implementation (same 16 diamond
    layers, same coordinates/colours, via a direct comparison script) before and after the rewrite.
    `validatePattern()`'s field-by-field layer rebuild already drops unknown properties, so the
    generation-only `role`/`proceduralRole` tag never reaches the saved/exported document.
  - C++: three analogous free functions added to `Engine/ContentInterchange/PatternLibraryPresets.h`
    — `ConstructInsetMotif` (runs the existing, already-shipped `GenerateVectorAuthoringArray` engine
    against a throwaway scratch document and harvests the instructions as a plain list, so Array/Inset
    reuses one tested placement engine rather than a new shrink loop), `MirrorVectorAuthoringMotif`
    (reflects a plain instruction list about an axis) and `AlignVectorAuthoringMotifToEdges`
    (translates copies to anchors, recolouring instructions that match a sentinel accent colour). A
    new 7th library preset, "Diamond Weave" (`ConstructPatternLibraryPreset(6)`,
    `QueryPatternLibraryPresetCount()` now 7), is built from exactly these three calls: one 4-ring
    inset array (242/189/136/83, matching the HTML sizes), mirrored across the tile's vertical
    midline, then aligned to the left/right edge quarters with two different accent colours.
    `PatternLibraryPresetValidation` gained a dedicated structural check (ring count, inset sizes,
    per-edge accent count) plus the count-change assertion bump to 7 — now 2065 checks, all existing
    index-0..5 preset checks untouched. Full build + `ctest` → 27/27 PASSED.
  - Still open: this pattern (one base shape + array/inset + mirror + align-to-edge) has only been
    applied to the Diamond weave/Diamond Weave starter so far, as the proof of the approach — the rest
    of the ~7,874-line, 9-module HTML pattern library and the remaining C++ presets have not yet been
    converted. Scoped but not started; large enough to warrant explicit user sign-off before an
    unattended full-library pass (risk of silently regressing 100+ shipped pattern designs without
    per-pattern visual verification).

## GPU mesh baking (Vulkan compute, GTX + RTX) (2026-10-26, branch arena/4014756b-frontier)

- The Baking Workspace's 27 mesh maps (previously CPU-only in `Engine/ContentInterchange/BakeMesh.cpp`)
  now have a GPU route behind an inspector "Bake device" choice (CPU default; GPU appears when the
  host supplies a device name and also unlocks 2048/4096 resolutions). One Vulkan compute path serves
  GTX and RTX — GTX has no RT cores/`VK_KHR_ray_query`, and the bake needs closest-point bevel queries
  that are compute work anyway; CUDA/OptiX and an RTX ray-query fast path were considered and deferred.
- New `Engine/ContentInterchange/BakeField.h/.cpp` flattens a bake request (std430 triangle/BVH slots,
  pre-interpolated per-texel surfaces, a field-need mask) and re-implements the per-texel evaluation
  statement-for-statement as the CPU reference twin. `BakeFieldValidation` compares it against
  `RunMeshBake` over all 27 maps × 4 scenarios (Teapot/Cube 256², two-part DirectX/dust 128²,
  query-free 128²): **bit-exact — 100% identical texels, 0 LSB worst channel difference**, with
  guards (≤2 LSB, ≥99.99% within 1) for cross-platform float noise. Evidence retained in
  `Exhibits/TextureStudio/Results/GpuBakeField/` (checks JSON + Teapot legacy/field/difference
  PNGs; difference frames are black). The field route is also faster on CPU (Teapot 256² ≈2.9s → ≈2.0s)
  thanks to index-order bbox UV coverage instead of a per-texel full-triangle scan.
- `Engine/Render/Shader/BakeFieldEvaluation.slang` mirrors the twin in compute (64-wide workgroups,
  manual normalize, identical tolerances/traversal order); compiled to `BakeFieldCompute.spv` via
  slangc or the pinned Slang WASM route (`Automation/CompileBakeField.mjs`, which asserts the
  48-byte push-constant ABI from reflection) and installed beside the other surface archives
  (`Automation/BakeRasterization.cmake`). `Engine/Render/BakeRasterization.h/.cpp` is the
  borrowed-device host in the `VectorCoverageRasterization` idiom: staged device-local BVH,
  262,144-texel banded submission (TDR budget), serial field harvest, and refusal-as-data — any
  failure becomes a `BakeGpuOutcome` the panel turns into a same-settings CPU re-bake, named in the
  completion line. The panel (`UI/TextureStudio/BakingWorkspacePanel.*`) gained device hooks only —
  no Vulkan in the panel; `TextureStudioApplication.cpp` owns the unit behind a fault latch.
- Verified in this sandbox (host-verification CMake/Ninja, null GLFW backends): `BakeFieldValidation`
  and the full `TextureStudio` (`TextureWorkspace`) targets build and link, the shader compiles, and
  the CTest passes. Windows CI gained the `BakeFieldValidation` target/test; it only triggers on
  push to `Eksa`/`arena/db2f9953-frontier`, so this branch has no CI run.
- **Not yet proven:** any GTX/RTX execution — the sandbox has no GPU or Vulkan ICD, so the shader has
  been compiled but never run. First hardware run needs the user's Windows machine: pick "GPU" in
  the Baking inspector; GPU fields may differ from CPU by a few LSB (FMA/libm), which the validation
  thresholds anticipate. Also noted while packaging: `Automation/BuildTextureStudioWindows.py`'s
  hardcoded shader count was already stale (10 surface → now 14) and its render source list predates
  the vector-coverage units; the count is corrected to 18 and `BakeRasterization` added, but that
  zig cross-build remains unexercised here. An RTX ray-query fast path can layer on the same field
  layout later without changing the panel or encoder.

## Baking Workspace UI parity pass against Alloy Studio (2026-10-26, branch arena/4014756b-frontier)

- Audited `UI/TextureStudio/BakingWorkspacePanel.cpp` section-by-section against the Alloy original
  (`Experimental/TextureStudio/src/BakingStudio.jsx`, `baking/MeshMapGrid.jsx`, `studioTheme.css`) using
  Material/Pattern/Stamp as the native reference ports, and fixed the concrete divergences found (UI only):
  dock columns now use the reference 278/820/338-of-1436 grid and a 32px status inset; the assets dock
  gained its missing `.bk-asset-scroll` region so the 27-tile grid scrolls instead of running under the
  pinned footer; pane/section headings and card object-counts are right-aligned on their baselines as the
  CSS flex rows specify; mesh stats regained the Box glyph and locale-grouped counts; import buttons carry
  the Upload glyph; target rows show the bare grouped triangle count at the right edge.
- Restored behaviour the port had dropped: after a bake, tiles show the active set's real map thumbnails
  (`object-fit: contain` on `#080a08`, culled to visible rows) instead of permanent gradient swatches; the
  Map inspector card regained its per-map parameter fields (bevel radius/strength/samples, dust axis/falloff,
  wireframe line width, mask strength — now also covered by settings undo/stale detection and recipe save);
  the 2D preview selectors are fed by baked results only, disabled when empty, and fall back to the active
  set's first map like the web original; the 2D stage respects texel alpha over its checkerboard.
- Locked the workspace like the original while a bake runs (`locked = busy`): recipe actions, mode switch
  (which also forces the 3D view for Material patch/Volume SDF again), presets/tiles, every inspector
  field, and settings undo/redo. The Bake action now only appears in Mesh-maps mode and reads
  "Baking mesh maps…" while busy; no Cancel button was faked (there is no native cancellation channel yet).
  The stale Bake-contract paragraph (it claimed only six maps were implemented) now matches the original's
  copy, the status bar is the 32px dot/ellipsis strip, and errors surface as the top-centre amber toast.
- Verified in this sandbox: `TextureStudio` and `BakingWorkspacePanelConstructionValidation` rebuild and
  link, and the construction validation passes. Still open (unchanged, not UI): mesh import, recipe loading
  (no native file picker), Material patch / Volume SDF native panels, bake cancellation, and any
  GTX/RTX or Windows visual confirmation.

## Texture Paint Workspace native port (2026-10-27, branch arena/4014756b-frontier)

- New `UI/TextureStudio/TexturePaintWorkspacePanel.h/.cpp`: the Alloy `TexturePaintStudio.jsx` composition
  copied 1:1 onto the shared native surfaces (PanelDockspace, VectorAuthoringControlSurface, GlyphSpace,
  the Material/Baking orbit camera) — 43px header with the five-workspace switcher, Layers dock (LEFT,
  278/1436), Teapot viewport dock (CENTER), Inspector/History dock (RIGHT, 338/1436), content-browser
  strip and 31px status bar. The long-disabled "Texture" pills in Material/Pattern/Stamp/Baking are now
  live (`ActivateTexture` wired both ways in `TextureStudioApplication.cpp`, layout persisted as
  `texture_studio_texture_layout`, project JSON/draft in the library directory).
- The layer document is real local state, modelled on `textureDocument.js`/`useTextureDocument.js`:
  Alloy's default three layers (Surface detail/Surface finish/Base material), max 64, add/duplicate/
  delete/reorder/folder-assignment/ungroup, masks (fill/paint/colour/gradient), the 15 canonical channel
  targets (Height auto-enabled by Normal), inherited visibility/locking, collapsed-folder hiding,
  81-entry snapshot history (80 undo steps, delayed commits) and `alloy.texture.v1` JSON save/autosave.
- The viewport reuses the Baking workspace's CPU rasterizer pattern over the same built-in Teapot
  (`BakeMesh.h`): smoothed orbit/pan/zoom, Lit/Wireframe, ground grid, UDIM part targeting (Body/Lid/
  Handle/Spout → 1001–1004 derived from real chart ids), an authoring-UDIM UV view plus read-only Source
  UV0, and digital Paint/Erase/Fill + mask region editing evaluated per-triangle as a preview composite.
- Scope stated plainly (also in the panel header): this is NOT Alloy's WebGL surface compositor. No GPU
  paint slices, no baked/exported PBR maps, no decal/stamp projection engine, no material-snapshot
  shading; content-browser assignment stores an asset link (Alloy's own behaviour), channel sources are
  configuration only, and unported actions (Stamp/Transform placement, project file picker) stay disabled
  or say so in the status line instead of faking results.
- Verified in this sandbox: `TextureStudio` and `TexturePaintWorkspacePanelConstructionValidation` build
  and link; the new validation (default document/history/topology) passes, as do the Baking/Stamp
  construction validations. Full CTest: 46/48 — the two failures are the pre-existing
  `DockCycleRecoveryValidation`/`FrameFaultRecoveryValidation` ImGui fault-injection exhibits
  (FrontierInterface-only, untouched here, aborting on ImGui asserts in this sandbox build config).
  Sandbox has no GPU/display: Windows visual confirmation still pending.

## Texture Paint fixes from first Windows test (2026-10-27, branch arena/4014756b-frontier)

- User-tested `8c01cf4` on Windows: workspace runs, three issue classes fixed here.
  (1) Dear ImGui "SetCursorScreenPos() to extend window/parent boundaries" errors (Layer stack window +
  list child, Inspector card): every terminal cursor jump in the docks now submits a zero-size Dummy
  anchor (layer list, pane footer, Digital-painting card, history rows, browser grid, viewport footer,
  and all new channel-panel rows), so scroll/auto-resize boundaries include the reserved extents.
  (2) Viewport: removed the two radial-gradient discs (clear flat field, as requested) and framed the
  teapot closer (3.1x bounding sphere vs the material capture rig's 5.8/1.35, matching Alloy's Texture
  scene; Baking keeps its own framing).
  (3) Inspector parity audit against `TextureLayerInspector.jsx` / `ChannelPropertyPanel.jsx` /
  `textureStudio.css` / `paintChannelModel.js`: field rows are now label-left + 128px select-right
  (`.tp-field-row`), sliders are Alloy's label+value-capsule row over a 24px pill track (`.tp-amount`),
  card notes are 8px with the top rule (`.tp-card-note`), warm accent corrected to `#a79171`, mask card
  gained its reset button, Alloy's White/Black + "Uniform/Inverted fill" preview wording, the
  "Paint overlay · n records / Clear painted mask overlay" block, "Selected colour" + per-UDIM-part
  effective-coverage swatches (native `ColourMaskPreview`), and "Remove layer mask". Layer thumbnails
  now follow `LayerThumbnail` (material swatch / paint checker + recorded dabs / kind icon) in rows,
  identity and source summary; mask identity icon is 32px with effective fill + colour dot.
- Channel targets rebuilt as Alloy's `ChannelPropertyPanel`: heading with count badge + picker toggle,
  removable channel chips, Clear all, grouped picker rows, then one folded card per enabled channel
  (Alloy's six default-folded channels) with source-mode pills (Value/Texture/Generator/Gradient —
  Gradient Base-color-only), per-channel value/colour defaults and ranges from `paintChannelModel.js`,
  generator choice + its real parameter sliders, "Value swatch · Not composited", and the Normal
  Height-derived note. Settings persist in the document/history/JSON as sparse `channelSettings`.
  Honest scope unchanged: these are stored authoring targets — texture import, source canvas, material-
  studio generator and gradient point editing say "not native yet"; nothing composites PBR maps.
- Verified in this sandbox: `TextureStudio` + TexturePaint/Baking/Stamp construction validations build
  and pass. Windows visual re-check pending (user).




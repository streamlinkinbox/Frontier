# Vulkan preview, materials and illumination integration

## Recommendation

**Implement 1 CAD → 2 MatCap → 3 preview PBR → 4 full materials/shaders, sequentially on shared Vulkan/Slang.**
This sequence is authorized. The user requested MatCap next despite the still-open native CAD host gates.
Do not make CAD depend on a visibility
buffer, PBR materials, ray tracing or ReSTIR. Keep both painting applications and reference collections intact.
Use one shared rendering foundation, not four independent renderer implementations.

Prefer **B's resource/culling structure** for the later illumination port, with selected features/tests from A.
Neither branch establishes a GPU-speed winner, supplies hardware ray tracing, or is a finished production GI solution.

## Current implementation boundary — CAD, MatCap and preview PBR offscreen

- `Engine/Render/SurfaceRasterization` now separates shared device programs, shared resident geometry and independent
  image targets. Viewports/thumbnails reuse the same pipeline pair and uploaded geometry; neither belongs to UI.
- Six `.slang` entries compile through an explicitly supplied portable compiler; reflection checks 112-byte CAD
  and 128-byte MatCap push ABIs and vertex locations; MatCap also checks texture/sampler slots. Both require only
  core SPIR-V Shader capability, not optional draw parameters or wide lines.
- Executed native C++/Vulkan readback on **SwiftShader (CPU), not Windows or hardware GPU**: orthographic/perspective,
  thumbnail, front/rear edge occlusion, near/eye clipping, cancelled upload retry and retained resource lifetimes.
- Fixture is two authored blocks in Exhibits, with explicit edges; it is not a CAD decoder, Boolean union or preset.
- C++ source, executable validation and captured images: `Exhibits/Renderer`. Portable compiler binary and SPIR-V
  outputs stay ignored; provenance/reflection reports and real PNG readbacks are retained.
- Still required for CAD acceptance: existing-device native presenter/image registration; orbit/pan/zoom and window
  resize/DPI; validation layers and Windows/hardware runs; document geometry/identity and thumbnail cache integration;
  shared-edge cleanup, transform/precision coverage and fenced retirement across all presentation readers.
- MatCap now uses the same resident geometry and sized targets, with one shared optional pipeline and immutable
  sampled lookup residency. View-space inverse-transpose normals, texture orientation/filtering, scale/shear/reflection,
  CAD round trips, texture changes, depth, thumbnail reuse and source-scope finalization are pixel-checked.
- The normal-calibration sphere is analytical test geometry, not a CAD document or creation preset. Captures and code
  are in `Exhibits/Renderer`. Native presenter/Windows/validation-layer acceptance remains open for both modes.
- Preview PBR adds an optional fourth shared pipeline, immutable scattering uniform residency and a shared texture
  interface. `ScatteringStratum` names the evaluated angular-scattering unit; `PreviewScattering` orders up to eight.
  Opaque strata mix evaluated GGX/diffuse by coverage; dielectric coatings reflect and attenuate scattering beneath
  using Schlick entrance/exit Fresnel and Beer absorption. This is a shared-normal preview approximation, not the
  full Unreal material implementation, parameter interpolation, or a completed material compiler.
- PBR includes directional illumination, deterministic cosine/GGX mixture quadrature of an analytical sky, planar
  sRGB albedo sampling, exposure, ACES-fit tone mapping and sRGB display encoding. No HDR archive/prefilter,
  authored UVs, normal mapping, refracted transport, multiple scattering or subsurface transport is claimed.
- The earlier HTML browser displays real native offscreen readbacks through `ServeRenderer.py`; one persistent
  Vulkan device retains four programs and each fixture geometry across material/mode requests. Original reference
  samples remain explicitly WebGL. Browser evidence does not close native editor/presenter acceptance gates.
- Authored opaque/coating materials now compile strict TOML to typed mip textures and a 1504-byte immutable uniform (original 1216-byte prefix plus automotive/preview fields).
  `MaterialSurface.slang` adds authored UV/tangent normals, ORM and radiance, using the shared scattering equations.
  The earlier HTML applies/imports/exports authored source and displays native compilation diagnostics/readbacks.
  This is the bounded material slice, not a general shader-expression compiler or every scattering model.
- Authored strata now have independent texture-array slices, UVs and normal strengths, rotated anisotropic GGX
  and optional exact dielectric Fresnel. Environment proposals/PDFs match each directional lobe. These additions
  do not add scene refraction, SSS, complex-IOR conductors or multiple-scattering compensation.
- Native document material assignment/presentation and advanced transport remain pending. No GI/Render settings, native sample presets,
  project-format migrations or painting integration are introduced by this work.

## Pinned references — keep A and B distinct

| Reference | Branch | Pinned commit | Exhibit |
| --- | --- | --- | --- |
| A | `arena/b55d93c0-slate` | `8a7004983b10c079564becb932ae9b444716b9c4` | VehicleProvingGrounds |
| B | `arena/d984ba1b-slate` | `c552cb26bf2be710baaefe64b830fe6db28f7ae7` | IlluminationHost |

A includes `c005f6e0be68254efc6315e53fa8ac5bd7ec8105` and its garden follow-up `8a70049`.
These branches diverged: A is not simply an upgrade over B. The shared native Engine and painting-reference files
have identical Git blob identities between these two pinned trees. Their exhibit code differs.

Preserved under `Experimental/SlateReference/`: both complete renderer exhibits, the complete 30-file Next/React
painting UI app, standalone paint prototypes, native C++ Engine sources and their supporting tools/build recipes.
These are pinned source snapshots, not nested Git repositories or a promise that the imported apps are production-ready.
See `Exhibits/RendererPlanning/Results/SlateSources.json` for all 1,149 source identities and SHA-256 fingerprints.

## Do we need a visibility raster?

**Ordinary visibility/depth rasterization: yes. A full visible-triangle ID buffer: not for the first CAD renderer.**
The latter stores surface/triangle identities and reconstructs shading attributes later. It is distinct from normal
GPU rasterization and from the acceleration structures used by ray tracing.

Legend: effort/cost ✔️ low, 🚩 medium, 🔴 high; suitability has more ⭐ = better for the first CAD milestone.
Costs are engineering expectations, not measured GPU timings.

| Approach | Effort | Extra work/storage | CAD-first suitability | Decision |
| --- | --- | --- | --- | --- |
| Hardware raster, depth + colour, topology-edge overlay | ✔️ | ✔️ | ⭐⭐⭐⭐⭐ | Start here |
| Attribute/G-buffer then deferred shading | 🚩 | 🚩 | ⭐⭐⭐ | Add only for a consumer that needs it |
| Full triangle-ID visibility buffer + attribute reconstruction | 🔴 | 🚩 | ⭐⭐ | Benchmark later, not a prerequisite |

A visibility buffer can reduce repeated expensive material evaluation and attribute bandwidth in suitable scenes.
For a small CAD preview it can instead add attachment traffic, indirection and reconstruction work. An RG32_UINT
identity target alone occupies about 15.8 MiB at 1920 × 1080 before depth, motion, coverage, MSAA or history.
There is no universal efficiency win. Hardware RT also does not require this buffer.

The native Slate visibility implementation is useful reference, not a finished dependency: its fragment shader
writes partition/triangle identities and occupancy; comments explicitly mark motion and cutout handling as absent.
Adopt stable identities and camera-relative precision now; defer its entire visibility machinery until warranted.

## A versus B — source-based comparison

Legend: suitability has more ⭐ = better for the later integration foundation; ranking is architectural, not FPS.

| Concern | A | B | Integration choice |
| --- | --- | --- | --- |
| Shared native C++ engine | Same pinned source | Same pinned source | No engine-level winner |
| Light binning | 16-entry cell lists; counts overflow and drops excess assignments | 32-bit cell masks for 32 lights; outside-grid all-light fallback | B for current bounded scene; scalable lists later |
| Light/bin buffer allocation | 84,016 bytes including light records | 6,176 bytes including light records | B uses less storage here, not proven faster |
| Glass | World-traced refracted leg; one interface, fixed 1.4 m skip | Additional screen-space backdrop/refraction step with fixed 0.5 m offset | Neither is final physical transmission |
| Material records | 48 bytes; dielectric IOR derived from F0 | 64 bytes; explicit IOR with shared stride constant | Explicit semantics; validate one new shared ABI |
| Surface identity | Packed with surface attributes; screen-trace mode suppresses derez glow | Separate identity attachment, with corresponding effect-target fix | Preserve explicit identity, use integer IDs natively |
| Triangle rays | Compute/software two-level hierarchy, reflections toggle | Compute/software two-level hierarchy, reflections toggle | Neither is Vulkan hardware RT |
| ReSTIR GI | Distance-field + surface-card gather/reuse | Distance-field + surface-card gather/reuse | Validate estimator before replacing visibility |
| Timing tools | Detailed copyable snapshot, including zero-duration work | 120-frame configuration matrix with summaries | Combine both; add warm-up and native GPU timestamps |
| Foundation suitability | ⭐⭐⭐ | ⭐⭐⭐⭐ | B structural base; A feature/test donor |

A bounded CPU reproduction with 32 huge-reach lights retained 16 per cell in A and all 32 in B.
A counted 16,384 dropped light/cell assignments in that test. This is not a frame-time benchmark.
B's mask is not a general solution for thousands of lights; it must grow into tested overflow-safe lists or multiple
words before the light count grows. Both approaches build their bins on the CPU in these exhibits.

Do not blindly transplant A's fixed transmission skip, B's screen-space refraction addition, half-float identities,
fixed-size traversal stacks, or unexamined reservoir weights into the production path. Test thin/thick interfaces,
entry/exit intersections, energy conservation, occlusion, capacity exhaustion and identity precision explicitly.

Local replay: **A 64/64, B 46/46 Node tests pass** after restoring the pinned Jolt dependency. Counts describe these
exact trees and commands, not equivalent test coverage. Earlier 58/58 and Dawn-clean statements are historical
source/user reports; Dawn and real-GPU rendering were not rerun for this planning review.

## Shared rendering shape

```text
Editor / Project Browser / standalone painting host
                       |
          shared Vulkan device + resource lifetime
                       |
       persistent geometry + camera + render target
                       |
      +----------------+-----------------+
      |                |                 |
 CAD surface/edges   MatCap          shared material evaluation
                                        |
                               preview PBR / full PBR
                                        |
                           optional ray visibility + ReSTIR GI
```

The primary painting reference is now **TextureStudio / Alloy** from `streamlinkinbox/Frontier`, pinned at
`773b2f5a2672ae530accff000d65d779e9059b66`, preserved in full under `Experimental/TextureStudio`.
The earlier Slate React/HTML painting references were the wrong application; they remain secondary references only.
The standalone painter and the embedded Frontier workspace must use the same document, material evaluation,
texture residency, undo and shader code. Separate application hosts are allowed; a second independent material
implementation or an iframe standing in for native integration is not the goal.

### Common rules

- Engine owns reusable Vulkan rendering, geometry residency, shading and materials. UI owns viewport controls.
- Extend the existing Vulkan presentation at a narrow offscreen-submission/image-registration boundary. Do not
  construct a second Vulkan device merely to show a browser thumbnail, or expose mutable device ownership to UI.
- Keep original geometry uploads resident; update transforms/camera separately. Rebase large translations in host
  precision before narrowing; handle non-uniform normal transforms correctly.
- Render offscreen, synchronize writes to shader sampling, then display the image through the existing ImGui adapter.
  Retire old images/descriptors only after all in-flight work completes. No per-frame device-idle waits.
- Compile `.slang` to SPIR-V. Share geometry/material declarations, validate layout and use reflected bindings.
  Keep authored descriptions in TOML; shader source is source code, not serialized material content.
- CAD, MatCap, preview PBR and full PBR are selectable modes/feature tiers, not copies of geometry or asset loading.
- Dirty thumbnails render on geometry/material/camera/size changes; batch requests and reuse cached GPU images.
- Specify scene-linear versus display-referred colour and perform the output transfer exactly once.
- No GUI GI/renderer controls are introduced into the Control Centre by this plan.

## Dependency order and acceptance gates

| Order | Deliverable | Acceptance before proceeding |
| --- | --- | --- |
| 0 | Rendering contracts + narrow Vulkan host boundary | Geometry/identity/target ownership; shader compilation and reflection; validation enabled |
| 1 | **CAD only** | Real topology edges, hidden-surface depth, orthographic/perspective, capture, resize/DPI and zero validation errors |
| 2 | MatCap | Offscreen core checked; native-host acceptance still open; same geometry/targets and normal-transform contract |
| 3 | Preview PBR | Shared material subset, GGX/direct light/environment, textures and colour management; no mandatory GI |
| 4 | Full materials + shader integration | Explicit closures/features, layering, alpha, transmission policy, shared ABI and safe compile failure |
| 5 | Painting app: standalone and embedded | Real strokes/UV projection, masks/layers, undo, material browser, persistence round trips |
| 6 | Native ray visibility + reference integration | Capability detection, BLAS/TLAS build/refit, hardware-ray and fallback agreement, no leaks |
| 7 | ReSTIR GI + reflection integration | Validated weighting, reuse/rejection, denoising, material edits, lighting changes and reference-image comparisons |
| 8 | Scale/optimization review | Hardware A/B timings justify any visibility buffer, GPU culling or further specialization |

Stages 5 and 6 may proceed independently after the material contracts stabilize. GI must see the same evaluated
materials as rasterization and painting; it must not silently use a separate approximate material identity table.
Do not delay the CAD milestone for the rest of this roadmap.

### CAD milestone — first implementation gate

- Depth-tested opaque surfaces followed by boundary/crease/authored curve edges. Hide tessellation diagonals in CAD
  mode; offer triangle-wire inspection separately later. Never label triangle soup as faithful CAD topology.
- Prefer an explicit topology-edge list and screen-space expanded strokes with antialiased coverage. Do not require
  optional wide-line support, geometry shaders, or `fillModeNonSolid` merely to draw CAD edges.
- Depth bias/edge visibility must match the chosen depth convention; do not copy bias signs blindly with reversed Z.
- Support orbit/pan/zoom, orthographic/perspective, resize and DPI. X-ray/hidden-edge display is an explicit later option.
- Start with a small real source geometry fixture in Exhibits, not a production creation preset. Blank remains the
  only native project preset. Geometry decoding must stay explicit; no fake successful CAD import.
- Check shared-edge deduplication, boundary/crease masks, transformed instances, near-plane clipping, very large/small
  coordinates, thin features, GPU image readback, thumbnail/viewer agreement and resource retirement.
- Record GPU/CPU times at thumbnail and viewport extents plus increasing geometry counts on named hardware.
  Success means correct images and bounded resource use first; no FPS guarantee is made without those measurements.

### Material, shader and painting preparation

The imported C++ project contains real material declarations, layer sequences, processing/dirty keys, image import,
texture upload/export and a Slang thumbnail shader. Inspect/reuse those seams rather than porting the whole editor.
That thumbnail shader is a simplified sphere/highlight calculation, not evidence of a complete production PBR path.
Preview and full PBR should share core BRDFs and material meaning while differing in enabled effects/sample budgets.

TextureStudio is the correct app: real texture layers, digital painting, packed UVs, masks, materials/patterns/stamps
and baking workspaces. Its existing project export is `.texture.json`, not an established `.pigment` painting format.
Its original compiled self-contained app is served on port 8774; local smoke checks cover the real teapot and
Lit/Wireframe switching. Native Vulkan conversion and full functionality validation are still future work.

The previously selected Slate React app contains layer/inspector UI and some simulated painting operations. `PaintWorkshop.html` contains
actual Three.js/WebGL/Canvas painting interactions and depends on a remote Three.js module. Neither is already
Frontier's native Vulkan painting application, and copying its source does not complete its integration.

The reference `.pigment` codec is a typed binary fixed-dielectric declaration: name, base colour, roughness, IOR and
one dielectric boolean. It explicitly excludes image/layer processing. It cannot currently save a complete painting
session. Before implementation, define versioned authored material/layer/texture references and recovery/undo rules,
using the project's approved TOML authoring policy. Reconcile the existing binary `.pigment` through an explicit
compatibility decision, not a silent overwrite or a new format invented while writing a browser.

Standalone and embedded hosts must round-trip the same content; shader/material edits invalidate the correct atlas
regions and GI history. Thumbnails use the same material evaluation but must not require full-scene ReSTIR to load.

### ReSTIR and ray tracing preparation

Use the source exhibits as behavioural references, not prevalidated native hardware-RT code. Port WGSL algorithms
into the shared Slang/Vulkan implementation. Start with a deterministic direct/path reference before temporal reuse.
Then validate candidate PDFs and reservoir weights, temporal reprojection, disocclusion/material rejection, spatial
reuse visibility, denoising and reset conditions against that reference. A reservoir implementation is not made
unbiased or production-ready merely by retaining the ReSTIR name.

Use Vulkan acceleration structures and ray queries where supported for the first hardware-RT visibility route;
consider a full RT pipeline only when shader needs justify it. Keep capability-negotiated alternatives and explicit
unsupported diagnostics for non-RT devices. CAD and MatCap remain operational without RT extensions.
Screen-space tracing remains optional assistance, not a substitute for off-screen geometry or correct world tracing.
Surface-card/distance-field approximations remain deliberate quality choices, not assumptions inherited by every mode.

## Evidence and boundaries

- Pinned sources: `Exhibits/RendererPlanning/Results/SlateSources.json`.
- Local Node results: `ReferenceChecks.json`, `A-Tests.tap`, `B-Tests.tap` in that Results directory.
- Reproducible light-bin comparison: `Exhibits/RendererPlanning/Code/CompareReferences.mjs` and its JSON result.
- No new native renderer, shader, GUI renderer controls or format migration was implemented in this planning turn.
- No hardware timing, Windows/Vulkan execution, Dawn rerun, native scene capture, commit, push or Actions is claimed.

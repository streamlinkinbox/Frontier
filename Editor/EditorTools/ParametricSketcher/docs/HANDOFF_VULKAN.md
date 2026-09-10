# Vulkan hand-off — from the RasterExchange seam to a real GPU raster

This document is the contract for the next `VulkanRaster` that will live next to
`SoftwareRaster` and implement the same `RasterExchange` interface. The
SoftwareRaster already speaks Vulkan shape — it consumes typed vertex streams,
a per-draw record, a single `ViewRecord`, draws lattice / surface / segment /
point, switches to a depth-test-off overlay, and readbacks an `RGBA8` plus
pick image. The job of the GPU port is to translate each of those verbs into
one command buffer, with the buffers, descriptors, push constants and
subpasses described below.

The goal is **1:1 parity with the SoftwareRaster proofs**. The same `Scripts/`
files run end-to-end against either raster, byte-equal for the geometry stage
and within dither noise for the matcap studios. Proofs that the GPU path has
to reproduce out of the box:

- `Proofs/Proof_02*`  (lattice, primitives, sketch, matcap, plastic scenes)
- `Proofs/Phase7*`    (profile algebra, areas, fill, holes)
- `Proofs/Phase8*`    (loft / sweep / pipe / patch)
- `Proofs/Phase9*`    (boolean booleans — trimmed curved faces, pick identities)
- `Proofs/Phase9b_*`  (FairPatch — translucent sheets over a body)

---

## 1. The verbs the seam already speaks

```
BeginTarget(ClearRGBA)            // one render pass
BindView(ViewRecord)              // all 16-byte push-constant / dynamic-state
DrawLattice()                     // background grid + axes
DrawSurface(SurfaceStream, DrawRecord)
DrawSegments(SegmentStream, DrawRecord)
DrawPoints(PointStream, DrawRecord)
BeginOverlay()                    // depth test OFF, depth write OFF, straight alpha
EndTarget()
Readback() -> RasterImage         // RGBA8, top row first
Pick(X, Y) -> uint32              // PickIdentity, 0 = nothing
Depth(X, Y) -> float              // 0..1 clip depth
QueryTally() -> Tally             // tri/seg/pt/frag + culled (regression alarm)
```

That is the public surface. Everything above it (`ScenePresentation`,
`TransformGizmo`, console `render`, the gizmo overlay segment) speaks only
that vocabulary. Nothing else is allowed to leak below.

---

## 2. Buffers, descriptors, and pipeline layout

| Object | What it is | Sharing | Notes |
|---|---|---|---|
| `UBO_Frame`        | 16 + 16 + 16 + 16 + 16 + 16 + 4 + 4 + 4 + 4 = 256 B | one per `BeginTarget` | maps `ViewRecord` byte-for-byte; `BindView` updates a dynamic UBO or push constant |
| `SSBO_Draw`        | 96 B per draw record | one per draw, dynamic | `DrawRecord` is uploaded as a single UBO at `vkCmdBindDescriptorSets` time, so `DrawSurface`/`DrawSegments`/`DrawPoints` only set one descriptor and call `vkCmdDraw*` |
| `SSBO_Stream`      | per-draw vertex stream, float32 packed | one per draw, dynamic | `vkCmdBindVertexBuffers` rebinds every draw — the streams are tiny (KB range) so the cost is negligible against the draw call |
| `SSBO_Matcap`      | 10 studios × 256×256 RGBA8 | static, immutable | exactly matches `MatcapStudio.slang`; bind once at startup |
| `Attachment_Color` | `RGBA8` swapchain or offscreen | per `BeginTarget` | also written by overlay; the overlay re-uses the colour, switches the depth attachment to read-only |
| `Attachment_Pick`  | `R32_UINT` | per `BeginTarget` | written only by `DrawSurface` / `DrawSegments` / `DrawPoints` (when `DrawRecord::PickIdentity != 0`); the readback path uses `vkCmdCopyImageToBuffer` on this attachment only |
| `Attachment_Depth` | `D32_SFLOAT` | per `BeginTarget` | depth-tested, depth-written in the main subpass; read-only (no write) in the overlay subpass |

Three subpasses within one render pass:

1. **Lattice** — `DrawLattice()` only, write colour, no depth.
2. **Opaque** — `DrawSurface` (matcap or plastic) + `DrawSegments` + `DrawPoints`,
   write colour, write depth, **write the pick attachment** (a separate
   fragment shader output bound to `Attachment_Pick`). Translucent draws
   (`Tint[3] < 1`) are deferred to the overlay.
3. **Overlay** — gizmo, translucent fills, selection halos, HUD. Depth test
   on (so the gizmo occludes behind solids), depth write **off**, pick
   **off** (the gizmo is not pickable through its overlay).

Why three subpasses and not three render passes: a single
`vkCmdPipelineBarrier` between them, no resolve cost on the colour, and the
depth attachment stays a single allocation the whole frame.

---

## 3. Pipelines (one per `RasterExchange` verb)

| Pipeline | Topology | Cull | Depth | Blend | Outputs | VS source | FS source |
|---|---|---|---|---|---|---|---|
| `Lattice`        | triangle list | none | off, no write | off | colour | `Lattice.slang` | `Lattice.slang` |
| `Surface`        | triangle list | back, CCW | on, write | off (opaque pass), straight-alpha (overlay pass) | colour + pick | `Surface.slang` | `Surface.slang` |
| `Line`           | triangle list (segments expanded to quads by VS) | none | on, write | off | colour + pick | `Line.slang` | `Line.slang` |
| `Point`          | triangle list (points expanded to quads by VS) | none | on, write | off | colour + pick | `Point.slang` | `Point.slang` |

The vertex shaders are the same `.slang` files the SoftwareRaster already
mirrors in C++. They are translated to SPIR-V by the Slang compiler
(`slangc -target spirv -profile spirv1.5`) and shipped as four `.spv` files
alongside the engine; no shader recompile at startup.

Push constants:

- `pc_frame` : `ViewRecord` (256 B) — the only thing `BindView` changes.
- `pc_draw`  : `DrawRecord` (96 B)  — set per draw, never read by the VS.

---

## 4. Pick identity

`PickIdentity` is a `uint32` written by every fragment that emits a non-zero
value. The SoftwareRaster allocates a separate `R32_UINT` tile the same size
as the colour tile and writes it through the same fragment stage; the GPU
path uses the dual-source / `SV_Target1` mechanism: the fragment shader has
two outputs, the first bound to the colour attachment, the second to a
dedicated pick attachment.

Two key invariants:

- A translucent fragment (alpha ≤ 0.5) **does not write to the pick
  attachment**. This matches `SoftwareRaster`'s depth+pick rule and means the
  gizmo overlay (and translucent fills like `Phase7b_Areas` and `Phase9b_*`)
  never steal pick from the body underneath.
- A pickable surface (`PickIdentity != 0`) and a non-pickable helper
  (`PickIdentity == 0`, e.g. lattice, gizmo) live in the same draw call;
  the only difference is the fragment shader's `if (Draw.PickIdentity != 0)
  output2 = Draw.PickIdentity;`.

`Pick(X, Y)` becomes `vkCmdCopyImageToBuffer` on a 1×1 region of
`Attachment_Pick` — the depth image stays untouched. The same `RasterImage`
readback path is used for the colour.

---

## 5. Matcap studios

`MatcapStudio.slang` indexes into a 10-layer 256×256 RGBA8 atlas. The atlas
is built once at startup from the ten hand-painted studios (steel, chrome,
gold, copper, plastic-white, plastic-red, plastic-blue, clay, pearl, carbon)
and bound as a combined image sampler with `clamp-to-edge`, `linear`
filtering. The fragment shader takes `DrawRecord::Matcap` as the layer
selector, samples `uv = matcap(0.5 * normal.xy + 0.5)`, multiplies by
`DrawRecord::Tint.rgb`, and writes the colour attachment. The
`PickIdentity` output is unchanged.

Plastic and flat shading are two of the same pipeline, branched on
`DrawRecord::Shading` (0 flat, 1 plastic, 2 matcap) — a single
`if/else` in the fragment shader. The cost difference is negligible; the
artistic difference is decisive.

---

## 6. What has to change in the SoftwareRaster for parity

Two known drift points that any port has to keep aligned:

1. **Top-left fill rule and supersampling** — the SoftwareRaster uses 4-tap
   supersampled coverage for the analytic matcap shaders; a naïve port that
   just does `VK_POLYGON_MODE_FILL` with `MSAA x4` gives visibly different
   edges on long thin triangles. Use `sampleShading` enabled with `minSample
   Shading = 1.0`, or accept 4×MSAA and live with the memory cost.

2. **Pick write gating** — translucent fragments must skip the pick
   attachment write. The SoftwareRaster gates on `alpha > 0.05`; the GPU
   port needs the same branch in the fragment shader, and the overlay
   subpass must not bind the pick attachment as an output at all (it would
   otherwise get an `image layout` validation error).

If a VulkanValidationLayer error fires on the first frame, it is almost
certainly one of those two. The SoftwareRaster's `QueryTally` also reports
`BackFacing` and `DepthRejected` — the GPU port should match those counters
or the regression alarm trips.

---

## 7. The minimum viable port

Order of work to reach parity:

1. **Skeleton** — one pipeline per verb, single subpass, no overlay, no pick
   attachment. Render `Proof_02a_Lattice.png` and confirm it matches.
2. **Three subpasses + overlay** — render `Proof_02b_Primitives.png` and
   confirm the gizmo overlay is the same.
3. **Pick attachment** — render `Proof_04a_BoxSelection.png` and confirm
   `pick` returns the same identity as the SoftwareRaster.
4. **Matcap studios** — load the 10 studios, render
   `Proof_02j_MatcapScene.png`; the per-figure tinting has to match.
5. **Translucent fills + skip-pick** — render `Phase7b_Areas_Iso.png` and
   confirm `pick` over a translucent area returns the curve underneath, not
   the fill.
6. **Trimmed curved-face tessellation** — Phase 9 booleans produce
   trimmed faces; the raster needs to clip the surface lattice by the trim
   loops, the same `PlanarCells` walk the kernel already does. Render
   `Phase9_Booleans_Iso.png`; the lens and the tee must look the same.
7. **Readback path** — `vkCmdCopyImageToBuffer` on the colour and the pick
   attachments, blit to the linear-tiled image, map, copy, hand back as a
   `RasterImage`. The SoftwareRaster's own write path becomes a fallback
   for headless CI.

Estimated lines of new code: **~1,500** for the VulkanRaster plus the four
`.spv` outputs. The `.slang` sources already exist; the only edit is gating
the pick output on alpha > 0.05 in `Surface.slang`.

---

## 8. What we do not need

- **No descriptor indexing beyond the two UBOs and the matcap SSBO.** The
  per-draw record is a push constant, not a descriptor; per-draw vertex
  streams are dynamic vertex buffers, not SSBOs. The total live descriptor
  count is well below 16.
- **No compute shaders.** Everything in the renderer is a forward
  raster pass; the SoftwareRaster's own `frag` counter is the only thing
  that looks compute-shaped and it is a `uint` increment in the fragment
  shader.
- **No tessellation.** The NURBS surfaces are pre-tessellated on the CPU
  into `SurfaceStream` triangles; the GPU port never has to deal with a
  parametric surface. The kernel's `TessellateFace` for trimmed curved
  faces is what feeds the stream.
- **No ray tracing.** Phase 9's booleans are already exact; the GPU port
  is not asked to do hardware ray casts. `RT cores` are free for
  future ReSTIR-style work.
- **No mesh shaders.** Meshlet binning is a software-raster concern, not
  a GPU one. The GPU consumes the already-binned `SurfaceStream`.

---

## 9. Acceptance

A `ctest` run that today is 13 suites (and after Phase 10, 15) must be the
same against either raster, with the only difference being render time.
A `VulkanRaster` build target should be opt-in (`cmake
-DSOLIDARC_VULKAN=ON`) and the headless SoftwareRaster path remains the
default for CI. The `RendersEqual` check in `SuiteVerification` is the
regression net: same scene, same view, both rasters, same PNG hash to within
a 1-LSB-per-channel tolerance (dither noise is allowed; geometry is not).

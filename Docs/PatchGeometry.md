# Patch Geometry — native preview, first implementation

## Status and limits

This is **native C++ / Vulkan shader integration**, not another HTML-only demo. It is deliberately a **debug-preview tier**, not production shaded LOD yet.

- Quick tile: **Patches: Off → Patch Tiles → Tiles + Wireframe → Off**. Off restores the diagnostic inspector's selected view rather than overwriting it. The new views are also named in F3/configuration.
- Both Vulkan cull phases select actual alternate index ranges. The vertex shader runs the same selector and forwards the matching primitive base to the fragment shader. Surface resolve finds the owning patch for alternate primitive tokens. Colors remain stable when a patch changes detail; wire width is computed analytically in pixels (no compute-shader derivatives).
- Normal shaded rendering, ray traversal, shadow draws and luminaire sampling retain the original triangles. **No native GPU execution or speedup is claimed.**
- One coarse alternative per existing 128-triangle patch. No hierarchy, streaming, mesh shaders, geomorphing, hysteresis, displacement subdivision or dynamic/deforming-mesh rebake support yet.

### Separate opaque and transparent policies

**Opaque:** in patch preview, conservatively confirmed back-facing patches select the coarse alternative. Other patches must satisfy a one-pixel projected displacement bound. A near-plane guard retains fine geometry. Nonuniform, mirrored or sheared transforms do not take the backface shortcut; projected error uses a conservative Frobenius scale bound.

**Transparent:** transmission, alpha flags, opacity below one, thin surfaces, subsurface, uncertain transmission/subsurface/opacity textures, layered materials and emitters stay full-detail. Missing slab bindings also fail closed. Classification reads the current material/slab buffers, not a baked opaque/glass label. Material Apply now re-finalises and uploads the scene at an idle authoring boundary, so the visibility guard and shading receive the committed values; this is intentionally not an optimized incremental upload.

Glass therefore keeps its original shell and entry/exit geometry. This does **not** invent extra source detail or guarantee that a low-resolution source sphere is smooth in extreme close-ups. A higher-detail transparent tessellation policy is still future work.

Production shaded LOD remains disabled because an approximate raster surface combined with the original ray surface can self-intersect, self-shadow or disagree at reflection/refraction origins. That correspondence must be solved before enabling these alternatives in the lighting path. The existing renderer invalidates lighting history when leaving debug rendering.

## Bake and runtime load

`SceneStructure::RegisterInstance` Morton-orders fine triangles as before, then bakes alternatives with `PatchGeometry.h`:

1. Lock all patch boundaries and non-manifold edges. Never move a shared boundary or generate replacement vertex attributes.
2. Consider short interior endpoint collapses with similar normals, nearby UV coordinates and matching tangent handedness.
3. Require the edge link condition, retained triangle orientation/area and no duplicate faces. Stop when no safe collapse remains or the roughly half-size target is reached.
4. Accumulate an object-space endpoint displacement bound. It is conservative geometry error, not a shading/normal-map error bound.
5. Append alternate indices **after all fine indices of the owning instance**. Fine counts and ranges are unchanged. Public triangle counts sum the fine instance counts, not the enlarged index buffer.

Instances now reserve half the 14-bit primitive namespace: at most **8,192 fine triangles**, with room for alternatives below token 16,384. The runtime CPU/GPU cluster record is **64 bytes**, with alternate index/count/primitive/error fields at offsets 48/52/56/60. Full rebuild of the native binary **and shaders** is required. Both CMake and PowerShell track the new shader includes.

### `.pgeom` cache v1

Default location: `.frontier/cache/patch-geometry-v1/` relative to the application working directory. Set `FRONTIER_PATCH_CACHE` to redirect it. This directory is ignored by Git.

First registration bakes synchronously; subsequent registrations load the derived cache. No separately installed offline baker is required for this tier. Large first-load scenes may pause; this is not an asynchronous production content pipeline.

Files contain explicit little-endian words, not native C++ structs:

| Offset | Field |
|---|---|
| 0 | `FPG1` magic |
| 4 | version, currently 1 |
| 8 | 64-bit source/algorithm key |
| 16 | alternate index count |
| 20 | IEEE float error bits |
| 24 | index payload, followed by 64-bit checksum |

The source key includes ordered fine indices and referenced vertex position/normal/UV/tangent data. Readers bound allocation by the source patch, check version/key, finite nonnegative error, index membership, checksum and end-of-file. Corrupt/truncated/stale entries rebake; unwritable caches do not prevent source loading. Writers use a temporary file and rename. Hashes detect accidental corruption; this is a trusted local derived cache, not an authenticated interchange format. Algorithm changes require a version/key bump.

**Existing Space `CLST` stays 48 bytes per row.** The exporter writes only the frozen original prefix; the Space proof reads that prefix and zero-initialises the runtime tail. Alternatives belong in the versioned derived cache, not a silent change to old Space containers.

## Primitive coverage

Every registered indexed mesh enters the common patch path. Native Construct sphere and torus now use shared interior vertices and proper parametric UVs, with duplicated UV seams retained. Their former triangle-local UVs made every edge a boundary and prevented safe simplification. New Construct UV layout therefore changes; it does not rewrite imported assets.

Measured by the **actual native Construct/SceneStructure CPU code**, with the shared selector at a distant camera:

| Mesh | Fine triangles | Coarse preview triangles | Patches |
|---|---:|---:|---:|
| Sphere | 960 | 550 | 8 |
| Torus | 768 | 458 | 6 |
| Cube | 12 | 12 | 1 |
| Cylinder | 128 | 128 | 1 |
| Cone | 64 | 64 | 1 |
| Plane | 2 | 2 | 1 |
| Area emitter | 2 | 2 | 1 |
| Indexed disk fixture | 64 | 64 | 1 |

Retaining fine geometry is intentional when only silhouette/seam-constrained edges remain. This tier does not yet provide useful reduction on the triangle-soup cylinder/cone. Analytic lamps are not tessellated by this system; mesh emitters are protected, while lamp housing meshes use the ordinary mesh path. The disk fixture verifies preservation through the common path, not a new editor Disk button.

## CPU mirror and evidence

`PatchReference.h` mirrors the shader's transform/cone calculations. `PatchPolicy.shared.h` is compiled by **both C++ and GLSL**, including the material and scalar projected-error policy. The gate constructs real native primitives, registers a 16,928-triangle grid to exercise splitting, changes live material records, corrupts cache files and checks actual baked ranges.

```sh
SANITIZE=1 PATCH_EVIDENCE=Docs/PatchGeometryEvidence bash Tools/Build/CheckPatchGeometry.sh
PATH="$PWD/.cache/glslang-build/StandAlone:$PATH" bash Tools/Build/CheckShaders.sh
bash Tools/Build/CheckPerformanceTelemetry.sh
```

Results in this environment:

- **73,537 CPU assertions passed under ASan/UBSan**: boundaries, manifold edges, duplicate faces, attributes/index membership, cache cold/warm/corruption, projected policy, material edits, fine ray counts and 14-bit addressing.
- **22/22 shaders lowered to SPIR-V.** Six cluster-consuming modules independently inspected for 64-byte array stride and member offsets.
- **8 native translation units syntax-checked against real headers**, plus warning-clean checks of the bake, primitive builder and gate.
- Existing native Construct test: **nine entities, 1,936 original triangles**, placements/camera/emission/normals checks passed.
- Space family: **19 claims passed, 0 failed, 2 GPU-image claims skipped**; old CLST round trip preserved.
- Existing performance telemetry gate passed; CMake/PowerShell shader-table parity passed.

[Fine native sphere CPU projection](PatchGeometryEvidence/native-sphere-fine.svg) · [Coarse native sphere CPU projection](PatchGeometryEvidence/native-sphere-coarse.svg)

These are perspective painter's reference SVGs of real selected native indices, with the shader's patch color hash. The distant result is magnified to make topology readable. They are **not Vulkan captures, a full CPU rasterizer, refraction renders or timing evidence**. Raw CPU/compiler summaries are in `PatchGeometryEvidence/`.

## Required hardware follow-up

Rebuild on the Vulkan workstation:

```powershell
.\Projects\Project-Zero\Build\ToolchainSequence.ps1 -Rebuild
```

Then check the three quick-tile states, near/far camera sweeps, patch colors across transitions, wireframe alignment with AA, both cull phases/HiZ, transformed meshes, opaque-to-glass/opacity/texture/layer edits, and return to shaded rendering with clean history. Validate descriptors and synchronization with Vulkan validation enabled. Compare actual indirect triangle counts and timings; these CPU results do not establish either GPU correctness or performance.

Next tier: primary/ray surface correspondence, a transparent shape-error policy, useful cylinder/cone hierarchy, LOD transition stabilization, asynchronous/offline bake tooling, and production hardware measurement.

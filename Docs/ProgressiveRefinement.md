# Continued refinement and Patch Geometry topology

## Rendering

The 256-frame notification never stopped sampling: the integrator increments beyond
it and `ResolveSurface` continues updating the unfiltered mean. The message now says
**Initial accumulation ready — refinement continues**, rather than “Baking complete”.

Variance edge stops alone did not guarantee the five-stage spatial filter would
become an identity operation. Every stage now fades its contribution using the
**current pixel's valid sample count**: full strength through 33 samples, then
`33 / count`. This is a presentation policy, not a measured convergence criterion.
33 corresponds to the reprojection bound of 32 previous samples plus one new sample.
Newly exposed/reset pixels retain strong filtering even after a long stationary hold.
Long holds progressively reveal more of the raw mean, including its remaining noise;
this does not manufacture detail, increase render resolution, or remove depth of field.

The fade blends each stage's input with its filtered result. It does not blend from
clean history RGB (which would lose the weather composite). Variance blending uses
a conservative correlated-estimate bound. Existing history allocation supplies the
new read-only binding 4; descriptor layout, pool, per-level writes and compute
write-to-read barriers are updated together. Raw accumulation remains unfiltered.

## Patch Geometry

The scene exporter writes three vertices per triangle. Previously, registration
passed those distinct indices directly to the patch baker, making shared geometric
edges look like locked boundaries. A soup sphere reproduces zero simplification
before adjacency restoration.

Registration now canonicalizes indices only when all twelve vertex attribute
components match exactly (position, normal, tangent including handedness, UV).
No tolerance or position-only welding is used. Padding is ignored, mesh registrations
remain separate, original vertex storage is retained, and every fine triangle keeps
its full original attributes. Canonical indices become the bake/cache input, so old
soup cache entries do not mask the change; no cache deletion is necessary.

Patch tiles retain stable colors/IDs. **Tiles + Wireframe** reveals triangle changes
inside them. This is still conservative **preview-only, two-level** selection, not
an adaptive hierarchy that continuously splits colored tiles. Protected materials
(glass, emission, layers, etc.) and production/secondary-ray fine geometry remain
unchanged. Small meshes and heavily seamed patches may have no safe reduction.

## Validation

- `SANITIZE=1 bash Tools/Build/CheckProgressiveDenoise.sh`: runs the actual denoise
  shader through the existing mechanical C++ port. A fixed high-variance detail
  fixture's five-level mean absolute bias decreases from 0.0098315 (33 samples) to
  0.0012631 (256), 0.0006314 (512), 0.0000395 (8192). Tests per-pixel reset, input
  composite retention, disabled identity, finite variance, and actual integrator
  progression through 8192 frames. Synthetic CPU evidence, not a rendered scene.
- `SANITIZE=1 bash Tools/Build/CheckPatchGeometry.sh`: soup sphere selects **960 near
  / 562 far triangles**, while production retains 960. Existing boundary, cache,
  material protection, instance splitting and ray-range tests remain covered.
  Far test distance is 1000 m, near 1.7 m; not a claim about an ordinary dolly distance.
- `python3 Tools/Tests/TestDenoiseSafety.py`: descriptor/barrier/source guards and
  existing history snapshot, telemetry, dither and concurrency-model checks.
- Existing material denoise CPU suite: 97/97 (default young-history fixture).
- Actual `SwapchainExchange.cpp` passes C++20 syntax checking with dependency headers.

No Windows/Vulkan runtime or GPU image validation was available. SPIR-V compiler
installation/download was blocked by network access; shader execution above is the
C++ port, not SPIR-V. Rebuild the executable **and shaders together** before testing.
At native/100% resolution, hold a still camera past the initial accumulation message;
then move it to check young history. In Tiles + Wireframe, compare an opaque smooth
mesh near/far; expect stable patch colors but fewer internal triangles at sufficient
distance. Transparent/protected objects should not lose their fine shell.

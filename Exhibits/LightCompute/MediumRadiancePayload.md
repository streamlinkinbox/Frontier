# Shared cloud and fog shader inputs

`Frontier/Engine/Rendering/MediumRadiancePayload.{h,cpp}` now owns the typed cloud and height-fog
input compilers and bounded layout validators for `LightInfluence.slang`. The LightCompute exhibit
uses these functions in place of its local packers and duplicate validation. Existing CPU cloud and
fog models and the shader were already Engine-owned; this extraction does not by itself integrate
them into the editor or provide a Vulkan backend.

The cloud ABI is seven float4 groups; the two seed slots preserve arbitrary 64-bit patterns via
`memcpy`, including patterns that look like float NaNs. The fog ABI is four float4 groups. Fog
`Colour` is deliberately not packed: the shader uses a separate incident ambient radiance.
Float32 encoding and the bounded fields are checked before submission. Finite/field bounds do not
prove that all arithmetic for arbitrary world positions remains finite.

## Evidence

- [Release](Results/MediumRadiance-release.txt) and [ASan/UBSan](Results/MediumRadiance-sanitized.txt):
  64 cloud cases, exact arbitrary seed bits, fog constants/switches and 37 malformed-input guards.
  The actual builders extracted from commit `fe774a5` and the Engine builders produce the same
  7,232 bytes, SHA-256 `34dbcaa684698d67e610fd5d7422ced903e0501d9a36aa8bd0831901e887d090`.
- [Vulkan release](Results/MediumRadianceGpu-release.txt) and
  [sanitized host](Results/MediumRadianceGpu-sanitized.txt): a small sky-only fixture executes the
  actual cloud and fog Slang shaders using the new Engine payloads on **SwiftShader CPU Vulkan**.
  For 24 homogeneous cloud-cube texels and the upward camera ray, the maximum closed-form
  Beer–Lambert/ambient comparison error is `6.83e-7`; fog-only error is `7.49e-9` and combined
  cloud/fog error `1.67e-7`. Clear sky and two malformed-payload rejection controls pass.
  Only the native host is sanitizer-instrumented, not the external driver or shaders.

This focused fixture uses uniform cloud density, homogeneous fog, ambient illumination and no
geometry or direct lights. It does **not** test cloud noise/advection, geometry or cloud shadowing,
facet lighting, textured atmosphere, real sky calibration, physical GPUs or editor integration.
The existing full-size `--fog-checks` was stopped after an extended software-Vulkan run and is not
claimed as a post-extraction pass. Earlier evidence remains in its own exhibit logs.

## Reproduce

Requires CMake, a C++17 compiler and Git history containing `fe774a5`; the Vulkan check additionally
requires the prepared Slang compiler, Vulkan headers and SwiftShader. The scripts default to the
repository's `_ClaudeScratch/venv` and `ExternalPackages/VulkanCompute` where appropriate.

```sh
Exhibits/LightCompute/RunMediumRadianceValidation.sh
SANITIZE=1 Exhibits/LightCompute/RunMediumRadianceValidation.sh
Exhibits/LightCompute/RunMediumRadianceGpu.sh
SANITIZE=1 BUILD_JOBS=1 Exhibits/LightCompute/RunMediumRadianceGpu.sh
```

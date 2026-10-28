# Shared radiant-facet distribution

The existing LightCompute surface and fog paths used an exhibit-local area/luminance CDF builder and repeated
host-side validation. They now consume `Frontier/Engine/Rendering/RadiantFacetDistribution.{h,cpp}` through the
`FrontierRadiantFacetDistribution` C++17 library. The exhibit retains archive offsets, fixture construction,
independent integration and Vulkan submission; it does not maintain a second distribution builder.

| Check | Expected | Observed |
|---|---|---|
| Analytic triangles | Three sources, geometric area ratio 1:2 and one tick for a much dimmer facet | Passed; dark and degenerate facets excluded |
| Prior builder parity | Exactly the same float32 payload on 4,096 deterministic facets | 3,396 sources; all 54,340 bytes equal to the actual `fe774a5` exhibit builder |
| Invalid inputs | Reject malformed distributions, nonfinite/negative radiance and unrepresentable source area | 18 rejection checks passed |
| Fog-area integration | Existing SwiftShader fixture agrees with independent CPU and rectangular solid angle | Four 64×40 cases: max channel difference `1.32422e-5`; analytic rectangle relative error `0.000445949` |
| Toggles and guards | Cold history, surface isolation, zero density, empty CDF and invalid parameters | Passed in the same Vulkan run |

[Release](Results/RadiantFacet-release.txt), [ASan/UBSan](Results/RadiantFacet-sanitized.txt),
[SwiftShader](Results/RadiantFacetGpu-release.txt). Sanitizers instrument the CPU host, not the Vulkan driver.
The GPU run used the existing generated `VehicleLightInput.bin`; the reproduction below generates that fixture
from the retained source. The comparison is against a numerical model, not a measured light field.

## Contract and limits

The payload is a count followed by `(facet index, probability, area, cumulative probability)` float32 entries.
The original area × RGB-luminance weighting and 24-bit tick allocation are unchanged for valid inputs; each
nondegenerate luminous triangle retains at least one tick. This protects support but perturbs very small ideal
probabilities. The shader treats facets as two-sided. The Engine now rejects nonfinite coordinates, negative or
nonfinite RGB, unrepresentable source areas and malformed payload spans before dispatch. Validation checks the
encoded distribution, not whether it still describes a later geometry/radiance revision; a caller must rebuild it
when those inputs change.

This is a **global** distribution for the existing one-bounce/fog shader, not clustered emissive lighting.
The replay experiment uses its separate uniform facet-index list and is not changed by this extraction. Neither
path is yet submitted by the production editor; no hardware-GPU or real-time claim follows.

## Reproduce

Requires Git history containing `fe774a5`, CMake ≥3.20, GCC/Clang, Python, Node, pinned Slang and a Vulkan
loader/device. The GPU runner builds the 13 existing shader entries and regenerates the fixture; on the tested
SwiftShader device the fog-area regression took several minutes. Set `PREPARE` using the repository's existing
Vulkan tool preparation if `ExternalPackages/VulkanCompute` has not been restored.

```sh
bash Exhibits/LightCompute/RunRadiantFacetValidation.sh
SANITIZE=1 bash Exhibits/LightCompute/RunRadiantFacetValidation.sh
bash Exhibits/LightCompute/RunRadiantFacetGpu.sh
```

The CPU runner extracts the prior implementation from Git into ignored scratch, compiles it against the same
fixed fixture and compares the actual binary output; it does not keep a separately edited copy in the repository.

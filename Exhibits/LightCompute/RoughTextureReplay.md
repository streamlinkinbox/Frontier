# Guarded rough, textured path replay

The isolated replay host now admits a bounded **single-stratum rough GGX + albedo texture** combination that it
previously rejected. The existing shader evaluates the atlas material on primary and secondary hits. It retraces
reused seeds through current geometry; this is still an experimental SwiftShader Vulkan path, not editor-integrated
or complete ReSTIR GI.

## Domain and model

- Explicit opt-in to both rough and textured replay; one opaque, full-coverage stratum, roughness `0.25–1`,
  metallic `0–1`, IOR `1–3`, and a validated power-of-two RGB albedo pyramid. The diffuse-only mode still
  rejects rough or metallic atlas coefficients.
- No analytic lights, normal maps, sheen, absorption, coatings, paint, refraction or layered materials. Very smooth
  or delta lobes are rejected. Sampling defaults to cosine weighting; a later [optional GGX mixture](ReplayImportance.md)
  can improve some cases but can increase variance on others.
- Authored atlas scattering and the compact rough model use **different diffuse Fresnel terms**. The CPU oracle
  evaluates the authored entrance/exit term for atlas hits, rather than silently substituting the compact model.
- Texture footprints use the existing camera-distance heuristic, not ray differentials. Mip/address/UV, material
  and BVH guards apply; atlas changes cap represented history at one for the transition. The host's historical
  `TexturedDiffuse` flag now selects the guarded texture atlas in either diffuse or rough mode.

## Evidence

The three four-hit room rows below use temporal and spatial reuse after the stated scene change. Values after
`±` are standard errors; primary position and albedo values are maximum absolute errors.

| Check                           | Result                                                                       |
| ------------------------------- | ---------------------------------------------------------------------------- |
| Guards                          | 33 malformed, 4 rough bounds, opt-in and diffuse rejection of rough atlas    |
| Authored GGX quadrature — green | CPU `0.368590`; Vulkan `0.364369 ± 0.010936` batch SE                        |
| Wrong compact BRDF — green      | `0.467336`; differs from authored by `0.098747`                              |
| Primary intersections           | 3,840 hits, 1,172 textured; position `3.82e-6`, albedo `6.56e-7`, params `0` |
| Four-hit atlas change — red     | CPU `0.690925 ± 0.003286`; Vulkan `0.691711 ± 0.000891`                      |
| Four-hit blocker — red          | CPU `0.021418 ± 0.000315`; Vulkan `0.022149 ± 0.000340`                      |
| Four-hit reopening — red        | CPU `0.687114 ± 0.003267`; Vulkan `0.690276 ± 0.001680`                      |
| Secondary-texture control — red | CPU `0.000450 ± 0.000058`; Vulkan `0.000274 ± 0.000058`                      |
| Primary floor probe — green     | CPU `0.316783 ± 0.002250`; Vulkan `0.308789 ± 0.016810`                      |
| Dynamic history                 | 40,960 temporal accepts each; 97,280 spatial links; transition cap `1`       |

The quadrature uses converged `128×256` vs `256×512` double-precision hemisphere samples, with 128 reset Vulkan
batches of 16 probes. Four-hit room baselines use 400,000 independent `mt19937_64` CPU paths per state, brute-force
triangle intersections and an independently implemented bilinear/mip sampler; each Vulkan comparison has eight
reset batches of 1,024 centre pixels. Room GPU error bars above use the larger of pixel SE and sample batch SE.
The paired control changes only the texture to a dark constant, using 120,000 CPU pairs and 24 Vulkan batches.
The two primary-pixel references each use 240,000 CPU paths and 128 reset Vulkan batches. Room and primary RGB
comparisons pass `6 × combined SE + 1% of CPU mean + 1e-4`; finite GPU seed independence is not established.

The primary probes here are static; a [separate small-motion test](RoughTextureMotion.md) checks two moving
rough-textured primary pixels. All image pixels, broader scene scales, near-delta convergence, hardware GPUs,
performance and the editor remain unverified. The dynamic atlas and blocker checks here use a manual untextured
receiver, with rough-textured secondary hits. The CPU references check an analytic model, not observed materials.

```bash
bash Exhibits/LightCompute/RunRoughTextureReplay.sh
SANITIZE=1 bash Exhibits/LightCompute/RunRoughTextureReplay.sh
```

[Release results](Results/RoughTextureReplay-release.txt) · [Sanitized host](Results/RoughTextureReplay-sanitized.txt)
· [Replay regressions](Results/RoughTextureReplay-regression.txt) · [Diffuse texture audit](ReplayTexture.md)
· [Bounded replay](PathReplay.md) · [Motion and blocker renders](RoughTextureReplayCaptures.md).

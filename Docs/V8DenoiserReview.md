# V8 denoiser review

> Follow-through: [Denoiser safety fixes and GPU measurement workflow](DenoiserSafetyFollowup.md). The quantization and in-place history findings below describe the reviewed baseline; subsequent code fixes and remaining hardware validation are recorded there.

Reviewed engine revision: `00c380db5e6392f2618c90e18a21bb86071ece4f`.

Scope: source inspection of the production accumulation shader, a-trous shader, Vulkan descriptors/barriers/dispatch, fidelity tiers, glint material hook, and CPU proof. No renderer changes. No GPU execution or timing measurement in this review. The earlier 97/97 gate result is a CPU/shader-mirror result, not a concurrency or GPU-image guarantee. The quoted DenoiserLab PSNR/firefly metrics were not independently rerun for this report.

## Executive recommendation

Keep V8 as the comparison baseline, but prioritize history correctness and demodulation validation before increasing filtering. Add denoise-off dither for presentation consistency. Treat sigma tuning, fifth-level filtering and filtered-history feedback as measured experiments, not free upgrades.

## Findings requiring attention

### 1. In-place reprojected history has a GPU read/write hazard — high priority

`Engine/Shaders/ReSTIRViewport.slang:1226–1242` loads previous surface, moments and color at `prevPx`. Lines 1306–1311 write this frame's surface, moments and color at `pixel`, through the same image bindings (3, 18, 19).

`Engine/DeviceExchange/SwapchainExchange.cpp:1938–1948` binds one image view for each history. The pre-dispatch barriers at approximately 3068–3107 order frames, but cannot prevent an invocation from overwriting a texel that another invocation in the SAME dispatch still needs as previous-frame history. Cross-workgroup execution order is unspecified. This can also mix surface/moments/color from different logical frames.

This is a pre-existing source-level hazard, not introduced by V8. Its actual visible severity has not been measured. Same-pixel stationary accumulation does not have the cross-pixel dependency; motion reprojection does.

Recommendation: immutable previous-frame history plus separate current-frame writes (ping-pong or an explicitly staged snapshot). Audit descriptor reuse and frames-in-flight as part of that change. Existing bindings are constrained, so this needs a resource-layout plan, not an extra barrier inside the shader. Add motion/disocclusion tests and GPU validation. Memory/bandwidth costs are real.

### 2. Albedo floor breaks exact RGBA8 round-trip — confirmed arithmetic issue

`ReSTIRViewport.slang:1342` quantizes albedo, THEN clamps it to 0.02. The denominator is therefore 0.02 for dark channels, but the RGBA8 OutputImage stores the nearest representable value, 5/255 = 0.019607843. The filter reloads that value at its final write (`AtrousDenoise.slang:305`).

Even with no spatial filtering, the affected channel's linear-radiance factor is:

`(5/255) / 0.02 = 0.9803921569`

That is approximately 1.96% attenuation before tone mapping. Dither does not repair it. The shader's “exact inverse” claim is incorrect at the floor.

Recommendation: clamp to the intended bounded range BEFORE quantization and use that exact quantized value for both division and storage. Alternatively choose a representable floor (e.g. 6/255 if the minimum must stay at least 0.02). Add black/near-black/saturated-channel tests including actual UNORM conversion. Validate the upper range too: RGBA8 cannot preserve values above one.

### 3. Whole-radiance demodulation is not universally irradiance

The `mean` being divided includes diffuse AND specular lighting, lens contribution, and weather composited at lines 1313–1325. Dividing all of these by primary base color is not a physical diffuse-irradiance decomposition.

Without filtering, matched division/multiplication can cancel. With filtering, different pixels' denominators mix, so specular reflections, colored metals, fog over texture and lens highlights may be distorted or become harder to filter. A constant dark denominator also changes the log-luminance weights and early-out behavior: it is not generally a no-op.

The scalar variance conversion `variance / Luminance(albedo)^2` is exact for scalar/gray scaling, not general RGB component-wise division. Accurate colored variance would need more information (RGB moments/covariance or an explicitly validated approximation).

Recommendation: test metals, near-black paint, saturated textures, rough/smooth reflections, fog over checkerboards and flare over geometry. Consider unit-denominator fallbacks for problematic classes as a low-complexity experiment. Longer-term, diffuse/specular separation and a separate deterministic-weather composition stage are cleaner, but add resources/passes and need careful integration.

The ordinary M9 mirror explicitly parks UNIT albedo (`Exhibits/Workbench/Materials/AtrousDenoiseMirror.cpp:89–92`); its pass cannot certify these colored/quantized-albedo cases. The separate source-repository lab provides complementary evidence, not a substitute for those integration tests.

## Assessment of the proposed changes

| Proposal | Verdict | Cost and caveat |
|---|---|---|
| Dither with denoiser OFF | Yes, useful consistency fix. `ResolveSurface` currently writes undithered `ToneMap(mean)` at line 1350. | Small hash/arithmetic cost; not a noise-removal optimization. Reuse the same bounded hash and apply once at presentation. Audit alternate output paths too. |
| Change sigma schedule index 3 from 1 to 1.5 | Worth an A/B option, not an automatic fix. Current schedule is `{4,4,2,1,1}`. | Same dispatch/tap structure; may smooth residual hot pixels AND desired glints/edges. Test real content and motion; lab residual ratios are not GPU predictions. |
| Five levels on Minimal | Do not enable by default without timing. Minimal/Economy/Standard currently use four; Ultra/Reference five. | Another dispatch, barriers, reads/writes and filter work. 4→5 is 25% more dispatches, NOT a claim of 25% more GPU time. Allocated descriptors do not make execution free. Texture is not immune to arbitrary filtering. |
| Motion-gated filtered-history feedback | Potentially valuable, but defer until immutable history exists. | Current history stores clean pre-weather radiance; filter output is post-weather and demodulated. A direct copy would mix incompatible representations, bake weather into history and leave moments inconsistent. Need separate histories or an explicit conversion/estimator design, plus multi-frame testing. |
| Increase candidates/resolution for sparkles | Useful diagnostic and quality lever, but not free or conclusive. | Minimal is 0.5× each dimension and one DI candidate. Full resolution means 4× primary pixels. More light candidates do not automatically supersample a subpixel flake normal or geometry. |
| Dark-metal demodulation is harmless/unbiased | Reject as a blanket claim. | Quantization issue above; whole-radiance filtering, nonlinear weights and variance approximation still change behavior even if the unfiltered arithmetic cancels. |

### History/motion qualifications

`prevPx != pixel` is an integer-coordinate test, not a complete motion classifier. Subpixel motion can map to the same texel. Lighting, reflections and material appearance can change without corresponding geometric reprojection displacement. Some scene edits reset accumulation in `GameExecution.cpp`, but this does not establish validity for every dynamic signal. Review temporal confidence and reset policy before adding feedback.

### Firefly/glint qualifications

The current clamp requires `TapColour.a > 0.0225 * TapLum^2` (`AtrousDenoise.slang:260`). Variance is an estimate, not proof: insufficient samples, correlated samples or propagated variance can make a bad sample appear reliable. Conversely, moving legitimate highlights can have high variance. “Fireflies always have high variance” is too strong.

A synthetic test where the old clamp leaves isolated stable points intact only rules out that clamp for that test. It does NOT rule out luminance weighting, reconstruction, temporal averaging or sampling as production glint-loss mechanisms.

The primary material hook at `ReSTIRViewport.slang:409–421` still uses a procedural normal perturbation; that call does not consume the available ray-cone footprint. Footprint-aware unresolved flake statistics/LOD is a more targeted long-term fix than blindly increasing DI candidates. `Docs/AutomotiveFlakesNextPass.md` is marked historical/superseded; use the production hook as the authority for this path.

## Additional optimization candidates

1. **Measure per denoise level and count early-outs.** `GpuPostMs` currently combines denoise and luminance work (`VisibilityExchange.h:101`); it is not a dedicated denoiser timer. Compare controlled camera paths, fixed resolution/exposure and repeated GPU runs.
2. **Early-level shared-memory tiling.** Start with step-1 neighborhoods where overlap is strongest. Validate bitwise/tolerance equivalence and measure occupancy/barrier trade-offs. An 8×8 group needs a 12×12 tile for a step-1 5×5 stencil, but 72×72 at step 16; one tiling strategy is not efficient for every level.
3. **Avoid unused final ping-pong writes where proven safe.** The final level writes its target and presentation; audit every consumer before removing the redundant target write. This is a concrete bandwidth-saving candidate, not a verified optimization.
4. **Adaptive late-level work.** Reuse convergence/variance information to skip work in suitable regions. Existing per-pixel early-outs already exist; tile-level compaction/dispatch skipping adds overhead and requires conservative correctness rules.
5. **Exposure/material-aware convergence tests.** V8 still uses the historical fixed linear-variance threshold. Its display-space error claim needs revalidation after demodulation, across colored albedos and exposure. Do not simply increase the threshold.
6. **Footprint-aware glint LOD.** Preserve stable unresolved sparkle statistics instead of treating every subpixel highlight as more denoising noise. This is a separate material/sampling effort.

“+8% loads” refers to four added depth-neighbor reads relative to the main loop's maximum 50 reads; it is not 8% measured bandwidth or frame time. There are also prefilter reads, stores, early-outs and caches. Likewise V4 evaluates logarithms inside the tap loop: ALU-only is not synonymous with zero cost.

For reference, four chained 5-tap-per-axis levels at steps 1/2/4/8 have a maximum theoretical support width of 61 pixels; five at 1/2/4/8/16 have 125. These are support bounds, not uniform blur windows. The earlier 81×81 statement does not match this chain.

## Recommended order

1. Fix the representable albedo floor and extend quantized/colored-albedo regression tests.
2. Fix in-place history reprojection before adding any filtered-history feedback.
3. Add denoise-off dither and consistent A/B capture tooling.
4. Obtain real GPU images and per-level timings; test metals, fog/flare, glints and subpixel camera motion.
5. Try same-pass sigma variants; pursue tiling/adaptive work only against measured bottlenecks.
6. Only then evaluate fifth-level Minimal filtering or a separate diffuse/specular/history architecture.

No renderer changes were made by this review.

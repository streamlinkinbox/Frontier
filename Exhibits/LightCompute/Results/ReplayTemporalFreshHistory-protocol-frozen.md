# Raw-seed temporal history under changing visibility

## Protocol fixed before measurement

The previous [moving-receiver holdout](ReplayFreshDonorMotion.md) failed the lower-floor temporal-only metal
mean, even though the spatial candidate did not alter that mode. This study diagnoses the retained-history
weight and evaluates a **test-only** alternative: persist the pixel's first raw fresh seed for one submission,
with normalization and represented count both one. The current receiver retraces that seed. This replaces,
rather than extends, selected-reservoir history; it discards older temporal proposals and is not an Engine port.

| Concern       | Fixed protocol                                                               |
| ------------- | ---------------------------------------------------------------------------- |
| Scenes        | Mixed dim/metal and same-metal visibility checkerboards under one absorber. |
| Trajectories  | Four supplied-receiver poses, +0.2 x per step; stationary final-pose control. |
| History       | Direct recorder; changed-scene cap 1 for motion, normal cap 64 when stationary. |
| Arms          | Instrumented original versus test-only raw-seed history, serial Vulkan runs. |
| Modes         | Fresh1, Fresh2, Fresh4, temporal1, temporal4; spatial reuse and seed bank off. |
| Floors        | Existing default 0.01 and lower bound 0.0001.                                |
| Cohorts       | 48 paired 32 × 32 images per trajectory/scene; 512 pixels per group.         |
| Seeds         | MT19937-64 base `0xa92f10d4b731 + scenario_index * 0x1000193`; four per cohort. |
| Reference     | Independent final-pose hemisphere minus blocked-square area integration.    |
| Mean gate     | Every arm/group/mode/floor within `5 * nominal cohort SE + .001`; finite pixels. |
| Comparisons   | Same-seed candidate minus original cohort MSE; signed paired 3-SE plus both mean gates. |
| Controls      | Fresh-mode hashes match; original diagnostic shader matches prior uninstrumented hashes. |
| Diagnostics   | Record prior normalization, replayed radiance/target and previous selected target. |
| Work / tails  | Final and warm rays, triangle tests, conditional means and maximum samples.   |
| Promotion     | No automatic promotion or change to Engine defaults or history ABI.          |

All arms share an Engine-derived Generate shader with an extra write to an unused, noncompact audit slab;
the candidate changes only what Resolve persists in temporal slot 0. Primary and Generate binaries are
identical across arms. The first eight old-seed cohorts in each moving scene reproduce Fresh1/Fresh4 and
original temporal1 group hashes from the prior uninstrumented log. Candidate fresh hashes must match those
same controls. Fresh2 additionally provides a two-path final-submission comparison with no warm-up.

For temporal1, the GPU diagnostic exposes the selected history's source normalization and its replayed
radiance/target. The host independently checks that every output equals one of the two allowed weighted
reservoir selections, and computes their conditional mean. For the original arm only, the prior selected
target identifies almost-dark historical winners that became visible. The candidate must persist exactly the
first raw seed with unit normalization/count, and both arms must reject altered motion guides. A finite
enumeration separately illustrates rare positive-target weights and the effect of clipping; it does not assert
that the GPU's observed failure has the same cause.

Reference quadrature uses 1,024 steps plus a 512-step convergence check. Nominal errors can miss rare tails;
multiple gates are not multiplicity-adjusted. Operation counts are not elapsed GPU time. These are supplied
primary receivers, one bounce, artificial materials and a static square, not a real camera, textured scene,
production resource path, physical GPU, Windows or editor. The completion-gated C++ sequence currently resets
changed receiver records; this direct-recorder stress test deliberately continues them.

## Outcome

Pending execution.

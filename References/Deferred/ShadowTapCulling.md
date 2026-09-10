══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Shadow tap contribution culling — measured, and deliberately not taken
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

STATUS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Written, instrumented, measured, **reverted**. This is a decision, not an interim state. The code that produced
every number below is `References/Deferred/ShadowCullCost.cpp.groundwork`, kept next to this note.

The idea was round 10 backlog item #3: the shadow lookup is the most expensive thing in the shading loop, so
before running it, bound how much the tap could *possibly* contribute and skip the kernel when that ceiling is
too small to matter. It is a standard optimisation and it is genuinely cheap to test. It does not pay here, and
the reason it does not pay is the interesting part.


WHAT THE LOOKUP COSTS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Per surface-tap pair the shadow lookup is up to `kBlockerTaps² + TapCount²` texel fetches — **25 + 81 = 106 at
Reference** — and it runs for every tap that survives the two free cosine rejects (`NdotL <= 0`, `LdotL <= 0`),
however little that tap can contribute. So the ceiling test guards a real cost.

The bound used was deliberately an over-estimate, so the cull could never darken the image by construction:

    ceiling = max_c(Tap.Le[c]) · NdotL · LdotL · Tap.Weight / d²

with `LitFrac ∈ [0,1]` assumed to be 1 and specular excluded. A tap whose ceiling is below the threshold cannot
reach the threshold no matter what the shadow map says.


THE MEASUREMENT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Counters live inside `VisibilityRaster` (`TexelFetches` is the count that dominates cost); sheets come from the
same renderer `CheckShadowTiers.sh` already gates. Deviation is 8-bit, against the unculled render **at the same
tier**, so tier differences cannot be mistaken for cull damage. Rig: the tier proof's own — floor, 1.2 m emitter
overhead, two plates — lifted verbatim so the two harnesses describe the same scene.

First pass, the thresholds one would actually consider shipping (0, 1e-5, 1e-4, 5e-4, 1e-3, 5e-3, 1e-2):
**every tier, every threshold, zero taps culled, zero fetches saved.** Not "small gains" — exactly nothing.

That is the shape of a bug, not a result, so the next step was not to tune the thresholds but to ask what the
distribution actually looks like. Instrumenting the ceiling at every tap evaluation:

| percentile | contribution ceiling |
|---|---|
| p0 (dimmest tap in the frame) | 0.141087 |
| p25 | 0.291739 |
| p50 | 0.367819 |
| p95 | 0.582578 |
| p100 | 2.38273 |

The dimmest tap in the entire frame has a ceiling of **0.141**, which is over 14× the largest threshold tested.
Nothing was culled because nothing *could* be culled: the whole distribution sits far above the range where a
contribution cull operates.

Against the tone map — Reinhard `x/(1+x)` then gamma 1/2.2 — a pixel first rounds up to 1/255 at a raw radiance
of **1.105e-6**. The dimmest tap in the frame is therefore about **127,700× brighter than the visibility floor**.
Every tap that survives the two cosine tests is visible in the output. There is no dim tail to cull.


CONFIRMING THE MECHANISM RATHER THAN TRUSTING THE ZERO
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
A row of zeroes can equally mean "nothing to cull" or "the cull is wired up wrong and never fires". Those were
separated by re-running with thresholds pushed into the measured distribution (0.15 … 3.0). The cull fires, scales
monotonically, and damages the image exactly as expected — Reference tier:

| threshold | fetches | saved | taps culled | worst Δ | mean Δ |
|---|---|---|---|---|---|
| 0     | 26,687,031 | 0.0% | 0 | 0 | 0.0000 |
| 0.15  | 26,680,141 | 0.0% | 65 | **18** | 0.0070 |
| 0.25  | 23,540,017 | 11.8% | 30,401 | 71 | 5.2270 |
| 0.4   | 10,949,313 | 59.0% | 149,979 | 90 | 35.3915 |
| 0.6   | 510,004 | 98.1% | 248,933 | 110 | 67.9742 |
| 1.0   | 362,826 | 98.6% | 250,404 | 113 | 68.7282 |
| 3.0   | 0 | 100.0% | 254,400 | 159 | 70.7214 |

The mechanism is correct, so the zeroes are a real negative result. And the table gives the verdict directly: the
very first threshold that removes *any* work at all (0.15, saving 65 taps out of 254,400 — 0.0%) already moves a
pixel by **18/255**. There is no free setting, and no cheap one. The curve goes from "no effect" to "visibly
wrong" without passing through "worth it". The same pattern holds at all five tiers; Minimal/Economy/Standard
differ only in fetch counts, with identical cull counts and near-identical deviations.


WHY, AND THE SECOND REASON THIS WAS DROPPED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
A contribution cull pays when a shading point sees **many lights, most of them dim** — the cull skips the long
tail. This renderer's shadow path places at most `kShadowMaximumTaps = 4` taps on **one** bright luminaire. Every
tap is a significant fraction of the pixel's light by construction. There is no tail, so there is nothing to cull.
The optimisation is not wrong; the scene is the wrong shape for it, and the cost model says so before any
threshold tuning is warranted.

The second reason is more decisive, and worth stating plainly. `VisibilityRaster` is the **CPU reference harness**
— it is not in the shipping application. The app's shadows run on the GPU through `VisibilityExchange` /
`SwapchainExchange`; the only references to `VisibilityRaster` outside its own translation unit are comments
asserting the two paths agree, plus the `.vert`/`.frag` SPIR-V that share its name. So even a *free* cull here
would have optimised a measurement tool and left every shipping frame untouched. Speed work belongs on the GPU
path, where the shadow passes still have no timestamps — that measurement should come before any further
optimisation.


IF THIS IS EVER REVIVED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
It becomes worth re-measuring only if the shading loop starts handling many-light scenes with a genuine dim tail.
In that case the harness re-runs as-is once the reverted accessors are restored:

    g++ -std=c++20 -O2 -msse4.2 -DFRONTIER_DEVELOPMENT -I . -I Engine -I Engine/GeometricRaster -I Scratchpad \
        -I /tmp/vkh/include -I /tmp/tinybvh \
        References/Deferred/ShadowCullCost.cpp.groundwork \
        Engine/GeometricRaster/VisibilityRaster.cpp Engine/GeometricRaster/SceneStructure.cpp \
        Engine/GeometricRaster/GeometryStructure.cpp Engine/GeometricRaster/TraversalIndex.cpp \
        Engine/DisplayPresentation/FidelityClassifier.cpp Engine/ContentInterchange/MaterialIndex.cpp \
        Engine/DeviceExchange/OrientationClassifier.cpp -o /tmp/shadowcull

Two lessons worth keeping regardless of the outcome:

1. **Measure the distribution before tuning the threshold.** Seven thresholds were chosen by intuition and all
   seven were three to four orders of magnitude below the dimmest value in the data. One histogram would have
   answered the question before any of them were run.
2. **A row of zeroes has to be falsified.** "Nothing was culled" and "the cull never fires" produce identical
   output. Pushing the threshold until the image visibly breaks is what turns the first reading into evidence.

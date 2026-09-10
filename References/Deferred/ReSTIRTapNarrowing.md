══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  ReSTIR spatial-tap narrowing — measured, and declined
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

STATUS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Investigated, measured, **not implemented**. The measurement harness is committed as
`Scratchpad/ReSTIRTapRejectRate.cpp` because the number it produces is useful regardless of this decision.

The idea: the spatial reuse loop (`ReSTIRViewport.slang:972`) loads a whole 64-byte `GpuReservoir` per tap, but
the validation test immediately after it only reads `Counts.x`, `Normal` and `UvDepth.w` — about 20 bytes. If most
taps fail validation, the rest of that load is wasted bandwidth, and narrowing it to a validate-then-fetch would
pay. That is a real pattern and worth checking.


THE MEASUREMENT THAT SETTLES IT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The whole idea rests on the rejection rate, and nothing in the tree measured it. `ReSTIRTapCost.cpp` deliberately
models the worst case — "temporal valid, all spatial taps valid" — which is right for a budget but says nothing
about how often the early-out would fire.

`Scratchpad/ReSTIRTapRejectRate.cpp` applies the kernel's exact validation rule (`ReSTIRViewport.slang:963-978`:
`M > 0`, stride match, `dot(N,Nn) > cos 25°`, `|Δt|/t < 10%`) to a real rendered depth/normal buffer — a Cornell
box with an occluder block, cast analytically so the harness needs no engine internals — using the kernel's own
tap placement (`PcgHash` seeding, rotated cross, 4–16 px radius scaled by width).

At 1280×960, 876,096 shaded pixels (71.3% coverage):

| taps | tested | rejected | rejected % | cause breakdown |
|---|---|---|---|---|
| 1 | 876,096 | 37,961 | **4.3%** | offscreen 0.0% · no-surface 1.3% · normal 2.6% · depth 0.4% |
| 2 | 1,752,192 | 75,996 | **4.3%** | offscreen 0.0% · no-surface 1.3% · normal 2.6% · depth 0.4% |
| 3 | 2,628,288 | 113,915 | **4.3%** | offscreen 0.0% · no-surface 1.3% · normal 2.6% · depth 0.4% |
| 4 | 3,504,384 | 151,861 | **4.3%** | offscreen 0.0% · no-surface 1.3% · normal 2.6% · depth 0.4% |

**Only 4.3% of taps are rejected**, so 95.7% need the full payload anyway. A validate-then-fetch split would
narrow the load for one tap in twenty-three and add an extra dependent memory round-trip to the other twenty-two
— on most hardware that is a net loss, and at best it is noise. The optimisation is declined.

The rate is stable at 4.2% when measured at 320×240 as well, so it is not an artefact of resolution: the tap
radius scales with width, so the tap samples the same neighbourhood in world terms either way.

The cause breakdown explains why the rate is low and is the part that generalises. Rejections are an
**edge-density** phenomenon: normal mismatch (2.6%) and no-surface (1.3%) dominate, and both only happen near a
crease or a silhouette. An interior of large flat walls has little edge per pixel. **So 4.3% is a lower bound for
simple interiors, not a universal constant** — a scene with foliage or thin geometry would reject far more, and if
the renderer's target content ever looks like that, this is worth re-measuring before dismissing again.


WHAT ELSE WAS CHECKED, AND FOUND ALREADY OPTIMAL
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Looking for headroom across the kernel rather than only at the tap loop:

  • **Redundant `PHatFull` at line 898.** Initially looked like the same dead-evaluation bug that R10 #5 removed —
    the winner's p̂ is known when it wins the reservoir, so re-deriving it should be unnecessary. It is not a bug:
    it is the *establishing* evaluation for `pSelected`, and every later stage (temporal merge line 925, spatial
    merge line 982) already carries that value forward instead of recomputing. `CheckReSTIRSelection.sh` guards the
    invariant, and reports the win already banked: **3,052,454 → 1,150,818 target-function evaluations, 62.3%
    fewer**. The carry is complete; there is nothing left to take here.
  • **Shadow rays.** One `TraceShadow` per pixel (line 1005), on the final selection only — not per candidate.
    That is the correct ReSTIR structure and already minimal.
  • **Early-outs.** The kernel returns immediately on a missed primary (`kVisibilityInvalid`) and on directly-hit
    emitters, before any material resolve or resampling.
  • **`PHatFull` ladder** (per pixel, from `ReSTIRTapCost.cpp` in its `cached` mode): Minimal 3 · Economy 6 ·
    Standard 10 · Ultra 16 · Reference 26. The counts follow the tier ladder as designed.

The honest summary is that the ReSTIR kernel has already had this class of attention, and the obvious remaining
candidate does not survive measurement.


WHAT WAS NOT MEASURED, AND WHY
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The narrowing question is fundamentally about **memory**: cache lines, coalescing, bandwidth. A microbenchmark was
written (`/tmp/restir/tapload.comp.glsl`, both variants, compiling and ready) and deliberately **not run for
timings**, because the only device available is SwiftShader, which executes workgroups serially on 2 CPU cores at
a flat ~0.14 µs each and goes through the CPU cache hierarchy. A microsecond figure from it would describe this
machine's L2, not a GPU's memory system, and quoting it would be exactly the error
`References/SoftwareVulkanDevice.md` warns against. The rejection rate, by contrast, is pure geometry and is
device-independent — which is why that is the number this note rests on.

Also unmeasured: whether the 64-byte load is even the shape the driver emits. glslang's output is unoptimised
(1,055 `OpVariable`, 2,186 `OpLoad` for this kernel) and the real narrowing decision is made by the driver's
compiler, not visible in the SPIR-V. That is another reason a static argument here would not have been sound.

The standing conclusion from the shadow work applies unchanged: **the ReSTIR kernel has no GPU timestamps**, and
until it does, every performance statement about it — including this one — is a work count rather than a
millisecond. Instrumenting the passes remains the highest-value performance task, because it is the thing that
converts all of this into real numbers the moment the engine runs on hardware.

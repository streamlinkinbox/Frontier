══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  GPU timestamps for the shadow and ReSTIR stages
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

Every performance statement in rounds 9 and 10 was a work count — PHatFull evaluations, texel fetches, tap
counts — because nothing on the shipping path was timed. This adds the instrumentation that turns those counts
into milliseconds the moment the engine runs on real hardware.

Guarded by `Scratchpad/CheckGpuTimestamps.sh`. The arithmetic was verified on a device (SwiftShader — timing
numbers from it are meaningless, but the *mechanism* is real Vulkan and that is what was checked).


WHAT WAS ALREADY THERE, AND WHAT WAS WRONG WITH IT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
`VisibilityExchange` already owned a 12-query-per-slot timestamp pool covering cull, raster, HiZ and resolve. The
last span, queries 10→11, was reported as `KernelMilliseconds`.

That span is written at the very end of the frame's compute work, so it never meant "the kernel". It meant
**everything after the resolve**, and by round 10 that was three different things:

  • the ReSTIR dispatch (GI on),
  • the whole GI-off shadow stage — map rasterisation plus `ShadowResolve` (GI off, where **ReSTIR is never
    dispatched at all**),
  • the à-trous denoise and the luminance reduction, in either mode.

So with GI off the diagnostic overlay would report several milliseconds of "kernel" for a frame in which the
ReSTIR kernel did not run. Not a crash, not an obviously bogus number — a plausible one, attributed to the wrong
stage, which is the kind of measurement that sends optimisation work in the wrong direction. Item #3's whole
conclusion ("optimise the GPU path, not the CPU reference") depends on being able to tell these apart.


THE SCHEME
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Pool grown from 12 to 16 queries per slot. Two new pairs, each bracketing exactly one stage:

| queries | span | written in |
|---|---|---|
| 12 → 13 | the GI-off shadow stage | `VisibilityExchange::RecordShadowFrame` |
| 14 → 15 | the ReSTIR dispatch alone | `RecordRestirBegin/End`, called around the dispatch in `SwapchainExchange` |

Query 12 is deliberately **not** at the top of `RecordShadowFrame`: everything above it is one-off resource
creation (maps, framebuffers, descriptor refresh) that only runs when the map size changes, and timing that would
report a resolution change as a slow frame.

The reader then attributes exactly:

    Shadow   = Ms(12,13)
    Restir   = Ms(14,15)
    Trailing = Ms(10,11)
    Kernel   = Restir > 0 ? Restir : (Trailing - Shadow)
    Post     = Trailing - (Restir > 0 ? Restir : Shadow)      // denoise + luminance, reported honestly

`PostMilliseconds` exists because the alternative was to keep quietly folding denoise and luminance into whichever
stage happened to be adjacent. Naming it is what makes the other two figures trustworthy.


THE TRAP: AN UNWRITTEN QUERY IS NOT ZERO
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Exactly one of the two new pairs is written on any given frame — GI off writes 12/13, GI on writes 14/15. A query
that is reset but never written returns **undefined** data, not zero. Reading it the old way would have subtracted
garbage from the kernel figure on every frame, which is a worse bug than the one being fixed.

So the read now passes `VK_QUERY_RESULT_WITH_AVAILABILITY_BIT` with a two-word stride, and `Ms()` returns 0 unless
*both* endpoints report available. Every telemetry field goes through `Ms()`; the gate checks that none reads
`Stamps[]` directly and bypasses the guard.

Verified on a device, two frames, one per mode:

    FRAME A — GI OFF: shadow runs, ReSTIR does not
      availability: q12 1  q13 1  q14 0  q15 0
      trailing 0.755 ms | shadow 0.566 | restir 0.000  =>  kernel 0.189  post 0.189
      shadow + restir + post = 0.755  vs trailing 0.755   (delta 0.000)

    FRAME B — GI ON: ReSTIR runs, shadow does not
      availability: q12 0  q13 0  q14 1  q15 1
      trailing 1.578 ms | shadow 0.000 | restir 1.355  =>  kernel 1.355  post 0.223
      shadow + restir + post = 1.578  vs trailing 1.578   (delta 0.000)

The availability words are correct in both directions, and the parts reconcile to the whole exactly.


THE OVERLAY
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
`DiagnosticInspector`'s gpu row now reads:

    gpu   cull 0.42 · raster 1.10 · HiZ 0.08 · resolve 0.31 · shadow 0.57 · restir – · post 0.19 ms

A stage that did not run prints as an en dash rather than `0.00`, because a zero reads as "this stage is free"
rather than "this stage did not run" — and exactly one of shadow/restir is absent in any given mode. The row
buffer was widened 128 → 160 B: the worst realistic row is 128 B, which would have silently truncated.


WHAT THIS STILL DOES NOT GIVE YOU
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
**No real numbers yet.** The only device available here is a CPU implementation whose timings describe this
machine, not a GPU (see `References/SoftwareVulkanDevice.md`). What has been verified is that the queries are
written in the right places, that the subtraction reconciles, and that a skipped stage cannot poison the result.
The first hardware run is what produces figures worth acting on.

When that run happens, the numbers to look for are the ones rounds 9 and 10 could only express as work counts:

  • the PHatFull ladder (3 · 6 · 10 · 16 · 26 per pixel across the tiers) against measured `restir` time,
  • the shadow tier ladder (hard → PCF → PCSS, 256 → 2048 maps) against measured `shadow` time,
  • whether `post` (denoise + luminance) is large enough to deserve its own optimisation attention.

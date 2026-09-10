══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  GPU shadow pipeline — first real execution
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

Round 10 shipped the GI-off GPU shadow path verified only by reading: layouts, matrices and dispatch logic were
checked by eye, and the commit said plainly that a first device run might still surface something. This is that
run. The engine's own shaders and the engine's own matrix were executed on a software Vulkan 1.3 device
(SwiftShader — see References/SoftwareVulkanDevice.md for how it is built and why it is correctness-only).

**Result: the shadow path works, and the round-10 PCSS fix is confirmed by direct measurement.** No defect was
found in engine code. Three defects were found in the harnesses written to test it, and one in the emulator.

Nothing here is wired into a gate. The toolchain lives in /tmp and takes ~45 min to rebuild, so this is recorded
as evidence and as a starting point, not as a check that runs in CI.


WHAT WAS EXECUTED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
| file | what it does |
|---|---|
| `myglslc.cpp` | a stand-in for `glslc`. The repo's shaders are GLSL-in-`.slang` (`#version 460`, `#include`), and no `glslc` is reachable here; this resolves the includes and drives the glslang bundled in the `slangpy` PyPI wheel. Compiles all three shadow shaders unmodified. |
| `shadowdiag.cpp` | creates the real pipelines from the repo's SPIR-V and checks the std140 block. |
| `shadowexec.cpp` | renders an occluder into a shadow map with the real `ShadowRaster.vert/.frag` and reads the depth back. |
| `pcss.comp.glsl` + `pcssexec.cpp` | `#include`s the real `ShadowSample.slang` and runs `ShadowLitFraction` over a receiver plane — the actual PCSS code, not a reimplementation. |

Both `.pgm` files are lit-fraction images straight off the device (0.50 m and 2.00 m lights).


① PIPELINE CREATION AND THE std140 BLOCK
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    sizeof(ShadowConstantRecord) = 480 B ; shader block expects 480 B  -> MATCH
    offsets: LightClip 0  TapOrigin 256  TapRadiance 320  TapNormal 384  Geometry 448  Control 464

    ShadowResolve        module OK (6135 words)      ShadowResolve compute pipeline:  CREATED
    ShadowRaster.vert    module OK (1779 words)      ShadowRaster graphics pipeline:  CREATED
    ShadowRaster.frag    module OK (1809 words)

The 480-byte layout that was previously only asserted at compile time is confirmed against a real driver
consuming the real SPIR-V. All three shaders lower, and both pipelines build.


② THE SHADOW MAP RASTERISES CORRECTLY
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
A 1 m quad at y=1, light at y=3 aimed at the origin, 35° half-angle, near 0.1 / far 10, into a 256² map:

    texels written by the occluder : 8464 / 65536 (12.9%)
    depth range                    : 0.959596 .. 0.959596 (mean 0.959596)
    expected depth (analytic)      : 0.959596

Exact to six decimals, and flat across the quad as a plane perpendicular to the light must be. `BuildLightClip`
is correct on a real device.


③ PCSS IS PHYSICALLY CORRECT — AND THE ROUND-10 FIX IS CONFIRMED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The whole point of the half-angle fix was that PCSS penumbrae should scale with the size of the luminaire.
Measured on the GPU, over a 128² receiver plane, counting umbra (lit=0), penumbra (0<lit<1) and lit (lit=1):

| light size | umbra | penumbra | lit | penumbra share |
|---|---|---|---|---|
| 0.05 m | 3969 | 256 | 12159 | 1.6% |
| 0.10 m | 3844 | 512 | 12028 | 3.1% |
| 0.25 m | 3481 | 1280 | 11623 | 7.8% |
| 0.50 m | 2809 | 2816 | 10759 | 17.2% |
| 1.00 m | 1849 | 5376 | 9159 | 32.8% |
| 2.00 m | 484 | 10752 | 5148 | 65.6% |

Penumbra grows monotonically with the luminaire and the umbra shrinks to almost nothing at 2 m — contact
hardening, behaving exactly as PCSS should.

**Negative control.** The same harness with the *pre-fix* recovery (`1.0 / ShadowLightClip[Tap][1][1]`) for a
ceiling lamp aimed straight down:

| light size | umbra | penumbra | lit | penumbra share |
|---|---|---|---|---|
| 0.05 m | 3969 | 256 | 12159 | 1.6% |
| 0.25 m | 3969 | 256 | 12159 | 1.6% |
| 1.00 m | 3969 | 256 | 12159 | 1.6% |
| 2.00 m | 3969 | 256 | 12159 | 1.6% |

Byte-identical at every light size: a 2 m area light cast exactly the same razor-sharp shadow as a 5 cm one.
PCSS was not merely inaccurate before the fix, it was **inert** — soft shadows did not respond to the luminaire
at all in the most ordinary lighting setup there is. `ShadowMatrixProof` predicted a 466,307× penumbra error from
the matrix algebra; this is that prediction confirmed in rendered pixels on a device.


④ WHAT ACTUALLY BROKE (harness bugs, and one emulator bug)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Worth recording, because three of the four failures looked at first like engine defects and were not.

1. **Nothing rasterised (harness).** The first run wrote 0 texels. The cause was a hand-rolled `LookAtPerspective`
   in the harness: it produced `w = −2.0` and `ndc.z = 1.06`, i.e. the geometry was behind the light and clipped.
   Transcribing the engine's `BuildLightClip` verbatim fixed it immediately. The lesson is the one from the
   contribution-cull work: a harness must exercise the engine's code, not a plausible reconstruction of it.
2. **"Unsupported Descriptor Type: −868172816" (harness).** Uninitialised `VkWriteDescriptorSet` padding. The
   value changed every run, which is the tell.
3. **"UNSUPPORTED: VkComponentSwizzle 7" (harness).** `VkImageViewCreateInfo::components` left unset.
4. **Intermittent segfault inside `vkQueueWaitIdle` (SwiftShader).** ~50% of runs on identical input. Isolated
   with flush markers to inside the queue wait; not size-dependent (0.50 m both crashed and succeeded). It is not
   ours: raster-only succeeded 6/6 and compute-only 6/6, but graphics+compute in one submit is the failing case,
   and `SWIFTSHADER_DISABLE_MULTITHREADING=1` improves it to 4/6. Adding a proper depth-write→shader-read barrier
   did *not* change it (2/10), so the missing-barrier hypothesis was wrong and is recorded here as rejected.
   Decisively: **every run that completes produces byte-identical results** (4/4 identical over 12 attempts). It
   is a liveness bug in the emulator's 2-core scheduler, not a correctness signal about the shaders.


⑤ WHAT THIS DOES NOT SHOW
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Stated plainly so the result is not over-read:

  • **No timings.** SwiftShader's per-workgroup cost is flat, so it cannot rank the tiers. Milliseconds still
    require hardware, and the GPU shadow passes still have no timestamps.
  • **The validation layers were not available.** They are not reachable as binaries here (the GitHub release
    asset host is blocked) and a from-source build was still compiling its dependencies when this was written.
    Everything above therefore rests on results and return codes, not on layer diagnostics. Building them remains
    the single highest-value next step: it is what would catch a wrong-but-not-fatal descriptor or barrier.
  • **`RecordShadowFrame` itself was not executed.** These harnesses record their own command buffers against the
    engine's shaders, matrix and constant layout. The engine's own recording path — its barriers, its per-tap
    loop, its indirect draws — has still not run.
  • **No ray query on this device**, so nothing about ReSTIR/GI is exercised. The GI-off path is gated ray-free,
    so that limitation does not affect the shadow result.


REPRODUCING
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    # 1. SwiftShader + cmake — see References/SoftwareVulkanDevice.md
    # 2. the GLSL compiler (slang's bundled glslang, via PyPI)
    python3 -m pip install --target /tmp/spy slangpy
    git clone --depth 1 --branch v2026.12 https://github.com/shader-slang/slang.git /tmp/slangsrc
    g++ -std=c++17 myglslc.cpp -o /tmp/myglslc -ldl

    # 3. lower the shadow shaders (run from the repo root)
    /tmp/myglslc Engine/Shaders/ShadowRaster.vert.slang vertex   /tmp/spv/ShadowRaster.vert.slang.spv Engine . Engine/Shaders
    /tmp/myglslc Engine/Shaders/ShadowRaster.frag.slang fragment /tmp/spv/ShadowRaster.frag.slang.spv Engine . Engine/Shaders
    /tmp/myglslc Scratchpad/GpuShadowDiagnosis/pcss.comp.glsl compute /tmp/spv/pcss.spv Engine . Engine/Shaders

    # 4. build and run (retry: see defect 4 above)
    g++ -std=c++17 -I /tmp/sws/include pcssexec.cpp -o /tmp/pcssexec \
        /tmp/sws/build/Linux/libvulkan.so.1 -Wl,-rpath,/tmp/sws/build/Linux
    VK_ICD_FILENAMES=/tmp/sws/build/Linux/vk_swiftshader_icd.json /tmp/pcssexec 0.50 out.pgm

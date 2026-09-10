══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  A software Vulkan device in the sandbox — what it is good for, and what it is not
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

STATUS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Built and verified working: **SwiftShader** (Google's software Vulkan implementation) produces a real Vulkan 1.3
device in this sandbox, on a machine with no GPU at all.

Use it for **correctness**. Do **not** use it for **performance**. The measurement below is why.


WHY THIS WAS TRIED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Every performance number in round 10 is a work count, not a millisecond, and no part of the GPU shadow path had
ever executed. The open question was whether a software GPU could close either gap. It closes exactly one.


THE BUILD (the only route that works here)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The sandbox has no GPU (`/dev/dri` absent) and, more restrictively, **the Debian mirrors are unreachable** —
`deb.debian.org` fails on plain HTTP and on HTTPS, so `apt-get install mesa-vulkan-drivers vulkan-tools` cannot
work, and neither can lavapipe, which needs LLVM from apt. Only `github.com` and `pypi.org` are reachable.

That leaves one viable path, and it does work:

    # cmake/ninja from PyPI (PEP 668 blocks a normal install, so --target)
    python3 -m pip install --target /tmp/cmakepkg cmake ninja
    export PATH=/tmp/cmakepkg/bin:$PATH PYTHONPATH=/tmp/cmakepkg

    # SwiftShader vendors its own LLVM, so it needs nothing from apt
    git clone --depth 1 https://github.com/google/swiftshader.git /tmp/sws     # ~1.4 GB
    cd /tmp/sws
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DSWIFTSHADER_BUILD_TESTS=OFF -DSWIFTSHADER_BUILD_BENCHMARKS=OFF -DSWIFTSHADER_BUILD_PVR=OFF
    cmake --build build --target vk_swiftshader -j2      # ~45 min on 2 cores, ~1200 objects

Produces `/tmp/sws/build/Linux/libvk_swiftshader.so` (20 MB), `libvulkan.so.1`, and an ICD manifest. Run any
Vulkan client against it with:

    VK_ICD_FILENAMES=/tmp/sws/build/Linux/vk_swiftshader_icd.json ./your_app

Headers come with the clone at `/tmp/sws/include` — useful, since `/tmp/vkh` from earlier rounds does not survive.
Note everything here lives in `/tmp` and is **not persisted**; the clone and the 45-minute build must be repeated
in a new sandbox. Nothing about it belongs in the repo.

Device it reports:

    SwiftShader Device (LLVM 10.0.0) | api 1.3.0 | timestampPeriod 1.000 ns | timestampComputeAndGraphics 1
      queue 0: flags 0x7 (graphics|compute|transfer), timestampValidBits 64

Graphics + compute on one queue, and full 64-bit timestamp support — so `vkCmdWriteTimestamp` genuinely works.


THE MEASUREMENT THAT DECIDES THE QUESTION
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
A compute shader with a fixed 20,000-iteration arithmetic loop, dispatched at increasing workgroup counts, timed
both with the device's own timestamps and with the wall clock:

| workgroups | gpu timestamp | wall clock | µs per workgroup |
|---|---|---|---|
| 1 | 117.1 µs | 1442.8 µs | — (first-dispatch warm-up) |
| 64 | 26.4 µs | 32.7 µs | 0.4125 |
| 256 | 60.5 µs | 65.9 µs | 0.2363 |
| 1024 | 141.1 µs | 145.9 µs | 0.1378 |
| 4096 | 549.0 µs | 553.6 µs | 0.1340 |
| 16384 | 2309.5 µs | 2318.2 µs | 0.1410 |

The timestamps are real: they track the wall clock closely and scale with the work. But the cost per workgroup
settles at a **constant ~0.134–0.141 µs** and stays there. 256× the workgroups costs 87× the time, and the
per-unit cost stops improving after ~1024 groups.

That flat per-workgroup cost is the whole answer. SwiftShader is executing workgroups essentially serially across
2 CPU cores, so total time is just (work × constant). **Real hardware has the opposite signature**: a wide GPU
runs hundreds of workgroups concurrently, so the curve stays nearly flat in total time while occupancy fills, and
only turns linear once the machine saturates. Occupancy, warp scheduling, memory-bandwidth limits, texture-cache
behaviour and latency hiding — the things that decide what a shadow kernel actually costs — are precisely what a
CPU implementation does not reproduce.

Concretely for the shadow work: the PCSS kernel's cost on real hardware is dominated by whether its 106 texel
fetches per surface-tap pair hit the texture cache and whether enough waves are in flight to hide that latency.
SwiftShader models none of that. It would report the fetch count faithfully as CPU time and mislead completely
about the GPU cost. A tier ranking taken from it could easily invert the true one.


WHAT IT IS ACTUALLY WORTH
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
It answers the *other* open risk, which is arguably the more valuable one. The round-10 caveat is that the GPU
shadow path has never executed — layouts, matrices and dispatch logic are verified only by reading. SwiftShader
runs the real driver stack, so it can execute the actual pipeline and catch:

  • validation-layer errors, descriptor/layout mismatches, wrong binding indices
  • the 480-byte std140 shadow constant block disagreeing between C++ and Slang
  • SPIR-V that fails to create a pipeline
  • wrong image layouts and missing barriers
  • shadow output that is blank, inverted, or not bit-comparable to the CPU reference

Those are exactly the failures "a first device run may still surface", and none of them need real timing.

So the split is clean:

| question | tool |
|---|---|
| does the GPU shadow path run at all, and produce the right pixels? | **SwiftShader — yes, usable now** |
| how many milliseconds does the shadow pass cost? | **real hardware only** |

A caveat on ray tracing: SwiftShader exposes no `VK_KHR_ray_query`. That does not block the shadow work, because
`RayTracingCapabilitySet` detects ray support rather than requiring it, and the GI-off shadow path is gated as
ray-free by `CheckShadowTiers.sh`. The ReSTIR/GI paths cannot be exercised this way.

Conclusion: there is no software GPU that will tell us what the shadow passes cost in milliseconds — that number
only exists on hardware. GPU timestamps should still be instrumented, because the instrumentation is what makes
the number obtainable the moment the engine runs on a real device.

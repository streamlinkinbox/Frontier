# Plan — dynamic rigid bodies under ReSTIR (two-level traversal)

Status: **proposal, awaiting approval.** No code written yet.
Goal: Jolt-driven balls falling in the showroom, ray-traced by the existing ReSTIR kernel, without the frame
cost scaling with triangle count.

---

## 1. Why the current design cannot move anything

The renderer bakes geometry into world space before it ever reaches the GPU:

| Stage | What it holds | Consequence |
|---|---|---|
| `SceneStructure::FlatTriangles` | `TriangleIndex` × N, **world-space** vertex positions | moving a body means rewriting its triangles |
| `TraversalIndex` | CWBVH over those flat triangles | moving a body means rebuilding the tree |
| `TraverseClosest(O, D, rD, tmax)` | one tree, world space | no notion of an instance |

Measured on the reporter's GTX 1650 SUPER / i3-2120, showroom (5286 triangles): CWBVH build
**116–248 ms**. Frame budget at 60 Hz is **16.7 ms**. A per-frame rebuild is 7–15× over budget, so
"just rebuild it" is not a viable option even for a dozen balls.

`UploadTraversal` also does `vkDeviceWaitIdle` + destroy + recreate. That is correct for load time and
unusable per frame — it stalls the whole device.

---

## 2. The answer, and it is not "do the rebuild faster"

**Rigid-body motion does not require any acceleration-structure rebuild at all.** A rigid body's triangles
never change *relative to each other*; only the body's transform changes. The standard solution — what DXR,
Vulkan RT and every console renderer does — is a two-level structure:

- **BLAS**, one per unique mesh, built **once**, in **object space**. Never rebuilt for rigid motion.
- **TLAS**, one tree over N instance boxes, rebuilt per frame. N is the *instance* count (~12), not the
  triangle count (5286), so it is orders of magnitude cheaper.
- A ray hits a TLAS leaf, is transformed into that instance's object space by its inverse matrix, and
  continues into the BLAS. **The geometry never moves; the ray moves instead.**

tinybvh's author states it plainly: *"mesh motion defined by a matrix does not require a rebuild or refit,
and is essentially free"*, and *"constructing the TLAS for 51 objects will take far less than a
millisecond."*

Expected cost for the drop scene: **TLAS rebuild ≈ 0.05–0.2 ms/frame on the CPU for ~12 instances**, versus
116–248 ms today. Traversal gains one matrix multiply per instance entered.

### Answering "can it be done on the GPU?" — measured, not argued

`Scratchpad/TlasRebuildBenchmark.cpp` rebuilds a TLAS over N instances every iteration, moving every
instance first so the number is a real per-frame cost. Compiled **SSE2-only**, matching a pre-AVX host like
the i3-2120 (a 2.6 GHz Xeon stood in for it; your 3.3 GHz part should be no slower per core):

| Instances | CPU TLAS rebuild | Share of a 16.7 ms frame |
|---|---|---|
| **23**  (the drop scene: 11 static + 12 bodies) | **0.019 ms** | **0.1 %** |
| 100 | 0.090 ms | 0.5 % |
| 1 000 | 0.98 ms | 5.9 % |
| 10 000 | 9.2 ms | 55 % |
| 100 000 | 54.5 ms | 327 % |

BLAS builds, which happen **once at load and never per frame**: room (5286 tris) 5.5 ms, ball (320 tris)
0.3 ms.

**At 23 instances the CPU TLAS rebuild is 0.019 ms — roughly one thousandth of the frame.** Moving that to
the GPU would mean a dispatch, a barrier and a synchronisation point, each of which costs more than 0.019 ms
on its own. It would be measurably *slower*, and it would add a GPU-side builder to maintain.

The crossover is around **1 000–10 000 instances**. Below that, CPU wins outright. Above ~10 000 a GPU build
starts to pay, and that is the point to revisit — not now.

### 2b. The real target scene: ~1000 static + ~100 moving, growing to many cars

The 23-instance drop scene is a stepping stone, not the destination. Measured for the stated world
(`Scratchpad/TlasScaleBenchmark.cpp`, SSE2, pre-AVX host), rebuilding ONE merged TLAS per frame:

| Total instances | Rebuild | Frame share | |
|---|---|---|---|
| 500 | 0.47 ms | 2.8 % | comfortable |
| 1 000 | 1.02 ms | 6.1 % | comfortable |
| **1 150** (1000 static + 100 moving) | **1.20 ms** | **7.2 %** | **the stated scene — fine** |
| 1 500 | 1.89 ms | 11.3 % | budget starts to hurt |
| 2 500 (100 cars) | 2.66 ms | 15.9 % | needs the fix below |
| 6 000 | 6.60 ms | 39.5 % | unusable as a full rebuild |

**Cars stack fast.** A car is not one instance: body + 4 wheels ≈ 5. So 20 cars ≈ 100 moving instances,
100 cars ≈ 500. Combined with 1000–2000 static props, a busy race scene lands at **2 500–3 000 instances**,
which is 16–20 % of the frame on the CPU — too much to spend on bookkeeping.

UI is not a concern at all: those ~50 elements are `InterfaceInstanceFigure`s drawn by the raster overlay
(`InterfaceExchange`), **not** TLAS instances. They never enter the acceleration structure. Cost is one draw
call regardless of element count.

### 2c. The obvious optimisation, measured and REJECTED

Splitting into a static TLAS (built once) and a dynamic TLAS (rebuilt per frame) makes the rebuild
**13.7× cheaper** — 1.13 ms → 0.082 ms at 1100 instances. It looks like an easy win.

It is not. Every ray must then traverse **both** trees, and neither can cull the other's geometry.
Measured over 200 000 rays (`Scratchpad/TlasSplitTraversalBenchmark.cpp`):

| | per ray | |
|---|---|---|
| one merged TLAS | 0.370 µs | |
| two TLASes | 0.492 µs | **+32.8 %** |

Trading ~1 ms of CPU build for **+33 % on every ray** is a bad deal by a wide margin — at 1 M rays/frame the
traversal side dominates completely. **The split is rejected.** Recording it so it is not "rediscovered"
later as a good idea.

Also checked: `BVH::Refit()` **cannot** be used on a TLAS — tinybvh aborts with *"do not refit a TLAS, use
Build(..)"*. So the cheap middle option does not exist in this library.

### 2d. What actually scales, in priority order

1. **Rebuild one merged TLAS per frame on the CPU.** Correct and simple to ~1 500 instances. This is what
   D1–D5 will implement, and it comfortably covers the stated 1000 + 100 scene at 7 % of frame.
2. ~~**Move the rebuild off the critical path.**~~ **WITHDRAWN — there is no spare core.** See §2e.
3. **Cull before building.** Unreal's guidance: instances outside the view/relevance radius never enter the
   TLAS. A race track has most of its 1000 props behind or far from the camera; culling to the active
   radius typically removes the majority, and this composes with (2).
4. **Only then consider a GPU build**, past ~10 000 instances, where the measurements show the CPU losing.

The plan below is written for (1) because that is what the stated scene needs today. (3) is additive and
does not change the data layout — which is the point of doing (1) first rather than guessing.

### 2e. Why the TLAS worker thread is withdrawn

I proposed building the TLAS on a worker thread. That was wrong for this target, and the objection is
correct: **the threads are already spoken for.**

Thread census on an **i3-2120 — 2 physical cores, 4 hardware threads**:

| Thread | Owner | Negotiable? |
|---|---|---|
| main / render | GLFW + Vulkan submit | no |
| Jolt job pool | `hardware_concurrency() - 1` → **3 workers** | count is tunable, existence is not |
| miniaudio realtime callback | OS-owned, high priority | **absolutely not** |
| tinybvh internal build threads | 8 references in the header | implicit |

That is already **1 + 3 + 1 = 5 threads on 4 lanes** before adding anything. Jolt's default alone
oversubscribes the machine.

Measured (`Scratchpad/TlasContentionBenchmark.cpp`), 1150 instances, rebuild time as competing threads are
added:

| Competing busy threads | TLAS rebuild | Frame share |
|---|---|---|
| 0 | 1.15 ms | 6.9 % |
| 1 | 1.10 ms | 6.6 % |
| 2 | 1.92 ms | 11.5 % |
| 3 | 2.34 ms | **14.0 %** |

**Oversubscription doubles the cost.** A "background" TLAS build on a saturated CPU does not hide latency —
it adds a runnable thread that steals slices from the render thread and, far worse, can preempt the audio
callback. A missed audio deadline is an audible click; a late TLAS is nothing. **We must never trade an
audio glitch for a graphics optimisation.**

**Revised guidance:**

- Keep the TLAS rebuild **synchronous on the main thread**. At 1150 instances it is 1.2 ms and predictable.
- **Cap Jolt's job pool explicitly** rather than taking `hardware_concurrency() - 1`. On a 4-thread host,
  1–2 workers leaves room for render and audio. This is a `RigidBodyConfiguration::WorkerThreads` value,
  already exposed — it needs a sensible default, not new machinery.
- Reach for **culling (3)** before threading. It reduces the work instead of relocating it, and it costs no
  cores.
- Revisit threading only on a host with genuinely idle cores, and only with the audio thread pinned away
  from the worker.

The general rule this establishes: **on a 2-core machine, latency hiding is a myth — there is nowhere to
hide it.** Reduce the work or do it predictably.



Three separate questions, and they have different answers:

1. **Ray transformation — yes, GPU, and that is where it belongs.** It is a `mat3x4 × vec3` per instance
   entered, in the traversal shader. This is the bulk of the per-ray work and it parallelises perfectly.
2. **TLAS build — CPU, deliberately.** At ~12–200 instances the tree is tiny; a GPU build would cost more
   in dispatch latency and readback synchronisation than the build itself. Industry practice agrees: Unreal
   rebuilds the TLAS on the CPU each frame and calls the cost *"proportional to the number of instances"*.
   GPU TLAS builds only pay off in the 10⁵–10⁶ instance range. **We would be optimising the wrong number.**
3. **Transform upload — GPU buffer write, no stall.** A persistently-mapped, per-cycle-slot ring of
   instance rows (~12 × 128 B ≈ 1.5 KB). No `vkDeviceWaitIdle`, no reallocation, no barrier beyond the
   normal frame one.

So the honest summary: **the expensive part is already on the GPU; the cheap part stays on the CPU because
moving it to the GPU would make it slower.**

---

## 3. Top 3 research sources

**① Bikker, *How to build a BVH, part 5: TLAS & BLAS* (2022) + the tinybvh manual (2025).**
<https://jacco.ompf2.com/2022/05/07/how-to-build-a-bvh-part-5-tlas-blas/>
Decisive because **Jacco Bikker is the author of tinybvh, the library already vendored in
`ExternalPackages/tinybvh`.** Gives the exact construction we need — BLAS in object space, TLAS over
instances, ray transformed by the inverse matrix — and the quantitative claim that a 51-object TLAS builds
in well under a millisecond. It also tells us the racing-game case (static track + 50 moving parts) is
precisely our showroom + falling balls.

**② "Adding support for two-level acceleration for raytracing", Interplay of Light (2020).**
<https://interplayoflight.wordpress.com/2020/11/01/adding-support-for-two-level-acceleration-for-raytracing/>
The most valuable source because it is an **independent measurement of the thing we are worried about**:
someone retrofitting TLAS/BLAS onto a custom (non-DXR) software ray tracer, exactly our situation. Result:
TLAS/BLAS traversal **31 ms vs 30.5 ms** for the monolithic BVH — i.e. the ray-transform overhead is
**~1.6%**, not the 2× a reader might fear — and **1.2 ms CPU** to rebuild the TLAS. This is the number that
says the plan is safe. It also confirms the key design point we adopt: *transform the ray once per BLAS
entered, never the geometry.*

**③ Unreal Engine *Ray Tracing Performance Guide* (Epic) + DXR spec on Partitioned TLAS (Microsoft).**
<https://dev.epicgames.com/documentation/unreal-engine/ray-tracing-performance-guide-in-unreal-engine>
Production guidance rather than theory, and it sets our scaling expectations honestly: *"The TLAS is
rebuilt every frame... costs are mostly proportional to how many mesh instances go into the acceleration
structure"*, and rigid transforms need **no BLAS work** — only skinned/deforming meshes rebuild a BLAS.
It also names the failure mode we must avoid (refit quality degrading under large motion → periodic
rebuild) and the escape hatch if instance counts ever explode (partitioned TLAS). Confirms we should **not**
prematurely build the GPU-side TLAS machinery.

Secondary, noted but rejected for now: *Ray Tracing Massive Amounts of Animated Geometry* (SIGGRAPH 2025,
tetrahedral cages). Solves **deformable** animation at massive scale — genuinely impressive, and irrelevant
here: our bodies are rigid, so the far simpler matrix transform is exact and free. Recorded so nobody
re-researches it later.

---

## 4. Design

### 4.1 Data

```
InstanceRecord  (exists, 160 B, already has World + PreviousWorld)
    + BlasOrdinal        which BLAS this instance traverses
    + InverseWorld       world → object (new; needed by the ray transform)

TraversalIndex  (today: one CWBVH)
    → BlasIndex          one CWBVH per unique mesh, object space, built ONCE
    → TlasIndex          flat 2-wide BVH over instance AABBs, rebuilt per frame
```

Static geometry (the room) stays exactly as it is: one BLAS, identity transform, one TLAS leaf. **The R0
Cornell-box bit-identity reference must still render byte-identical** — that is the regression gate.

### 4.2 Shader

`TraversalCWBVH.slang` keeps `TraverseClosest(O, D, rD, tmax)` **unchanged** and gains a wrapper:

```
TraversalHit TraverseScene(vec3 O, vec3 D, vec3 rD, float tmax)
    walk TLAS with the world-space ray
    on leaf i:
        Oo = InverseWorld[i] · O          // point
        Do = InverseWorld[i] · D          // direction, no translation
        hit = TraverseBlas(BlasRoot[i], Oo, Do, 1/Do, tmax)
        keep nearest; record instance ordinal alongside primitive
```

Because the existing function is already parameterised purely on `(O, D, rD, tmax)`, this is an additive
change — the single-BLAS path stays intact and remains the fallback when instance count is 1.

⚠️ **The hit record must carry `instance` as well as `primitive`.** Today `primitive` indexes the flat
world-space triangle array and material lookup keys off it. Under TLAS, primitive is BLAS-local, so
`(instance, primitive)` is the identity. Every consumer — shading, material fetch, ReSTIR reservoirs which
*store* hit identity, and the luminaire `TriangleSlot` indirection — has to be updated together. **This is
the actual risk in the whole plan, not the traversal maths.**

### 4.3 Per-frame flow

```
Jolt.Advance(Δτ)
Jolt.QueryPoses(...)                     → pose per body
World[i], InverseWorld[i] ← pose         (PreviousWorld[i] ← last World[i], motion vectors free)
Tlas.Rebuild(instance AABBs)             ~12 boxes, sub-millisecond, CPU
UploadInstanceRing(slot)                 ~1.5 KB, persistently mapped, no stall
RecordAndPresent(...)
```

### 4.4 Why this is fast

- Nothing scales with triangle count. The only per-frame work is O(instances).
- No BLAS is ever rebuilt while bodies stay rigid.
- No device stall: the instance ring replaces destroy/recreate.
- ReSTIR temporal reuse already has `PreviousWorld`, so motion vectors come free and reprojection keeps
  working for moving bodies.

---

## 5. Staging — each row independently provable

| Row | Work | Proof |
|---|---|---|
| **D1** | BLAS/TLAS split in `TraversalIndex`; single static instance | Cornell box **bit-identical** to R0; CWBVH stats unchanged |
| **D2** | `TraverseScene` wrapper + `(instance, primitive)` hit identity through shading, materials, reservoirs, luminaires | showroom renders identically with 1 instance; CPU harness asserts identity round trip |
| **D3** | Instance ring upload, no `vkDeviceWaitIdle`; transforms animate from a scripted sine (no physics yet) | headless: N frames, no validation errors, transforms observably applied |
| **D4** | Jolt poses drive the transforms; drop scene in the showroom | balls fall, settle, rest at Jolt's slop; TLAS rebuild time logged per frame |
| **D5** | Telemetry + budget guard | TLAS ms and instance count in the diagnostics overlay; fail loudly if TLAS > 1 ms |

D1 and D2 carry the regression risk. D4 is the visible payoff.

---

## 6. What I will *not* do

- **No GPU TLAS build.** Wrong optimisation at 12 instances; revisit past ~10⁵.
- **No per-frame BLAS rebuild or refit.** Rigid bodies do not need it. Deformables are out of scope.
- **No removal of the single-BLAS path.** It stays as the identity case and the fallback.
- **No claim that this is verified on hardware.** There is no GPU in this sandbox. Everything here will be
  proven by CPU harness, SPIR-V compilation and reflection, and headless numeric checks — the first real
  frame will be on your machine, as with the panel.

---

## 7. Honest risks

1. **Hit identity is a wide change** (§4.2). ReSTIR reservoirs persist hit identity across frames; if the
   meaning of `primitive` changes without every consumer agreeing, temporal reuse corrupts silently and
   looks like noise rather than a crash. Mitigated by doing D2 as its own row with a 1-instance identity
   proof before anything moves.
2. **Traversal cost is not literally zero** — source ② measures ~1.6% on their scene. Acceptable, but it
   should be measured, not assumed.
3. **TLAS quality under scatter.** Bodies spread over a large volume make loose instance boxes. At ~12
   bodies this is irrelevant; noted for when the count grows.
4. **Sandbox cannot prove frame time.** The 116–248 ms figure is yours; the sub-millisecond TLAS figure is
   the literature's. I can prove correctness here, not performance.

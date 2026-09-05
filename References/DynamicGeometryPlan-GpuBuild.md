# Plan — moving the acceleration-structure build to the GPU (R8 / H-PLOC)

Status: **proposal, awaiting approval.** No code written yet.
Question answered: *should the per-frame BVH work move to the GPU?*
Short answer: **yes — but not yet, and for a reason that changes with your scene size.**

---

## 1. What is actually being paid for today

After D6 the per-frame chain, measured on the real drop scene (pre-AVX host):

| Stage | Cost | Scales with |
|---|---|---|
| transform body triangles | ~0.1 ms | moving triangles |
| `BVH::Refit` | 0.08 ms | moving triangles |
| MBVH8 collapse | 0.26 ms | **total** nodes |
| **CWBVH compress** | **2.66 ms** | **total** nodes |
| blob upload (734 KB) | not separately measured | total nodes |

**2.57 ms mean, 2.75 ms peak** — 15 % of a 16.7 ms frame — plus **45 MB/s** of sustained
host→device traffic that exists only because the structure is built on the wrong side of the bus.

The compress dominates at 87 %. It is a *serial pack* of a format designed to be read by thousands of
GPU threads, performed by one CPU core, and then copied back across PCIe. That is the shape of a problem
that belongs on the GPU.

---

## 2. Why I said "CPU" earlier, and why that answer now flips

Three turns ago I measured the **TLAS** question and concluded CPU, correctly: at 23 instances a TLAS rebuild
is 0.019 ms and a GPU dispatch costs more than the whole job. That reasoning was about **instances**.

This is the **BLAS** question — triangles, not instances — and the numbers are three orders of magnitude apart.
The rule stated then still holds and now points the other way:

> work that scales with **pixels or triangles** belongs on the GPU; work that scales with **objects** stays on
> the CPU until objects reach the thousands.

Both answers come from the same rule. Only the operand changed.

---

## 3. The crossover, sized against *your* target scene

CPU cost is linear in moving triangles. A GPU build has a roughly **constant** dispatch floor (~0.3–0.6 ms for
H-PLOC's four passes plus wide-BVH conversion) and then near-free work at our scale.

| Scene | Moving tris | CPU (measured, extrapolated) | GPU (est.) | Ratio |
|---|---|---|---|---|
| today: 12 balls @ 552 | 6 336 | **2.57 ms** | ~0.51 ms | 5× |
| 20 cars @ 2 000 | 40 000 | 16.2 ms | ~0.59 ms | 27× |
| 50 cars | 100 000 | 40.6 ms | ~0.73 ms | 56× |
| 100 cars | 200 000 | 81.1 ms | ~0.95 ms | **85×** |

**The CPU path does not reach your stated target.** At 20 cars it is already over a full frame; at 100 cars it is
five frames of pure bookkeeping. This is not an optimisation — past ~15 000 moving triangles it is the difference
between shipping the scene and not.

Equally: **at today's 6 336 triangles the GPU build is overhead-bound, not work-bound.** The actual clustering work
is 0.014 ms; everything else is dispatch and synchronisation. A 5× win is real but it is the least impressive
version of this change, which is precisely why it should be built when the scene demands it rather than now.

---

## 4. The hardware constraint that shapes the design

The target GPU is a **GTX 1650 SUPER (TU116)**. Your own run log settles it:

```
Ray tracing: supported = Software BVH, using = Software BVH
[AS ext 0 feat 0 | RQ ext 0 feat 0]
```

No `VK_KHR_acceleration_structure`, no ray query, **no RT cores** — TU116 is the Turing die that omits them. So
`vkCmdBuildAccelerationStructure` is not available and never will be on this machine. A GPU build here means
**compute shaders we write**, emitting the same CWBVH blob `TraversalCWBVH.slang` already traverses.

That is a constraint but not a blocker, and it has a hidden benefit: the result is portable to any GPU with
compute, rather than to RTX parts only.

**What already exists and gets reused:** `ClusterCull.slang` is a real `local_size_x = 64` compute pass over
std430 buffers, and `VisibilityExchange` already owns dispatch and barrier call sites. The pattern is in the
codebase; this adds passes to it rather than inventing infrastructure.

---

## 5. Design

Four compute passes over the **dynamic** triangles only, plus a fifth conversion:

```
G1  scene bounds      reduce over moving triangles          → one AABB
G2  Morton codes      30-bit, quantised in that AABB        → key per triangle
G3  radix sort        4 × 8-bit passes, workgroup histogram → sorted keys
G4  H-PLOC            hierarchical PLOC, search radius 8    → binary BVH
G5  wide conversion   collapse + CWBVH pack                 → the blob binding 9 already reads
```

The static tree keeps its load-time CPU build. **This is the static/dynamic split from D6, arriving as a
consequence of the GPU work rather than as separate effort** — the two-level structure is needed here anyway,
so the descriptor change is paid once.

### The binding problem, and how it is handled

`ReSTIRViewport.slang` uses bindings 0–18, and **18 is the variable-count bindless texture array, which must be
last in the set**. A second tree needs two more buffers.

Two options, and I would take the first:
- **New descriptor set.** Put the dynamic tree's node/leaf buffers in set 1. No renumbering of set 0, so no
  churn in the shader, the C++ mirror, or any existing write site. Costs one extra `vkCmdBindDescriptorSets`.
- Renumber 18 → 20 and slot the tree at 18/19. Correct but touches every write site, for no gain.

### Traversal

`TraverseClosest` already takes `(O, D, rD, tmax)` and nothing else, so it becomes two calls — static tree, then
dynamic tree, keep the nearer hit. Same pattern as the D2 hit-identity work, which is why `(instance, primitive)`
was worth doing then.

---

## 6. Staging

| Row | Work | Proof |
|---|---|---|
| **G0** | CPU reference implementation of H-PLOC producing a byte-identical blob to the current path | golden-blob hash vs `TraversalIndex::Build` |
| **G1** | bounds + Morton + sort on GPU, results read back and compared to the CPU reference | sorted key arrays match exactly |
| **G2** | H-PLOC binary build on GPU | tree topology valid; SAH within 5 % of the CPU tree |
| **G3** | wide conversion + CWBVH pack on GPU | **blob bit-identical to the CPU pack** for a fixed input |
| **G4** | second descriptor set; traversal queries both trees | rays agree with the merged-tree result (the D5 gate, reused) |
| **G5** | budget telemetry + fallback to the CPU path on any failure | `CheckDynamicGeometryBudget.sh` threshold drops to 1 ms |

G0 and G3 carry the risk. Everything is gated by comparison against the CPU path that works today, so at no
point is the renderer relying on unverified GPU output.

---

## 7. Honest assessment

**Cost:** this is the largest single piece of work proposed so far — five compute shaders, a radix sort, a
descriptor-set change and a traversal change. Realistically several sessions, not one.

**What I cannot verify here:** there is no GPU in this sandbox. Every previous row was provable by CPU harness,
SPIR-V compilation and reflection. This one is not — G1–G3 can be validated by readback comparison *only on your
machine*. I can write it and prove the shaders compile and that the CPU reference agrees; the first genuine
execution would be yours. That is a materially weaker verification story than D1–D6 had, and you should weigh it.

**My recommendation:** **not now.** At 2.57 ms the current path is inside budget and fully proven. The moment the
scene grows past roughly 15 000 moving triangles — about 8 cars — it stops being viable and this becomes the
critical path. Build it then, with the scene that justifies it, rather than against a 12-ball demo where it wins
5× and cannot be tested.

**What I would do first instead:** the 3D-UI → audio work you asked for two turns ago and I still have not
delivered. It is smaller, it is verifiable here, and it is the last thing you explicitly requested that remains
outstanding.

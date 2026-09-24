# Dynamic geometry — BVH for moving and animated objects

Written 2026-09-17. Answers, with measurement and research: how moving/rotating objects and animated (triangle-level,
rig-less) characters should get an acceleration structure, whether the per-frame work belongs on the CPU or the GPU,
and what that costs in the layout this renderer actually traverses.

The question is not "CPU or GPU" in the abstract. It is **which work happens per frame, over how much geometry**, and
that number decides the answer. Three frequencies, three answers:

| work | frequency | scope | where it belongs |
|---|---|---|---|
| build the world tree | once at load | whole scene | **CPU** (today's `TraversalIndex::Build`; deterministic, already proven) |
| move a rigid object | every frame | transforms only — 0 triangles | **either** (CPU is 0.16–3.1 ms for the whole TLAS; see §3) |
| deform a mesh (skinning/VAT/cloth) | every frame | that mesh's triangles | **GPU refit**, once the layout allows refit-in-place |
| change topology (destruction, LOD swap) | occasionally | that mesh | CPU rebuild on a worker thread, or the GPU build kernel (`BlasBuild.slang`; H-PLOC's cluster merge is the open optimisation) |

## 1. What exists in this repository today

- `Engine/GeometricRaster/TraversalIndex.{h,cpp}` — tinybvh (submodule `ExternalPackages/tinybvh`) binned-SAH build →
  MBVH8 collapse → **CWBVH compress**, emitted as two SSBO blobs (80 B nodes, 48 B per triangle) that
  `Engine/Shaders/TraversalCWBVH.slang` walks in compute. One tree, world space, over the whole scene.
- `BuildBottomLevel` (D1) — same code under a name that records the intent ("these triangles are one instance's
  geometry"); `RefitBottomLevel` (D5) — refit the binary tree, then re-collapse and **re-compress the whole scene**
  blob. Measured in the header: 0.22 ms refit, 0.74 ms collapse, **6.31 ms compress** for a 16.8 k-triangle showroom
  on a pre-AVX host. That compress is O(all nodes) and re-emits static geometry too — the documented ceiling.
- `Projects/Project-Zero/Source/PhysicsInstanceSequence.h` (D4/D5) — Jolt poses → `InstanceRecord::World` (raster side)
  *and* → rewritten world-space flat triangles → refit (ray side). Two paths that must agree, one refit for the whole
  scene.
- `InstanceRecord` already carries `World[16]` **and** `PreviousWorld[16]`, and the R2 motion vectors are computed from
  the pair — so ReSTIR's reprojection is already correct for moving objects. Only the traced geometry lags.

## 2. The two-level structure, and why the local tree is the right answer

The industry model (DXR/Vulkan-RT, and tinybvh's own `BLASInstance` + `BVH::Build(BLASInstance*, …, BVHBase** BLASes)`)
is exactly what was proposed earlier in the conversation:

- **BLAS** — one tree per *unique mesh*, in **object space**, built once. A BLAS can be shared by any number of
  instances.
- **Instance row** — `transform` (object→world) and its inverse, plus the world-space AABB of the transformed root.
- **TLAS** — a tree over instances' world AABBs only (`N` leaves, not `N × triangles`).
- **Traversal** — walk the TLAS; at an instance leaf, transform the ray into object space and walk that instance's
  BLAS. Hits come back in object space and are transformed to world (normals via the inverse-transpose).

tinybvh ships this end to end: `BLASInstance::Update(blas)` recomputes the world AABB from the eight transformed
corners; its GPU example does the same on device (`kernels/raytracer.cl`, `tmpRay.O = transform_point(ray.O, inst.invTransform)`).
So "save the BVH locally and move it relative to the world" is not a heuristic — it is the standard structure, and
**a rigidly moving object costs zero tree work per frame**: the BLAS is not touched at all, only its instance row.

Two consequences that matter here:

1. It removes the D5 ceiling entirely. Today a dropped ball makes the whole scene re-quantize; with instance rows, the
   same drop touches 64 bytes plus a TLAS leaf.
2. It makes instancing free. The M10 level's 49-material sphere grid is 49 instances of **one** sphere BLAS today,
   with 49 transform rows.

## 3. Measured budgets (this CPU: 2 cores / AVX2, tinybvh from the pinned submodule)

Built for this note (`/tmp/bvhbench.cpp`; triangle counts are the ones the plan argues over — a character, a hero prop,
a level chunk; a shell of small triangles, `-O2 -mavx2`):

| phase | 16 k tris | 64 k tris | 256 k tris |
|---|---|---|---|
| binary binned-SAH build (once, load) | 13.2 ms | 32.9 ms | 148 ms |
| BuildHQ / SBVH (once, load) | 88 ms | 241 ms | 1 083 ms |
| **Refit** the binary tree (deform) | **0.17 ms** | **1.18 ms** | **6.1 ms** |
| MBVH8 collapse | 0.56 ms | 2.6 ms | 17.3 ms |
| **MBVH8 wide refit** (no collapse) | **0.31 ms** | **2.4 ms** | 8.6 ms |
| **CWBVH compress** (what the shipped path re-emits) | **5.2 ms** | **18.0 ms** | **61.4 ms** |
| CWBVH blob size | 1.13 MB (69 B/tri) | 4.35 MB (66 B/tri) | 15.7 MB (60 B/tri) |

TLAS over one 64 k-triangle BLAS, `N` instances, **all of them moving every frame** (tinybvh TLAS: build, never refit —
its `Refit()` is a hard error on a TLAS):

| instances | instance rows updated (inverse 4×4 + 8 corners each) | TLAS rebuild | frame total |
|---|---|---|---|
| 256 | 0.012 ms | 0.158 ms | **0.17 ms** |
| 1 024 | 0.047 ms | 0.699 ms | **0.72 ms** |
| 4 096 | 0.207 ms | 2.87 ms | **3.07 ms** |

Read together, these say:

- **Rigid motion needs no BVH build at all.** 4 096 simultaneously moving objects cost ~3 ms of *CPU* TLAS work per
  frame on this machine — and that is the worst case (rebuild every frame, single-threaded pool); a fat-AABB
  incremental TLAS or the same rebuild on a worker thread while the frame renders makes it invisible. On a GPU a TLAS
  rebuild is a single AABB-LBVH pass; drivers do it in well under a millisecond.
- **Deformation is where the CPU stops scaling, and the reason is the packed layout, not the algebra.** Refitting is
  0.17 ms for a 16 k character; re-quantizing that character into the shipped CWBVH blob is 5.2 ms — 30× the refit.
  Four animated characters would already be 21 ms of CPU work per frame in the current format.
- **The fix is a layout decision, not a processor decision**: keep dynamic BLASes in a **wide, unquantized** layout
  (Aila–Laine `BVH4_GPU` 64 B nodes: float node box + quantized child boxes, or `BVH8_CPU`-style float bounds), where a
  bottom-up refit kernel updates node boxes in place — no re-collapse and no vertex re-quantization. Then the per-frame
  update of a 16 k character is one compute dispatch over its nodes, and the CPU only writes instance rows.
- Static geometry keeps the **compressed CWBVH**, which is the best traversal format available here and costs nothing
  per frame. Two formats, chosen by whether the mesh is ever going to move: compressed for the world, wide-refittable
  for actors. (Intel's DXR guidance says the same from the other side: keep BLASes updatable *only* where needed,
  because updatable structures cost memory and trace performance.)

## 4. Animated characters without a rig

The brief is explicit — "just the triangles, not rig". That is the *easier* case to schedule, because the renderer
never needs bone matrices: it receives a triangle soup that changed shape. Options, in order of what should be built:

1. **Per-frame vertex update + BLAS refit (the default).** Whatever produces the deformed vertices (baked vertex
   animation, morph targets, a skinning pass elsewhere, cloth), the renderer refits that mesh's BLAS. Topology must be
   fixed — same triangle count and connectivity — which is exactly what vertex animation and skinning give. Cost:
   §3's refit row, on the GPU as a node-update kernel, or 0.17 ms/char on the CPU for a handful of actors.
2. **Refit with a periodic rebuild.** Refit quality decays as the deformation grows (tree AABBs slacken), so the
   standard rule is *refit while the vertex displacement is small relative to primitive size, rebuild when it is not*.
   Practical form: track the maximum vertex displacement per mesh per frame; rebuild that BLAS when it exceeds a
   fraction (say 10 %) of the mesh's average primitive size, or every `N` frames regardless. A rebuild is 13 ms
   (CPU, 16 k) — affordable on a worker thread, not in the frame's critical path.
3. **Vertex-animation textures (VAT).** If animation is baked from the DCC as a texture of per-frame vertex positions,
   the same refit path serves it *and* the CPU mirror can reproduce it deterministically — the same property that made
   the ReSTIR proofs possible. This is the recommended authoring route for the `.geometry`/`.instance` formats when
   animation is added.
4. **Prebuilt BLAS per pose cluster (memory-heavy).** Reuse a cached BLAS for poses within a tolerance of the current
   one; trades memory for update time. Only worth it for hero assets on constrained hardware.
5. **Tetrahedral cage (research).** A 2026 ACM paper ("Ray Tracing Massive Amounts of Animated Geometry") animates a
   coarse tetrahedral cage and rebuilds the TLAS over the *tetrahedra* — two to three orders of magnitude fewer
   primitives than the triangles — decoupling per-frame cost from triangle density. Worth watching; it needs a custom
   intersection test, so it is not a first step.

What is *not* needed for any of these: a skeleton, skinning weights, or a rig in the renderer. The renderer consumes
deformed vertices and refits; the animation system owns everything before that boundary.

## 5. Where the GPU work actually goes, and what it requires of this codebase

- **A GPU builder must write our blob.** The traverser reads SSBO blobs in a fixed layout. A GPU builder that emits
  some other layout is useless unless `TraversalCWBVH.slang` grows a second traversal path — which is the real cost of
  H-PLOC/PLOC++/LBVH here, not the builder itself. Vertex data must also already be on the device (it is not: the CPU
  uploads flat triangles). So a GPU *build* is a bigger change than a GPU *refit*.
  **D9 answered this the way the paragraph asks.** `Engine/Shaders/BlasBuild.slang` emits the same packed layout — Morton
  key prepass, a per-level octant partition that is also the sort (no radix passes, no key prefix sum), a childBase scan
  in ascending slot order, an emit that quantises with the shared function, then the leaf runs — so `TraversalCWBVH.slang`
  keeps its single traversal path. `Engine/GeometricRaster/BlasBuildMirror.cpp` is the reference the kernels are
  transcribed from, and the build RULE was chosen by measurement rather than by taste: §⑨g walks three builds of the M10
  level over the same 12 000 rays and clocks them.

  | build rule                          | nodes  | build   | walk     | triangle tests | empty slots |
  |-------------------------------------|--------|---------|----------|----------------|-------------|
  | D9's first build (octant, no pack)  | 17 835 | 17.9 ms | 10.93 ms |         36 700 |       64.2 %|
  | clustered: count-balanced bins + SAH|  4 681 | 15.8 ms | 19.73 ms |        165 229 |       26.4 %|
  | **shipped: octant + the format rule**| **7 185** | **12.5 ms** | **10.11 ms** |     **63 462** |   **46.7 %**|
  | collapse: uniform 8-way (D9b)      |  4 681 | 12.1 ms | 24.72 ms |        165 229 |       26.4 %|

  ⚠️ One number in that table is honest about its own spread: the walk clock for the two octant rows has read 12.36/12.36,
  12.84/13.57 and 12.84/14.91 ms across runs on this 2-core box — a ~20 % swing that no best-of-five removes, because it
  is another process on the box rather than noise inside the walk. §⑨g therefore bounds that pair at 25 % (its first 10 %
  failed on the coin flip) and keeps the terms that are decisive: the halved arena, the faster build, and the clustered
  rule's ≥ 1.4× walk, which is outside the spread by a wide margin.

  **D9b closed the H-PLOC question by measuring its cheapest faithful form.** The merge the plan asked for was tried two
  ways. The first — count-balanced bins plus an SAH that merges neighbouring bins — walked 60–95 % slower, and the second
  (`Collapse`: the same cut with NO merge, i.e. the uniform 8-way collapse an H-PLOC build ends up with) produced the SAME
  STRUCTURE on this level: 4 681 nodes, 26.4 % empty slots, the same 165 229 triangle tests, the same 24.7 ms walk. So on
  the M10 level the SAH merge never fires — a uniform cut is already near the cut the search would choose — and the row
  above is what that buys: the fewest nodes and the least empty slots of any rule, at **2.4× the shipped rule's walk**.
  The conclusion the plan needed is therefore not "H-PLOC is untried" but **"in a wide-node format, a full node is not a
  faster node"**: filling the eight slots forces each child's box to be a union of slices the octant rule would have kept
  apart, and the traversal pays per ray that descends into it. The gap the plan worried about (46.7 % empty slots) is the
  price of tight boxes, and it is the cheaper side of the trade. Both alternatives stay in the mirror so this stays
  reproducible.

  The two rules a wide format turned out to need are about the FORMAT, not about clustering: (1) a range that fits one
  node's eight leaf slots — 24 triangles — becomes runs of three and is never recursed into; (2) past the Morton bits a
  range too large for that is cut into eight count-balanced pieces, not into up to 64 octant children for a node with 8
  slots. Clustering by count produced by far the fewest nodes and the least empty slots and walked 60–95 % SLOWER: merging
  bins that are far apart makes a child's box the union of the far parts, so rays the octant rule rejects at the box
  descend and test triangles. **A wide-node build is a box-tightness trade, not a node-count one** — and the counters
  alone did not show it; the clock did. The rejected rule stays in the mirror (`BlasPartition::Clustered`) so the
  comparison stays reproducible, and H-PLOC's actual merge (bottom-up, merging siblings that are already spatially close)
  is a different algorithm and remains the open experiment. What is still owed is a GPU.
- **A GPU refit is small, and layout-local.** Bottom-up node-box updates over the wide layout: one dispatch per level,
  no topology change, no vertex re-quantization. This is the piece to write first, because it is what deformation needs
  — and it is what `Engine/Shaders/BlasRefit.slang` is: the same quantiser the build calls, the same child-bounds rule
  D8's CPU sweep uses, dispatched deepest level first so a parallel pass reads only finished children. §⑨e proves the
  two orders agree byte for byte on the same blob (4 827 nodes, 31 927 triangles, 0 differing floats), which is what
  makes the transcription checkable without a device.
- **GPU builders are for topology changes** (destruction, LOD swaps, meshes authored on the fly): H-PLOC (AMD, GPUOpen,
  Benthin et al. 2024) constructs a whole BVH in a single kernel launch, 1.1–3.6× faster than PLOC++/ATRBVH, with
  LBVH-quality-competitive results (a million instances, 4-wide, in 2.21 ms). PLOC (Meister & Bittner 2018) / PLOC++
  are the well-understood fallbacks, and a student CUDA port measured 0.44–0.83 G triangles/s. Which one we take
  depends on whether the emitted layout can be the same wide layout the refit kernel already writes — that question
  should be answered *before* writing kernels, not after.
- ⚠️ **The measured D5 numbers stay the reason to do this at all**: 6.31 ms to re-emit a 16.8 k showroom, and it grows
  with the whole scene, for one moving ball.

## 6. Milestones

- **D6 — object-space BLAS + instance transforms in the kernel — DELIVERED (CPU half measured, GPU half audited).**
  `Engine/GeometricRaster/InstanceAcceleration.{h,cpp}` builds one object-space BLAS per prototype into ONE shared pair
  of CWBVH blobs, emits the device records (`TlasInstanceRecord` 112 B, `BlasPlacement` 16 B) and a CPU reference trace;
  `Engine/Shaders/TraversalRecords.slang` mirrors those records; `TraversalCWBVH.slang` gained
  `TraceBlasClosest/TraceBlasOccluded(…, NodeBase, LeafBase)` (the old `TraverseClosest/TraverseOccluded` are those with
  zero bases, ie the pre-D6 instructions) plus `TraverseInstancesClosest/TraverseInstancesOccluded`, which transform the
  ray into each candidate instance's object space and back out with the inverse-transpose for normals;
  `TraversalIndex::TraceClosestObjectSpace` is the walker those use (it does NOT normalise the direction, where
  `TraceClosest` does — t is then the caller's own parameterisation, which is what makes the transform free of any
  rescale). Evidence, all CPU-measured (`Exhibits/Workbench/Traversal/CheckTwoLevelBvh.sh`, 61/61 gates):
  - identity instance vs today's world-space tree: blobs **byte-identical** (FNV-1a), and **20 000/20 000 rays
    bit-identical** (10 162 hits, 9 838 misses) — an identity instance IS the old path;
  - 8 chunked identity instances (a different tree SHAPE): 20 000/20 000 rays agree, hits bit-identical;
  - a rigidly moved instance (rotate 30° · scale 1.25 · translate) vs D5's transformed-triangle rewrite: the derived
    world AABB equals the transformed soup's bounds **exactly**; of 10 088 hits, 10 087 hit the same triangle (2 257 of
    them bit-identical t) and 1 is a grazing ray resolving to the neighbouring triangle of a shared tessellation edge —
    0 unrelated surfaces, 0 hit/miss disagreements;
  - the kernel's payload, walked by an INDEPENDENT walker written from the uploaded layout: 10 086 hits + 9 914 misses
    bit-identical to the builder's own tree.
- **D7 — TLAS — DELIVERED (CPU measured).** *(Plan wording: build at load over instance AABBs; rebuild per frame when
  any instance row changed; fat-AABB incremental update later if the rebuild ever shows in a profile. )* `InstanceAcceleration::UpdateTopLevel` recomputes the instance AABBs from
  the stored object AABBs and rebuilds the top level (a TLAS is never refitted — tinybvh hard-errors on it), then emits
  the kernel's payload (8 floats a node, integer fields bit-cast) and the instance list. Measured on this 2-core host,
  **every** instance moving every frame: 256 instances **0.07 ms** (top level alone 0.05), 1 024 **0.32 ms** (0.23),
  4 096 **1.42 ms** (1.09) — and the BLAS blobs are hashed before and after the frame loop to prove the updates never
  touch them. ⚠️ The D7 gate in the plan ("4 096 moving instances under 1 ms") is met at 1 024 instances on this host
  and missed at 4 096 (1.42 ms), where the top-level rebuild is 77 % of the cost; the number to watch on the user's
  machine is the TLAS build, not the row update.
  Host side: `SwapchainExchange::UploadInstanceTraversal`/`RefreshInstanceTraversal` (bindings 27-30, capacity-checked,
  no reallocation, no descriptor rewrite), the dispatcher's set grew 28 → 32 with the bindless table moved 27 → 31 so it
  stays the highest binding, and the push block's last reserve slot is now `TlasInstanceCount` — the selector that makes
  the kernel walk the two-level pair (0 = the pre-D6 single-blob path, byte-identical). `GameExecution` builds and
  uploads the pair beside the world-space structure and, in the drop scene, moves **rows** per frame instead of
  rewriting the flat soup (D5 stands down while the two-level path is live, because that rewrite would corrupt the rest
  pose the BLASes were built from).
  ⚠️ **Not verified here:** the shader and the dispatcher cannot be compiled in this sandbox (no shader compiler, no
  Vulkan device). What IS verified is the wiring: the check script's §⑦ pins 43 exact strings across the shader, the
  dispatcher, the integrator and the project (bindings, the table's last-place rule, the push slot, the object-space
  call sites, the inverse-transpose arm, the payload's float order) and computes the record offsets the shader's std430
  layout would produce. Running the kernel is the user's GPU build.
- **D8 — the deformation path, refit in place — DELIVERED (CPU measured; the GPU half is text-pinned).**
  *(Plan wording: "refit kernel over the wide layout, plus the displacement-driven rebuild policy".)* The shipped answer
  keeps **one** layout: the packed CWBVH. `InstanceAcceleration::RefitBlas(uint32_t, const std::vector<TriangleIndex>&)`
  is the only D8 entry point, and it rewrites the leaf entries from the deformed soup (each entry's own primitive index
  is read back out of block 2's `.w`, so the blob is self-describing and needs no side table), refits the inner binary
  tree the CPU porter walks, re-quantises every node in ONE **descending sweep** of node indices, and refreshes the
  object AABB. A CWBVH node quantises its children into at most 255 cells of its own extent, so the re-quantisation
  cannot change the block count: the update is in place **by construction**, and the offsets the kernel was handed at
  upload stay valid. Evidence — `Exhibits/Workbench/Traversal/CheckTwoLevelBvh.sh` reports **116 passed / 0 failed**,
  §⑧ is the D8 section:
  - **a refitted BLAS answers exactly as the same geometry rebuilt**: 20 000 rays over the deformed level → 10 159
    bit-identical hits, 9 841 both miss, **0 unrelated surfaces, 0 hit/miss disagreements**;
  - **the packed blob still reaches every triangle of its own arena**: an independent walker written from the uploaded
    layout, against a brute force over the blob's own triangles with the same float Möller–Trumbore → 5 544 identical
    hits, 14 456 both miss, **0 unrelated, 0 hit/miss**. This is the statement that a re-quantised box never prunes its
    own geometry — the one failure mode a CPU trace over the binary tree cannot see;
  - **containment, without rays**: 31 927/31 927 leaf triangles lie inside their own quantised slot box (15 814 slots)
    and every interior slot covers its child (4 827 parents), worst slack 0;
  - **an untouched BLAS is untouched**: refitting BLAS 1 leaves BLAS 0's node and leaf slices byte-identical (FNV-1a),
    and the refitted BLAS' own node slice DOES change — the check is not vacuous;
  - **cost**, this 2-core host, 63 854 triangles with 31 927 in the refitted BLAS: in-place refit **4.39 ms** against
    **74.84 ms** for a full rebuild; the packed re-emit (refit + collapse + compress) is **7.76 ms** and moves the node
    count 24 135 → 25 190 blocks, i.e. it **no longer fits the recorded slice** (+1 055). In-place is not merely
    faster, it is the only update that can stay in an already-uploaded buffer. 4 827 nodes went through the sweep, whose
    largest child-index jump is 4 125 (`Metrics.MaxChildIndexJump`, `RefitSweepable`) — the number a GPU kernel needs to
    size its windows;
  - the gate deforms at **5.04 % of the mesh's own mean edge** (0.0031 m of 0.0611 m), the regime the policy calls
    Refit; `Frontier::MeasureDeformation` + `Frontier::BlasUpdatePolicy{RefitDisplacementRatio, RebuildCooldownFrames}`
    answer all seven policy cases, a topology change is refused **and counted** (`RefitRefusedCount`), and a
    spatial-split (HighQuality) BLAS refuses to refit — its splits cut triangles, so the D8 trade holds.
  ⚠️ **Not verified here:** the GPU half (a refit kernel; D9's build kernels). What is pinned instead is the layout
  invariant that makes those kernels possible (descending child indices) plus §⑦'s text pins, which keep
  `TraversalCWBVH.slang` in step with the records.

### What writing D8's instrument turned up (three traps, all now gated)

The packed blob had never been walked on the CPU: every earlier gate read it through tinybvh's own binary tree. An
independent walker was needed precisely because a re-quantisation that *shrank* a box is invisible to a walker that never
reads the boxes — and it cost three wrong instruments before it was trustworthy:

1. **The leaf hit mask is the meta's UNARY field (1/3/7), not a triangle count (1/2/3).** Shifting a decoded count sets
   bit 1 for a two-triangle leaf: the walker drops that leaf's first triangle and invents one past its last. It
   "disagreed with the blob's own geometry" on 23 % of rays while the blob was correct — the kernel shifts the
   *field* (`(meta4 >> 5) & 7`), which is why the GPU path was never wrong.
2. **SPIR-V's FMax/FMin drop a NaN operand; `std::min/max` propagate it.** A ray with an exactly zero direction
   component has rD = inf on that axis, so every quantised slab value along it is 0 · inf = NaN. Transcribed with
   `std::max`, the walker rejected *every* axis-aligned ray (22 % of the control's rays) while the kernel was right.
   This is also the mechanism behind the long-standing note in `TraversalIndex.cpp` that the library's own AVX CWBVH
   walker "returns misses the binary tree hits": MAXPS/MINPS return their second operand when one input is NaN. The
   gate now carries a dedicated axis-aligned census (166 exact, 5 834 misses, 0 disagreements).
3. **The oracle must share the walker's arithmetic.** Comparing the walker against the library's binary tree folds in a
   second intersection routine whose grazing verdicts legitimately differ; on this ray set that produced 4 643 apparent
   "hit/miss disagreements", which were almost all rays the library resolved in the scene's *other* BLAS (5 531 of its
   hits land in the walked BLAS, 4 664 in the one a single-BLAS walker cannot reach). The gate now compares the walker
   against a brute force over the blob's **own** triangles with the same float Möller–Trumbore — exact, no tolerance —
   and reports the cross-implementation census separately.

Also worth recording: the deformation magnitude decides what "disagree" means. Sized by the level's bounding box
(0.15 · diagonal = 2 749 % of a primitive), two *valid* tree shapes disagree on rays that graze a node boundary — 4
hit/miss, none adjudicated as a real miss. The gate therefore runs at 5 % of a primitive, what a skinned character
actually does frame to frame, and reports the level-scale wave as information only.

### What building it actually turned up (three defects, all now gated)

1. **The kernel applied the stored inverse TRANSPOSED.** `dot(Inv0.xyz, P) + Inv0.w` reads the record's *rows* out of
   the *columns* of a column-major inverse: correct for an identity (Iᵀ = I), correct for a pure translation (the
   translation of a column-major matrix is symmetric), wrong the moment an instance rotates. It cannot be caught by any
   layout or binding check. The fix lives in exactly one place now — `InstanceWorldToObject` /
   `InstanceWorldToObjectDirection` in `TraversalRecords.slang` — and gate ③c transcribes both forms into C++ and
   compares them against the CPU mirror, so a GPU is not needed to catch it (the transposed form is 5.99 m off on the
   M10 level's own geometry, 0.00 m for the corrected one).
2. **The row's transform must be RELATIVE, not absolute.** `SceneStructure::Finalise` bakes each instance's `World` into
   the flat soup, so a BLAS built over that soup already sits in the baked frame; carrying `World_now` as the row
   transform applies the bake twice. The row is `World_now · World_rest⁻¹` (`RelativeMatrix`), which is also the reason
   the drop scene looked fine — its rest transform is identity. Gate ③b builds a prototype over a *baked* soup and moves
   it with a non-identity relative transform, against a world tree over the moved soup.
3. **Descriptor pool vs layout, for the second time.** The pool's storage-buffer count was hand-kept and had gone stale:
   14 for a layout asking 18 (the GI reservoir pair 25/26 and D6/D7's four were never added). One driver let that
   through with a validation error; the next would not. Both the set layout and the pool are now derived from one
   `ComputeBindingType` table with `static_assert`s on the counts, and the four new descriptor writes are guarded on
   their buffers existing (a `VK_NULL_HANDLE` write is invalid, not merely useless).

Two further notes worth keeping:

- **The ray-to-object transform needs no re-normalisation.** `M·(O' + t·D') = O + t·D`, so `t` survives; the CPU walker
  (`TraceClosestObjectSpace`) takes the caller's direction verbatim, and the kernel divides twice (`rD' = 1/D'`). This is
  what makes an identity instance bit-identical rather than merely close.
- **The BLAS is quantised in object space.** Under a non-uniform bake-to-world scale the object-space tree is a
  *different, axis-sensitive* quantisation of the same geometry, so two trees can disagree by an ulp at a grazing hit
  (measured: one ray in 20 000, |det| ≈ 0.03 on that triangle). Both orders are legitimate; a disagreement is only a
  defect if the ray meets the triangle well inside it. The proof adjudicates every disagreement with a
  double-precision Möller–Trumbore oracle rather than assuming.

### Shader compilation is now a gate

`Tools/Build/CheckShaders.sh` lowers every entry of CMakeLists' `SHADER_TABLE` and fails on the first error. It exists
because two defects sat in `Engine/Shaders/` for a whole milestone: `flat` used as an identifier in
`ReSTIRViewport.slang` (a GLSL keyword — the `glslc` fallback path cannot compile that file, though the Slang path can),
and a `vec3 histUv = res.SelectedUv;` followed by a `vec4(histUv, w, depth)` (five components from two). Installing the
Vulkan SDK makes it a one-line pre-commit check; on a host with `slangc`/`glslc`/`glslangValidator` it runs as-is.

- **D8 — dynamic update path — DELIVERED, CPU half (see §6).** Rigid: nothing (D6/D7 already cover it). Deforming: an
  in-place refit of the packed layout plus the displacement-driven rebuild policy. Acceptance: a moving/deforming scene
  renders correct shadows and reflections with the frame budget printed, and the CPU mirror reproduces the same images.
  ⚠️ The GPU half of that acceptance sentence — the kernel-side refit and a rendered frame — is still owed.
- **D9 — GPU refit / GPU build kernels — WRITTEN, COMPILED, PINNED, MEASURED ON THE CPU, HOST AND VULKAN HALVES BUILT,
  READY TO RUN; NOT RUN HERE (no device).** `Engine/Shaders/BlasRefit.slang`
  (two stages: rewrite the leaf records from the deformed soup, then re-quantise one dispatch per level, deepest first)
  and `Engine/Shaders/BlasBuild.slang` (five stages: Morton prepass · per-level octant partition · childBase scan ·
  emit · leaf runs), both lowering through `Tools/Build/CheckShaders.sh` (15/15) and both pinned to the CPU mirror by
  §⑩ of the two-level gate (B1–B115: the layout bytes, the interior test, the exponent rounding, the unary counts, the
  rank rule, the block units of triangleBase, the CMake entries that keep them in the compile gate, and the host half).
  ⚠️ The build is the octant partition plus the format rule, not H-PLOC's cluster merge — and §5 has the measurement
  that says why the top-down clustering variant is not what to ship (it walks 60–95 % slower). What the shipped rule
  costs: 7 185 nodes for 63 854 triangles, 46.7 % of the wide slots empty, 3.9× fewer nodes and 14 % faster to build
  than D9's first attempt, at the same traversal work. The empty slots are the remaining quality gap, and closing it is
  an H-PLOC-shaped job (merge siblings that are already spatially close, bottom-up) rather than another split heuristic.
  **The host half is built and gated.** `Engine/GeometricRaster/BlasDevicePayload.{h,cpp}` carries what the two kernels
  are dispatched with, as types the CPU can build: the build's 48 B push block and the refit's 16 B one (`static_assert`s,
  pinned field for field to the shader text), the scratch size as ONE expression their Stride constants share
  (`8 + 24 × node slots`), the soup in the layout the kernels index (3 vec4 = 12 floats per triangle — §⑨h's first run
  caught that unit slip as a heap overflow), the level table the refit counts down (`PackBlasLevels` widens `LevelsOf`'s
  uint16 to the kernel's uint32 with 0xFFFFFFFF for a slot the tree cannot reach), and the **dispatch plan** — the host's
  loop as data: §⑨i checks 60 build dispatches (`prepass`, then `partition · count+scan · BLOCK SCAN · emit` per level —
  the block scan lands between the two stages it feeds — then the run path's three) under a level cap of 14 for the 8
  levels this level has, and 9 refit dispatches whose levels run 7,6,…,0, each exactly once and deepest first, because a
  repeated level would re-quantise a node from a child that moved. The plan's `Groups == 1`
  entries are the build's two single-workgroup stages, which is why §⑩ pins the guard lines that make them single-threaded
  — parallelise one and the plan has to change with it. (D9b then parallelised the other two, which is exactly the change
  that rule predicted: the plan's 4 dispatches a level, §⑨j's scan model, and the pins moved together.) **And the engine's
  source batch compiles the four D6–D9 TUs**
  (`InstanceAcceleration.cpp` had been called from `SwapchainExchange.cpp` since D6 with no target compiling it); each was
  compile-checked standalone under the engine's own include paths and flags before registration, which B76–B78 pin.
  **The Vulkan half is written, and the run is one command away.** `Engine/GeometricRaster/BlasBuildPipeline.{h,cpp}`
  creates the two pipelines (`BlasBuild.spv`, `BlasRefit.spv`), allocates one BLAS' worth of buffers on a host-visible
  memory type, records the plan's dispatches with a compute→compute barrier between every stage, and verifies the device's
  node blob, leaf blob, level table and leaf-triangle total against the CPU mirror's — byte for byte. It links no Vulkan
  loader: the entry points arrive through a `VulkanSourced` table, which is why the file compiles (and is compiled by the
  gate) in a sandbox with no `libvulkan.so` at all. `Exhibits/Workbench/Traversal/BlasDeviceRun.cpp` is the program — it
  opens the loader with `dlopen`, picks a device (discrete first), builds the M10 level's soup, runs the mirror for the
  expected answer, dispatches, and prints the comparison; `--refit` runs the refit path too, against a wave-deformed soup
  and a mirror refit of the same deformation, so "the device wrote something" cannot pass for "the device was right".

      bash Exhibits/Workbench/Traversal/RunBlasDevice.sh --refit        # 0 = device == mirror · 1 = mismatch · 2 = no device

  `RunBlasDevice.sh` lowers the shaders itself (through `SHADER_OUT`, the same recipe the compile gate uses — which is how
  a real bug surfaced: that gate had been writing `<name>.spv.spv` since it was written, a name nothing loads), compiles
  the runner with the Vulkan-Headers only, and reports a device-less machine as **SKIPPED**, not as a pass.
  ⚠️ What is still owed is the RUN, and nothing else: the kernels have never executed. Two stages of the build are still
  serial by design and say so where they are (the two block scans, one iteration per 128 node slots); everything else in
  the build is now parallel across the level, and §⑨j checks the parallel scan's arithmetic against the serial one over
  the shipped build's own 7 185 nodes.
- **D10 — ReSTIR integration** — *(delivered; see §7a below)*. The plan said the validation should use instance/primitive
  identity plus the previous transform. It does, with one measured correction: the identity is the INSTANCE, never the
  primitive (see the granularity note in §7a), and the previous transform is not a separate input — the motion vector
  already carries it, and for a moving object the motion has to be the surface point's displacement, not the camera's.

## 7. Decision record

| question | answer | why |
|---|---|---|
| CPU or GPU for the world build? | **CPU**, once, at load | deterministic, proven, and 148 ms for 256 k triangles is a load-time cost; no PCIe round trip |
| CPU or GPU for a rigid object move? | **Either**; CPU TLAS rebuild is ~0.2–3 ms and can hide on a worker | the work is instance AABBs, not triangles |
| CPU or GPU for deformation? | **GPU refit**, CPU for a few actors | D8 measured the CPU path end to end: 31 927 triangles refitted in 4.39 ms (the raw binary refit inside it is 0.22 ms per 16 k) against 74.84 ms to rebuild — but that is still a frame's worth of CPU on a big character, so the kernel is the destination and the CPU path is the mirror it is checked against |
| Which format for dynamic BLASes? | **the packed CWBVH, refitted in place** — *(D8 reversed this plan row)* | the plan said "wide, unquantized (refittable)" on the belief that "packed formats cannot be partially updated". They can: a node quantises its children into ≤ 255 cells of its own extent, so a re-quantise cannot change the block count, and the re-emit that would change it no longer fits the uploaded slice (+1 055 blocks measured). A second wide format would double the traversal code and the descriptor traffic for no measured win |
| Keep one world-space tree? | **No** — two-level from here on (D6/D7 delivered; the single-blob path stays as the fallback and as the bit-identity reference) | the whole-scene re-emit is the ceiling that blocks every dynamic feature |
| Must the object-space ray be re-normalised after `M⁻¹`? | **No** — transform O and D, take t as-is | `M·(O' + t·D') = O + t·(M·D')`, so t survives the transform; re-normalising would both rescale t and perturb grazing rays by an ulp |
| Animation without a rig? | per-frame deformed vertices + refit; VAT as the authoring route | the renderer never needs bone data; refit needs fixed topology, which skinning/VAT give |

## 7a. D10 — temporal integration for moving geometry *(delivered)*

The plan's one line was "identity-based validation is the new part". This is what was actually built, and the two things
measurement changed.

### What the accumulator had, and why it was not enough

Every history read in the renderer — the running mean in `ResolveSurface` (R7a) and both reservoir pools — is reprojected
through the R2 motion vectors and validated on the surface's normal and depth: cos 25°, 10 % relative depth. That rule is
necessary and it is not sufficient, because **normal and depth agree for two different objects more often than is
comfortable**:

- two objects that exchange places present the same normal and the same depth at the reprojected pixel;
- an object sliding across a coplanar neighbour *is* the same plane at the same depth;
- an object leaving a wall while another arrives to take its place does the same thing at a silhouette.

In all three cases the pixel inherits radiance shaded for something else, and the running mean carries it until the
sample count drowns it — ghosting, visible as a smear that follows the old object.

### The third fact

`kFeatureTemporalIdentity` (bit 9) makes the history record **which surface it came from**, and the read is accepted only
if that surface is the same one. Implementation notes:

| | |
|---|---|
| the identity | the packed value the visibility raster already carries, `instance << 14 \| primitive` (`SceneRecords.slang`'s own packing) — not a new number about the scene |
| where it is stored (mean) | `MomentImage`'s reserved `z/w` channels, which held `0.0` and nothing else. **Not a new binding**: bindings 0–31 of that set are all taken and 31 must stay the highest number (the bindless table is variable-count). Each half is a 16-bit integer, and every integer below 2²⁴ is exact in a float32, so the decoded identity compares with `==` like the integer it is |
| where it is stored (reservoirs) | a fifth `uvec4 Identity` in `GpuReservoir`: **64 B → 80 B**, mirrored by `ReservoirBufferRecord` on the host behind a `static_assert` that fails at compile time if the two ever drift |
| the GI/debug views | `SurfaceResolve.slang`'s `GpuReservoirView` carries the new tail too — an array stride is the whole struct, so a 64 B view over an 80 B buffer drifts one field every 80 bytes and the M/W/Age debug views would silently read a neighbouring pixel |
| whose rule it is | it is the SAME test in all three places (mean, DI pool, GI pool), because a ghost in the reflection half is the same bug wearing a different hat |
| default | **on**, with `--restir-no-identity` (mirror) / the bit clear (kernel) restoring the pre-D10 rule, so the fix is measurable rather than asserted |

### ⚠️ The granularity correction — the identity is the OBJECT, not the triangle

The first cut compared the whole packed pair, primitive included. Measured on a **still scene with a still camera**, that
restarted **13.5 % of the image every frame**. The raster jitters sub-pixel once per frame (one Halton offset, shared by
raster and resolve) and M10's swatch spheres carry ~1 500 triangles for ~10 pixels, so the neighbouring triangle is far
smaller than a pixel and the primitive flips constantly while the surface does not move at all. An identity that fine
does not measure "the same thing", it measures the *tessellation* — and it would have behaved completely differently on
a 4-triangle box than on a 4 000-triangle sphere.

The object is the right granularity, and it is the one the feature exists for: it survives a moving object's motion (so
the object keeps its own history as it moves — the point of the milestone), it differs between two objects that exchange
places (the ghost the rule catches), and it is the number the raster, the instance buffers and the acceleration
structure all already agree on. *Same object, different face* is the normal/depth test's business. On the same still
scene the rule now changes 0.5 % of pixels instead of 13.5 %.

### ⚠️ The second measurement — the mirror's motion vector had the jitter in it

Once the rule was at the right granularity, the pool still refused ~1 275 merges per frame on a still scene, and the
diagnostic said why: the mirror measured the surface point's displacement from the pixel **centre** while the G-buffer's
hit point came from the **jittered** ray. Motion is a displacement of a POINT, so mixing the two left a sub-pixel offset
in every reprojection, and `floor()` turned half of them into a neighbouring pixel's history. The kernel does not have
this bug — its motion image carries a *vertex's* own screen displacement — so this is the mirror being brought into line
with it: the motion is measured from the ray the pixel actually took, and the read address is

    prevPx = floor((pixelCentre − motion) · extent)

which is the kernel's own `cuv - motion`. Refusals on a still scene fell from 1 275 to ~40 (the genuine silhouettes).

**Both bugs were found by measurement, not by review** — which is the argument for the gate below existing.

### What is measured

`Exhibits/Workbench/Materials/CheckTemporalIdentity.sh` (its own gate; the exhibit's `RunRestirViewport.sh` grew two
matching panels for the kept sheet):

1. **The shadow follows the object.** The moving object's shadow is tested against the renderer's own intersector: a
   96 × 96 grid of floor points, each shot sun-ward with the kernel's `Intersect`, and the subset whose FIRST blocker is
   the moving object *is* its shadow; measured at the rest pose and at the peak of the excursion. Both cast one
   (865 / 1 072 points) and the footprints DIFFER (307 points changed state, centroid 0.091 m) — the direction-agnostic
   statement of "the shadow moved with it". The mirror re-poses the geometry *and rebuilds the acceleration structure*
   each frame, so primary rays and shadow rays see the object where it actually is; a moving object with a lagging
   shadow is the other half of this acceptance.
2. **The ghost is refused, and the picture shows it.** One swatch slides along its own plane, out and back, so the closing
   frame's scene is the REST scene — which makes a plain render of the untouched level a ground truth for it. The same
   sequence runs with and without the rule: **891 vs 1 732 RMSE** against that ground truth (0.0136 vs 0.0264
   normalised), i.e. the reads the identity refuses carry about **twice** the error the pre-D10 rule leaves in the
   picture. Per frame, ~770 history reads (145 mean + 560 DI + 64 GI) agreed on normal and depth and belonged to a
   different object.
3. **A still scene is not disturbed** (the granularity lesson, as a check): with no motion at all the two arms agree to
   0.5 % of pixels.

### What is still owed

The kernel side is compile-checked and lowers (`Tools/Build/CheckShaders.sh`, 15/15) and the host record's stride is
static-asserted, but **no frame has been dispatched**: the identity path wants a GPU run on the owner's card, exactly
like the rest of the D9/D10 device work in §5–§6. The mirror is the kernel's algorithm with the kernel's constants, not
the kernel itself.

// BlasBuildMirror — D9's CPU mirror of the GPU refit/build kernels.
//
// The GPU path cannot be compiled or run in this sandbox (no Vulkan device), so D9 delivers it the way D6/D7 were
//    delivered: the algorithm is implemented here as the reference the kernels are transcribed from, and the kernels
//    themselves are text-pinned against it (`Engine/Shaders/BlasRefit.slang`, `Engine/Shaders/BlasBuild.slang`, and the
//    §⑨ pins in `Exhibits/Workbench/Traversal/GpuBlasBuildProof.cpp`). What that buys is not "the kernel works" — it
//    is that every invariant the kernel depends on is measured, and that the exact same arithmetic, in the exact same
//    order, is known to produce a structure the D8 walker can walk.
//
// Why a mirror instead of "just call tinybvh on the CPU": the GPU kernels exist to do what tinybvh does TODAY, on the
//    device, with the frame's own data — a host build of a deforming character is 74.8 ms (D8's measurement), which is
//    a frame's worth of CPU. The mirror therefore implements the GPU algorithm (H-PLOC-style Morton build; level-order
//    in-place re-quantise) rather than wrapping the library's SAH builder.
//
// The two algorithms, and the invariants each one rests on:
//
//   BUILD (topology changed — `BuildHPloc`)
//     1. Morton code each triangle from its centroid, normalised into the object AABB (30 bits, 10 per axis).
//     2. Sort by code (LSD radix, 8 bits × 4 passes here; the kernel does the same in shared memory).
//     3. Partition the sorted range TOP-DOWN into the wide tree. A node covers a Morton prefix and its eight child
//        slots are the eight prefixes one octant deeper — so a child's SLOT IS ITS OCTANT, which is exactly what the
//        traversal's octant-permuted hit mask assumes. A slot holding 1–3 triangles is a leaf; more recurses.
//     4. Emit every node's own AABB bottom-up (exact, from the children), then the packed layout: the node's
//        `p`/exponents and each slot's quantised box, with the leaf triangle records (e1 = v2−v0, e2 = v1−v0, v0,
//        primitive index in .w — the format the kernel's Möller–Trumbore reads).
//
//   REFIT (fixed topology, moved vertices — `RefitLevelOrder`)
//     * Recompute each leaf slot's exact bounds from the deformed soup, then re-quantise in place. The re-quantisation
//       takes each child's DECODED box as its input (not the child's exact bounds), so the parent covers what the
//       traversal will actually test — that is what makes the update conservative in one pass.
//     * Order: every interior child sits at a HIGHER node index than its parent, so processing levels deepest-first
//       (or indices descending) visits children before parents. Levels come from `LevelsOf`, an O(nodes) BFS over the
//       packed child pointers — the D9 kernel uploads the same table and runs one dispatch per level, because the
//       alternative (a descending window sweep) needs a window at least as wide as the largest child-index jump, which
//       D8 measured at 4 125 on this level: one window would be the whole arena, so levels are the usable independent
//       sets. ⚠️ Both orderings are safe for the same reason and neither changes a single output bit; the level table
//       is what a kernel can schedule.
//
// ⚠️ A node's block count cannot change under a refit (≤ 255 quantised cells of its own extent), so both paths write
//    in place. `BuildHPloc` allocates a fresh pair of blobs: that is the double-buffer the frame graph swaps.
#pragma once

#include <cstdint>
#include <vector>

#include "SwapchainExchange.h"   // TriangleIndex

namespace Frontier
{
    struct BlasBuildMirrorMetrics
    {
        uint32_t TriangleCount = 0u;   // [cnt] input triangles
        uint32_t NodeCount     = 0u;   // [cnt] 8-wide nodes emitted
        uint32_t LeafSlots     = 0u;   // [cnt] slots holding triangles
        uint32_t MaxLevel      = 0u;   // [cnt] deepest BFS level (0 = the root)
        uint32_t NodeBlocks    = 0u;   // [blk] float4 blocks in the node blob (5 per node)
        uint32_t LeafBlocks    = 0u;   // [blk] float4 blocks in the leaf blob (3 per triangle)
        uint32_t EmptySlots    = 0u;   // [cnt] slots no child claimed   // [cnt] slots a node did not use (format allows them; a build should not need them
                                       //       except where the octant partition ran out of Morton bits)
        float    BuildMilliseconds = 0.0f;   // [ms] the whole build
        float    RefitMilliseconds = 0.0f;   // [ms] the last RefitLevelOrder
        uint32_t LastRefitNodeCount = 0u;    // [cnt] nodes re-quantised in place
        uint32_t LastRefitTriangleCount = 0u;// [cnt] leaf records rewritten from the deformed soup
    };

    // How a node's Morton range becomes children (`BuildHPloc`'s `Partition`). Both rules cut the range into
    //    CONTIGUOUS slices, so locality, the arena order and the leaf-run layout are identical either way; what differs
    //    is where the cuts go, and §⑨g of the two-level gate walks both over the same rays and clocks them.
    //
    //    ⚠️ `Clustered` is the rule that looked right and measured WRONG, so it is kept in the tree as a measured
    //    alternative rather than deleted. On the M10 level (63 854 triangles, 12 000 rays, best of three):
    //
    //      | rule                   | nodes | build  | walk   | triangle tests |
    //      |------------------------|-------|--------|--------|----------------|
    //      | Clustered (count bins) |  4 681| 16.6 ms| 19.63 ms |     165 229     |
    //      | Octant, no pack (v1)   | 17 835| 14.9 ms| 12.24 ms |      36 700     |
    //      | Octant + pack (SHIPPED)|  7 185| 12.7 ms| 11.78 ms |      63 462     |
    //
    //    The clustering cut the node count by 3.8× and the empty slots to 26 %, and walked 61 % SLOWER: merging equal-count
    //    bins into a child makes that child's box the union of bins that are spatially far apart, so a ray that the octant
    //    rule rejects at the box now descends and tests triangles. Wide-node builds are a box-tightness trade, not a
    //    node-count one, and the counters are not enough to see it — the clock is (§⑨g reports both).
    enum class BlasPartition
    {
        // The tree mirrors the Morton octree: every occupied octant becomes a child. This is what ships, with the format
        //    rules below (a range of ≤ 24 triangles becomes leaf runs and is never recursed into; at the end of the
        //    Morton bits a too-large range is cut into eight count-balanced pieces, because per-octant splitting there
        //    could ask for up to 64 children and a node has eight slots). Against D9's first build it is 2.5× fewer nodes,
        //    14 % faster to build and 3.9 % faster to walk.
        Octant,
        // Eight COUNT-BALANCED bins, then the SAH keeps whichever of the seven boundaries pay for themselves (merging
        //    neighbours where the merged box is cheaper than the extra child). Measured slower than `Octant` above, and
        //    the merge is what does it: a merged child's box is the union of two bins' worth of space, and the traversal
        //    pays per ray that descends into it. `Collapse` below is the same cut WITHOUT the merge, which separates the
        //    two effects this rule conflated.
        Clustered,
        // The COLLAPSE step of an H-PLOC-shaped build, in its cheapest faithful form: at every level the range is cut into
        //    exactly eight count-balanced pieces, so a node has eight children by construction and the wide format's slots
        //    are full — no top-down merge, no nearest-neighbour search, just the eight-way collapse of the Morton order.
        //    This is the rule that attacks the one quality gap the shipped rule admits to (§⑨g: 46.7 % of the slots empty,
        //    because an octant split yields however many octants a range happens to occupy). Whether full slots at every
        //    level pay for the wider boxes a uniform cut produces is a question for the same walk measurement, and the
        //    answer is in the header's table.
        Collapse
    };

    class BlasBuildMirror
    {
    public:
        // The packed layout this mirror emits and reads: 5 float4 per node, 3 per triangle. Stated here because the
        //    kernels index with the same constants and a mismatch is silent (the node's block stride is what the
        //    traversal multiplies child indices by).
        // The Morton code is 30 bits, 3 per level, and the device kernel's kBlasMortonDepth is the same 10 — §⑩ pins
        //    the pair, because "past the last level" is where the octant read has to be guarded (B49).
        static constexpr uint32_t kMortonDepth   = 10u;
        static constexpr uint32_t kLeafSlots     = 8u;   // the wide node's slot count — the build's fan-out cap
        static constexpr uint32_t kNodeBlocks = 5u;
        static constexpr uint32_t kTriBlocks  = 3u;
        static constexpr uint32_t kMaxTrianglesPerLeaf = 3u;

        // ── H-PLOC-style build into the packed layout. Triangles are consumed in the order given (the primitive index
        //    written into each leaf record is the index into `Triangles`, so the caller's mapping survives).
        //    `PackFittedRanges` is a rule about the FORMAT rather than about the split: eight leaf slots hold 24
        //    triangles, so a range of at most 24 is emitted as runs of three and never recursed into. The only
        //    configuration that turns it off is `Octant` + false, which reproduces D9's first build for §⑨g.
        [[nodiscard]] static bool BuildHPloc(const std::vector<TriangleIndex>& Triangles,
                                             std::vector<float>& OutNodes, std::vector<float>& OutLeaves,
                                             BlasBuildMirrorMetrics& OutMetrics,
                                             BlasPartition Partition = BlasPartition::Octant,
                                             bool PackFittedRanges = true) noexcept;

        // ── The refit: same packed blobs, moved vertices, no change to the topology. `Levels` is LevelsOf()'s output
        //    (size = NodeBlocks / 5). Leaves are rewritten from `Triangles` (indexed by each record's own primitive
        //    index) and every node is re-quantised in place, deepest level first.
        [[nodiscard]] static bool RefitLevelOrder(std::vector<float>& Nodes, uint32_t NodeOffset,
                                                  std::vector<float>& Leaves, uint32_t LeafOffset,
                                                  const std::vector<TriangleIndex>& Triangles,
                                                  const std::vector<uint16_t>& Levels,
                                                  BlasBuildMirrorMetrics& OutMetrics) noexcept;

        // ── Per-node BFS level over the packed child pointers. The GPU refit kernel needs it as a table (one dispatch
        //    per level); the host can compute it in the same pass that measures child-index jumps.
        static void LevelsOf(const std::vector<float>& Nodes, uint32_t NodeOffset, uint32_t NodeBlocks,
                             std::vector<uint16_t>& OutLevels, uint32_t& OutMaxLevel) noexcept;

        // ── The shared quantiser: one node's `p`, per-axis exponents and eight slot boxes, from the slot bounds the
        //    caller supplies. Used by the build (exact child bounds) and by the refit (the children's decoded boxes),
        //    which is the point — one implementation, two callers, no drift between build and update.
        static void QuantiseNode(float* Node, const float SlotMin[8][3], const float SlotMax[8][3],
                                 const bool SlotPresent[8]) noexcept;

        // ── Slot accessors the kernels mirror (see the layout note in InstanceAcceleration.cpp).
        // ── D9b: the build kernel's two-step scan, as the CPU model of it ────────────────────────────────────────────
        //    The kernel could not be run here, so the part of it that is arithmetic rather than layout is modelled and
        //    checked numerically: the same counts through the same block width must produce the same childBase the
        //    serial construction produced. The three lines that matter are in BlasBuild.slang's stages 2 and 4 — the
        //    within-block prefix, the block total, and the block prefix — and §⑨j runs this model over the SHIPPED
        //    build's own interior counts (§⑩ pins the three lines, so the model and the kernel cannot drift apart).
        //    `Counts` are one level's interior-child counts, in node order; `OutBases` gets one childBase per count.
        //    Returns false if the block sums would run past `BlockSumsWords` entries.
        [[nodiscard]] static bool ScanLevelChildBases(const std::vector<uint32_t>& Counts, uint32_t TileStart,
                                                      uint32_t BlockWidth, uint32_t BlockSumsWords,
                                                      std::vector<uint32_t>& OutBases) noexcept;

        static bool  SlotIsInterior(const float* Node, uint32_t Slot) noexcept;
        static uint32_t SlotTriangleRun(const float* Node, uint32_t Slot, uint32_t& OutFirst) noexcept;
        static uint32_t InteriorChildIndex(const float* Node, uint32_t Slot) noexcept;
        static void  SlotBox(const float* Node, uint32_t Slot, float Lo[3], float Hi[3]) noexcept;
    };
}

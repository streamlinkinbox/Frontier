// BlasDevicePayload — the host-side contract of D9's two compute kernels.
//
// The kernels cannot be run in this sandbox (there is no Vulkan device), so the piece of the GPU path that CAN be built
//    and gated here is the interface: what the host must hand `BlasBuild.slang` and `BlasRefit.slang`, in the exact
//    shapes their SSBOs and push constants index. Every field below has a counterpart in the shader text, and §⑩ of
//    Exhibits/Workbench/Traversal/TwoLevelBvhProof.cpp pins them against each other — a push block that drifted by one
//    vec4 is the kind of mistake that shows up as a wrong tree on a device and nowhere else.
//
// What the host still has to do with this (owed with the GPU run, see Docs/DynamicGeometry.md §5):
//   · create the buffers and copy these payloads into them (the node/leaf blobs already exist: they are the same CWBVH
//     blobs the traversal reads);
//   · create one descriptor set per kernel from the bindings listed in the shader headers, and one compute pipeline;
//   · dispatch in the order the stages require (build: prepass → for each level {partition, scan, emit} → runs; refit:
//     leaves → one dispatch per level, deepest first), with a compute-write → compute-read barrier between stages and
//     before the traversal that consumes the blobs.
#pragma once

#include <cstdint>
#include <vector>

#include "SwapchainExchange.h"       // TriangleIndex
#include "InstanceAcceleration.h"    // QueryNodeBlob / QueryLeafBlob / QueryBlasObjectAabb
#include "BlasBuildMirror.h"         // kMortonDepth / kLeafSlots / kMaxTrianglesPerLeaf — the layout's own numbers

namespace Frontier
{
    // ── BlasBuild.slang's push block: four uints then two vec4 — 48 B, and std430 has nothing to reorder ─────────────
    //    The field ORDER is the shader's declaration order. `ObjectMin`/`ObjectExtent` are the object AABB the Morton
    //    codes are normalised against; the device must not recompute it (InstanceAcceleration::QueryBlasObjectAabb has
    //    it already, and a second reduction is a second answer).
    struct BlasBuildConstants
    {
        uint32_t Stage         = 0u;   // [-]   0 prepass · 1 partition · 2 scan · 3 emit · 4 runs
        uint32_t Level         = 0u;   // [lvl] the level being partitioned / emitted
        uint32_t TriangleCount = 0u;   // [cnt] triangles in the soup and the leaf arena
        uint32_t NodeCount     = 0u;   // [cnt] node slots the arena may use (a cap, not a reservation)
        float    ObjectMin[4]    = { 0.0f, 0.0f, 0.0f, 0.0f };   // [m]
        float    ObjectExtent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // [m] a zero axis pins that axis' key bits to 0
    };
    static_assert(sizeof(BlasBuildConstants) == 48u,
                  "BlasBuild.slang's push block is four uints then two 16-byte-aligned vec4: 48 B, not the 64 a "
                  "fourth uint would make it");

    // ── BlasRefit.slang's push block: four uints ─────────────────────────────────────────────────────────────────────
    struct BlasRefitConstants
    {
        uint32_t BlasIndex = 0u;   // [idx] index into the placements array: picks the node and leaf ranges
        uint32_t Stage     = 0u;   // [-]   0 = rewrite the leaf triangles, 1 = re-quantise one level's nodes
        uint32_t Level     = 0u;   // [lvl] stage 1: which BFS level (the host counts down from the deepest)
        uint32_t Pad       = 0u;   // [-]   0
    };
    static_assert(sizeof(BlasRefitConstants) == 16u, "the refit kernel's push block is four uints");

    // ── BlasBuild.slang's scratch: a header, then one block per node slot ────────────────────────────────────────────
    //    These two numbers are the shader's `kBlasScratchHeader` / `kBlasScratchStride`; §⑩ pins the pair on both sides,
    //    so a host that sizes the buffer with one and a kernel that indexes with the other cannot pass unnoticed.
    inline constexpr uint32_t kBlasScratchHeader = 8u;    // [u32] the two level pairs, the triangle total, reserved
    inline constexpr uint32_t kBlasScratchStride = 24u;   // [u32] per node slot: child ranges, own range, counts, slot maps
    inline constexpr uint32_t kBlasBuildLocalSize = 128u; // [-] BlasBuild.slang's local_size_x — as forward-declared,
                                                          //     because the scratch sizing below is written in it
    [[nodiscard]] inline uint32_t BlasGroupCountStub(uint32_t Items, uint32_t LocalSize) noexcept
    {
        return Items / LocalSize + ((Items % LocalSize) != 0u ? 1u : 0u);
    }

    // The scratch the build kernel needs for an arena of `NodeSlots` node slots — the same expression the kernel indexes
    //    with, so the host cannot under-allocate it.
    inline uint32_t BlasBuildScratchWords(uint32_t NodeSlots) noexcept
    {
        return kBlasScratchHeader + kBlasScratchStride * NodeSlots;
    }

    // The build's SCAN scratch: one uint per workgroup of the dispatch, because each workgroup scans 128 node slots and
    //    publishes its total (binding 7, `BlasBlockSums`). ⚠️ The 128 is `kBlasBuildLocalSize` and the block width at
    //    once — change one and this expression changes with it, which is why it is written here rather than at the
    //    allocation site.
    inline uint32_t BlasBlockSumsWords(uint32_t NodeSlots) noexcept
    {
        return BlasGroupCountStub(NodeSlots, kBlasBuildLocalSize);
    }

    // ── the soup: three vec4 per triangle, in primitive order ────────────────────────────────────────────────────────
    //    BlasBuild.slang reads BlasSoup[3·primitive + {0,1,2}] as v0, v1, v2 — a vec4 array, vertex .xyz with a free .w.
    //    The deformed soup the refit kernel reads is the SAME layout; that sharing is the reason one builder serves both.
    //
    //    ⚠️ Three units, and the slip between them is a heap overflow rather than a wrong picture: the kernels index VEC4S
    //       (3 per triangle), a std430 vec4 is 16 B = 4 FLOATS, and a VkBuffer size is in bytes. §⑨h's first run wrote 12
    //       floats per triangle into a buffer aliased as 3 — sized with the vec4 count and indexed with the float count.
    inline uint32_t BlasSoupVec4s(uint32_t TriangleCount) noexcept { return TriangleCount * 3u; }    // [vec4]
    inline uint32_t BlasSoupFloats(uint32_t TriangleCount) noexcept { return TriangleCount * 12u; }  // [flt]

    [[nodiscard]] bool PackBlasSoup(const std::vector<TriangleIndex>& Triangles, std::vector<float>& OutSoup) noexcept;

    // ── the level table: one BFS level per node slot, 0xFFFFFFFF for a slot the tree cannot reach ────────────────────
    //    BlasBuildMirror::LevelsOf produces uint16 levels with 0xFFFF meaning unreachable; the kernel's table is uint32,
    //    so the widening lives here rather than in a shader cast. `OutMaxLevel` is the deepest level present (0 = root),
    //    which is what tells the refit's dispatch loop how many times to run.
    [[nodiscard]] bool PackBlasLevels(const std::vector<float>& Nodes, uint32_t NodeOffset, uint32_t NodeBlocks,
                                      std::vector<uint32_t>& OutLevels, uint32_t& OutMaxLevel) noexcept;

    // ── everything one build of one BLAS needs, except the blobs themselves ─────────────────────────────────────────
    //    Not a buffer: the sizes a caller must allocate and the values it must push. The node and leaf blobs stay the
    //    traversal's (bindings 8/9 on the two-level path), so this deliberately does not copy them.
    struct BlasBuildPayload
    {
        std::vector<float> Soup;                 // [flt] BlasSoupFloats() — 3 vec4 = 12 floats per triangle
        uint32_t           TriangleCount = 0u;   // [cnt]
        uint32_t           NodeSlots     = 0u;   // [cnt] node slots the caller allocated (blocks / 5)
        uint32_t           ScratchWords  = 0u;   // [u32] derived: BlasBuildScratchWords(NodeSlots)
        uint32_t           SortedWords   = 0u;   // [uvec2] EACH of the ping/pong arrays: TriangleCount entries
        float              ObjectMin[4]    = { 0.0f, 0.0f, 0.0f, 0.0f };   // [m]
        float              ObjectExtent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // [m]

        // Every derived size, re-derived: a payload that fails this is one the kernels would index past.
        [[nodiscard]] bool SizesAgree() const noexcept
        {
            return TriangleCount > 0u && NodeSlots > 0u &&
                   Soup.size() == size_t(BlasSoupFloats(TriangleCount)) &&
                   SortedWords == TriangleCount &&
                   ScratchWords == BlasBuildScratchWords(NodeSlots);
        }

        // The arena bound: a node's eight leaf slots hold at least one triangle each, so a build can never need more than
        //    one node per triangle (and in practice it converges far below that — 7 185 nodes for 63 854 triangles).
        [[nodiscard]] static uint32_t NodeSlotBound(uint32_t TriangleCount) noexcept { return TriangleCount; }
    };

    // Fills every field of `Out` from the host's own records: the soup, the object AABB, and the derived sizes. Returns
    //    false rather than handing a kernel an under-sized buffer.
    [[nodiscard]] bool BuildBlasBuildPayload(const std::vector<TriangleIndex>& Triangles, const float ObjectMin[3],
                                             const float ObjectMax[3], uint32_t NodeSlots,
                                             BlasBuildPayload& Out) noexcept;

    // ── the DISPATCH PLAN: the host's loop, as data ─────────────────────────────────────────────────────────────────
    //    What is left of "host wiring" once the payload exists is the sequencing: which stage runs when, over how many
    //    groups, and where the barriers go. That is the part a device session would otherwise discover by trial, so it is
    //    written here as a list and checked by §⑨i against the numbers §⑨h/§⑨g measured. The plan holds no Vulkan types
    //    on purpose — the caller turns each entry into a vkCmdDispatch between two compute→compute barriers.
    //
    //    The single-workgroup stages (build's scan and run assignment) are single-threaded global passes: the kernels say
    //    so where they guard on `gl_LocalInvocationID.x != 0u || gl_WorkGroupID.x != 0u`, which is also why the plan gives
    //    them one group. That is a first-cut choice the shader headers name as the first thing to parallelise later.
    inline constexpr uint32_t kBlasRefitLocalSize = 64u;    // [-] BlasRefit.slang's local_size_x

    struct BlasDispatch
    {
        uint32_t    Stage  = 0u;   // [-]   the kernel's Stage constant for this dispatch
        uint32_t    Level  = 0u;   // [lvl] the kernel's Level constant (0 where the stage ignores it)
        uint32_t    Groups = 0u;   // [cnt] groupCountX — local size is 1D and comes from the constants above
        const char* Note   = "";   // [-]   what this dispatch is for, for the log
    };

    [[nodiscard]] inline uint32_t BlasGroupCount(uint32_t Items, uint32_t LocalSize) noexcept
    {
        return Items / LocalSize + ((Items % LocalSize) != 0u ? 1u : 0u);
    }

    // How many levels a build can need, without reading anything back. The Morton path contributes at most kMortonDepth
    //    levels; past it every level cuts a range into kLeafSlots pieces, so a range shrinks 8× per level until rule 1
    //    (≤ 24 triangles for eight leaf slots of three) ends it. The kernels no-op on a level whose node count is 0 —
    //    the guards test the scratch header — so a bound is enough for the loop, and a runtime loop may stop earlier
    //    once the header's node count reads back as 0.
    [[nodiscard]] inline uint32_t BlasBuildLevelCap(uint32_t TriangleCount) noexcept
    {
        if (TriangleCount == 0u) return 0u;
        constexpr uint32_t kTrianglesPerNode = BlasBuildMirror::kLeafSlots * BlasBuildMirror::kMaxTrianglesPerLeaf;
        uint32_t Range = (TriangleCount + kTrianglesPerNode - 1u) / kTrianglesPerNode;
        uint32_t Extra = 0u;
        while (Range > 1u) { Range = (Range + BlasBuildMirror::kLeafSlots - 1u) / BlasBuildMirror::kLeafSlots; ++Extra; }
        return BlasBuildMirror::kMortonDepth + Extra;
    }

    // The build's dispatches, in the order the stages require — ⚠️ the order matters, because some of these stages
    //    consume what the previous one wrote and only two of them may be reordered freely:
    //    0 prepass (one thread per triangle)
    //    for each level 0 .. cap-1:  1 partition (one group per 128 node slots — the kernel early-outs past the level's
    //                                own node count) · 2 count+scan (same grouping; writes the within-block prefixes AND
    //                                the per-block totals) · 4 block scan (ONE workgroup, over the block totals; it
    //                                publishes the NEXT level's base/count, so it must run AFTER 2 and BEFORE 3) ·
    //                                3 emit (same grouping as 1 and 2; it adds the two scan halves back together)
    //    5 runs count+scan (over the whole arena) · 6 runs block scan (ONE workgroup) · 7 runs emit (over the arena)
    //    The level parity in the partition comes from `Level & 1`: the ping and the pong swap every level, so the loop
    //    must be strictly ascending from 0 with no gaps — losing a level would read the wrong array. The header's own
    //    level pair ping-pongs on the same parity, which is why 4 can write the next level's pair while 3 still reads
    //    the current one.
    [[nodiscard]] bool BuildBlasDispatchPlan(uint32_t TriangleCount, uint32_t NodeSlots,
                                             std::vector<BlasDispatch>& Out) noexcept;

    // The refit's dispatches for ONE BLAS: stage 0 leaves (one thread per triangle record), then one dispatch per level
    //    from `MaxLevel` DOWN TO 0 — children are always at a higher node index than their parent, so a level's nodes may
    //    only be re-quantised after everything below them is done. `MaxLevel` is what PackBlasLevels returns; a level the
    //    BLAS has no nodes in is a no-op dispatch rather than a hole in the table.
    //    One plan is one BLAS: `BlasIndex` is the caller's push constant on every entry rather than a field here, so the
    //    plan stays a statement about the kernels' own work.
    [[nodiscard]] bool BuildBlasRefitPlan(uint32_t PrimitiveCount, uint32_t NodeCount,
                                          uint32_t MaxLevel, std::vector<BlasDispatch>& Out) noexcept;
}

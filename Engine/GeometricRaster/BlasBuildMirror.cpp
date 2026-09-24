//============================================================================================================================================
//  📦 Engine/GeometricRaster/BlasBuildMirror.cpp — D9's CPU mirror of the GPU refit / build kernels
//============================================================================================================================================
// See BlasBuildMirror.h for the algorithm and the invariants. This file is the reference the kernels are transcribed
//    from and the thing the §⑨ gate measures:
//
//      RefitLevelOrder — the kernel's decomposition of D8's in-place refit. The gate asserts BYTE IDENTITY against
//                        RefitBlas, so a kernel written from this file cannot drift from the gated host path.
//      BuildHPloc      — a full build into the same packed layout, for what a refit cannot cover (topology change).
//
// The one convention both live on, and the one that had to be measured rather than assumed:
//    a node's children occupy CONSECUTIVE arena slots in ASCENDING STORED-SLOT order — the first interior child at
//    childBase, the next at childBase + 1 — because the traversal derives a child's arena index as
//        childBase + popcount( IMASK & bits-below-the-stored-slot )
//    where IMASK is the node group the traversal built out of the node's own `imask` byte OR'd into bits 0..7. Every bit
//    of that byte sits below every stored slot (slots are 0..7, the hit bits live at 24..31), so the popcount is exactly
//    "how many of this node's interior children sit at a lower stored slot" — the child's rank in stored-slot order,
//    independent of the ray octant and of which children the ray hit. The hit bits only order the pops. Getting this
//    wrong is silent, and it is the trap D9 hit: ranking over the octant-permuted slot instead of the stored slot loses
//    2 426 of the 3 108 hits the same level contains (measured, §⑨).
//
// The second convention: a node's box is the union of its children's, so a ray that enters it passes through at least
//    one child's box. That is what keeps the traversal's "no hit children" fallback branch unreachable with a non-empty
//    mask, and it is why the leaf children's triangle bits survive (they are tested at the moment the node is decoded).
#include "BlasBuildMirror.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier
{
namespace
{
    constexpr uint32_t kNodeBlocks = BlasBuildMirror::kNodeBlocks;   // 5
    constexpr uint32_t kTriBlocks  = BlasBuildMirror::kTriBlocks;    // 3
    constexpr uint32_t kMortonDepth = BlasBuildMirror::kMortonDepth;  // 30 bits, 3 per level — the kernel's own depth

    inline const uint8_t* NodeBytes(const float* Node) noexcept { return reinterpret_cast<const uint8_t*>(Node); }
    inline uint8_t* NodeBytes(float* Node) noexcept { return reinterpret_cast<uint8_t*>(Node); }

    inline uint32_t WordAt(const float* Node, uint32_t Index) noexcept
    {
        uint32_t Value = 0u;
        std::memcpy(&Value, &Node[Index], sizeof(Value));
        return Value;
    }
    inline void StoreWord(float* Node, uint32_t Index, uint32_t Value) noexcept
    {
        std::memcpy(&Node[Index], &Value, sizeof(Value));
    }

    inline uint32_t NodeImask(const float* Node) noexcept { return WordAt(Node, 3u) >> 24u; }
    inline uint8_t  MetaOf(const float* Node, uint32_t Slot) noexcept { return NodeBytes(Node)[24u + Slot]; }
    inline bool     IsInteriorMeta(uint8_t Meta) noexcept { return (Meta & 0x18u) == 0x18u; }

    // A leaf slot's meta: the top three bits are a UNARY count (001/011/111 for 1/2/3 — a mask, so its popcount is the
    //    count), the low five are the slot's first triangle position within the node's run.
    inline uint32_t LeafRunOffset(uint8_t Meta) noexcept { return Meta & 0x1Fu; }
    inline uint32_t LeafCount(uint8_t Meta) noexcept
    {
        // Only 001/011/111 are counts. A byte of zero is an EMPTY slot, and anything else is not a leaf this format can
        //    produce — treating those as "three" would invent geometry, which is why the test is exhaustive.
        const uint32_t Unary = (Meta >> 5u) & 0x7u;
        return Unary == 0b001u ? 1u : (Unary == 0b011u ? 2u : (Unary == 0b111u ? 3u : 0u));
    }
    inline bool SlotPresent(uint8_t Meta) noexcept { return IsInteriorMeta(Meta) || LeafCount(Meta) > 0u; }
    inline uint32_t CountToUnary(uint32_t Count) noexcept { return Count == 1u ? 0b001u : (Count == 2u ? 0b011u : 0b111u); }

    inline uint8_t InteriorMeta(uint32_t Slot) noexcept { return static_cast<uint8_t>((1u << 5) | (24u + Slot)); }

    // A slot's quantised box, exactly as TraverseChildren decodes it: qlo.x at byte 32 + slot, qlo.y at +8, qlo.z at +16,
    //    qhi.x at +24, qhi.y at +32, qhi.z at +40, scaled by 2^exponent and offset by the node's `p`.
    void DecodeSlotBox(const float* Node, uint32_t Slot, float Lo[3], float Hi[3]) noexcept
    {
        const uint8_t* B = NodeBytes(Node);
        const float Qx = std::pow(2.0f, static_cast<float>(static_cast<int8_t>(B[12])));
        const float Qy = std::pow(2.0f, static_cast<float>(static_cast<int8_t>(B[13])));
        const float Qz = std::pow(2.0f, static_cast<float>(static_cast<int8_t>(B[14])));
        Lo[0] = Node[0] + static_cast<float>(B[32u + Slot +  0u]) * Qx;
        Lo[1] = Node[1] + static_cast<float>(B[32u + Slot +  8u]) * Qy;
        Lo[2] = Node[2] + static_cast<float>(B[32u + Slot + 16u]) * Qz;
        Hi[0] = Node[0] + static_cast<float>(B[32u + Slot + 24u]) * Qx;
        Hi[1] = Node[1] + static_cast<float>(B[32u + Slot + 32u]) * Qy;
        Hi[2] = Node[2] + static_cast<float>(B[32u + Slot + 40u]) * Qz;
    }

    // A run entry holds e1 = v2 - v0, e2 = v1 - v0, v0 with the object-space primitive index in v0.w.
    void DecodeTriangle(const std::vector<float>& Leaves, uint32_t LeafOffset, uint32_t Block, float V0[3], float V1[3],
                        float V2[3], uint32_t& OutPrimitive) noexcept
    {
        const float* E = &Leaves[(static_cast<size_t>(LeafOffset) + Block) * 4u];
        V0[0] = E[8]; V0[1] = E[9];  V0[2] = E[10];
        V1[0] = V0[0] + E[4]; V1[1] = V0[1] + E[5]; V1[2] = V0[2] + E[6];
        V2[0] = V0[0] + E[0]; V2[1] = V0[1] + E[1]; V2[2] = V0[2] + E[2];
        float Index = 0.0f;
        std::memcpy(&Index, &E[11], sizeof(uint32_t));
        OutPrimitive = 0u;
        std::memcpy(&OutPrimitive, &Index, sizeof(uint32_t));
    }

    void AppendTriangle(std::vector<float>& Leaves, const TriangleIndex& T, uint32_t Primitive) noexcept
    {
        const size_t At = Leaves.size();
        Leaves.resize(At + kTriBlocks * 4u);
        float* E = &Leaves[At];
        const float V0[3] = { T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ };
        const float V1[3] = { T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ  };
        const float V2[3] = { T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ };
        for (int C = 0; C < 3; ++C)
        {
            E[0u * 4u + C] = V2[C] - V0[C];
            E[1u * 4u + C] = V1[C] - V0[C];
            E[2u * 4u + C] = V0[C];
        }
        float Index = 0.0f;
        std::memcpy(&Index, &Primitive, sizeof(uint32_t));
        E[11] = Index;
    }

    // ── the build's own bookkeeping ─────────────────────────────────────────────────────────────────────────────────
    struct Sorted
    {
        uint32_t Morton = 0u;
        uint32_t Primitive = 0u;
    };

    struct StagedChild
    {
        bool     Interior = false;
        uint32_t Slot = 0u;
        uint32_t RangeLo = 0u, RangeHi = 0u, Depth = 0u;   // interior children: the subrange they cover
        uint32_t First = 0u, Count = 0u;                   // leaf children: their position in the node's run
    };

    struct StagedNode
    {
        float        Min[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
        float        Max[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
        uint32_t     ChildBase = 0u;
        uint32_t     TriBase = 0u;
        uint32_t     Depth = 0u;             // [cnt] this node's own depth (what the BFS level table must agree with)
        std::vector<StagedChild> Children;   // in slot order
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                             the shared quantiser — moved here from D8, so there is exactly one
//------------------------------------------------------------------------------------------------------------------------

void BlasBuildMirror::QuantiseNode(float* Node, const float SlotMin[8][3], const float SlotMax[8][3],
                                   const bool SlotPresent[8]) noexcept
{
    uint8_t* Bytes = NodeBytes(Node);

    float Lo[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
    float Hi[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
    for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
    {
        if (!SlotPresent[Slot]) continue;
        for (int C = 0; C < 3; ++C)
        {
            Lo[C] = std::min(Lo[C], SlotMin[Slot][C]);
            Hi[C] = std::max(Hi[C], SlotMax[Slot][C]);
        }
    }
    if (!(Lo[0] <= Hi[0])) return;   // no live child: leave the node exactly as it was

    Node[0] = Lo[0]; Node[1] = Lo[1]; Node[2] = Lo[2];
    // A flat node (zero extent on an axis — a perfectly planar mesh) has no exponent: log2(0) is −inf and the cast to
    //    int8 is undefined. tinybvh takes that cast on faith; here the axis is pinned to a cell of 1.0, which quantises a
    //    zero-extent range to 0/0 and is therefore exact.
    const float Extent[3] = { Hi[0] - Lo[0], Hi[1] - Lo[1], Hi[2] - Lo[2] };
    const int8_t Ex = (Extent[0] > 0.0f) ? static_cast<int8_t>(std::ceil(std::log2(Extent[0] / 255.0f))) : int8_t(0);
    const int8_t Ey = (Extent[1] > 0.0f) ? static_cast<int8_t>(std::ceil(std::log2(Extent[1] / 255.0f))) : int8_t(0);
    const int8_t Ez = (Extent[2] > 0.0f) ? static_cast<int8_t>(std::ceil(std::log2(Extent[2] / 255.0f))) : int8_t(0);
    const uint8_t Mask = Bytes[15];   // preserved: the caller owns the imask, a refit must not lose it
    Bytes[12] = static_cast<uint8_t>(Ex);
    Bytes[13] = static_cast<uint8_t>(Ey);
    Bytes[14] = static_cast<uint8_t>(Ez);
    Bytes[15] = Mask;

    const float Qx = std::pow(2.0f, static_cast<float>(Ex));
    const float Qy = std::pow(2.0f, static_cast<float>(Ey));
    const float Qz = std::pow(2.0f, static_cast<float>(Ez));
    uint8_t* Base = Bytes + 32u;
    for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
    {
        if (!SlotPresent[Slot]) continue;
        const int32_t QLoX = static_cast<int32_t>(std::floor((SlotMin[Slot][0] - Lo[0]) / Qx));
        const int32_t QLoY = static_cast<int32_t>(std::floor((SlotMin[Slot][1] - Lo[1]) / Qy));
        const int32_t QLoZ = static_cast<int32_t>(std::floor((SlotMin[Slot][2] - Lo[2]) / Qz));
        const int32_t QHiX = static_cast<int32_t>(std::ceil ((SlotMax[Slot][0] - Lo[0]) / Qx));
        const int32_t QHiY = static_cast<int32_t>(std::ceil ((SlotMax[Slot][1] - Lo[1]) / Qy));
        const int32_t QHiZ = static_cast<int32_t>(std::ceil ((SlotMax[Slot][2] - Lo[2]) / Qz));
        Base[Slot +  0u] = static_cast<uint8_t>(QLoX);
        Base[Slot +  8u] = static_cast<uint8_t>(QLoY);
        Base[Slot + 16u] = static_cast<uint8_t>(QLoZ);
        Base[Slot + 24u] = static_cast<uint8_t>(QHiX);
        Base[Slot + 32u] = static_cast<uint8_t>(QHiY);
        Base[Slot + 40u] = static_cast<uint8_t>(QHiZ);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              slot accessors (the kernels mirror these)
//------------------------------------------------------------------------------------------------------------------------

bool BlasBuildMirror::ScanLevelChildBases(const std::vector<uint32_t>& Counts, uint32_t TileStart, uint32_t BlockWidth,
                                          uint32_t BlockSumsWords, std::vector<uint32_t>& OutBases) noexcept
{
    OutBases.clear();
    if (BlockWidth == 0u) return false;
    const uint32_t Nodes = static_cast<uint32_t>(Counts.size());
    const uint32_t Blocks = Nodes / BlockWidth + ((Nodes % BlockWidth) != 0u ? 1u : 0u);
    if (Blocks > BlockSumsWords) return false;

    // stage 2 — each lane counts one node, the workgroup scans its own 128 counts (Hillis–Steele, the shader's loop),
    //    and the last lane publishes the block's total.
    std::vector<uint32_t> BlockTotals(Blocks, 0u);
    OutBases.assign(Nodes, 0u);
    for (uint32_t Block = 0u; Block < Blocks; ++Block)
    {
        const uint32_t Lo = Block * BlockWidth;
        const uint32_t Hi = std::min(Nodes, Lo + BlockWidth);
        uint32_t Inclusive = 0u;
        for (uint32_t I = Lo; I < Hi; ++I)
        {
            OutBases[I] = Inclusive;          // the exclusive prefix = the inclusive one minus this lane's own value
            Inclusive += Counts[I];
        }
        BlockTotals[Block] = Inclusive;       // the shader's Inclusive for the last lane of the block
    }

    // stage 4 — one workgroup walks the block totals and turns them into exclusive prefixes in place.
    uint32_t Running = 0u;
    for (uint32_t Block = 0u; Block < Blocks; ++Block)
    {
        const uint32_t Total = BlockTotals[Block];
        BlockTotals[Block] = Running;
        Running += Total;
    }

    // stage 3 — the emit adds the two halves back together, plus the level's tile start.
    for (uint32_t I = 0u; I < Nodes; ++I) OutBases[I] += TileStart + BlockTotals[I / BlockWidth];
    return true;
}

bool BlasBuildMirror::SlotIsInterior(const float* Node, uint32_t Slot) noexcept
{
    return IsInteriorMeta(MetaOf(Node, Slot));
}

uint32_t BlasBuildMirror::SlotTriangleRun(const float* Node, uint32_t Slot, uint32_t& OutFirst) noexcept
{
    const uint8_t Meta = MetaOf(Node, Slot);
    OutFirst = LeafRunOffset(Meta);
    return IsInteriorMeta(Meta) ? 0u : LeafCount(Meta);
}

uint32_t BlasBuildMirror::InteriorChildIndex(const float* Node, uint32_t Slot) noexcept
{
    // The traversal's own rule: the number of interior children at a lower stored slot, added to childBase. The imask
    //    byte is the only field that survives the octant permutation, which is exactly why the traversal ORs it in.
    uint32_t Rank = 0u;
    for (uint32_t Lower = 0u; Lower < Slot; ++Lower) if (IsInteriorMeta(MetaOf(Node, Lower))) ++Rank;
    return WordAt(Node, 4u) + Rank;
}

void BlasBuildMirror::SlotBox(const float* Node, uint32_t Slot, float Lo[3], float Hi[3]) noexcept
{
    DecodeSlotBox(Node, Slot, Lo, Hi);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        the level table a refit kernel dispatches over
//------------------------------------------------------------------------------------------------------------------------

void BlasBuildMirror::LevelsOf(const std::vector<float>& Nodes, uint32_t NodeOffset, uint32_t NodeBlocks,
                               std::vector<uint16_t>& OutLevels, uint32_t& OutMaxLevel) noexcept
{
    const uint32_t NodeCount = NodeBlocks / kNodeBlocks;
    OutLevels.assign(NodeCount, uint16_t(0xFFFFu));
    OutMaxLevel = 0u;
    if (NodeCount == 0u) return;
    OutLevels[0] = 0u;   // the root is the only node without a parent
    // One pass in ascending index: a child always sits at a HIGHER index than its parent (the collapse emits children
    //    into a growing arena), which is also the property D8's descending sweep and this table both rest on.
    for (uint32_t Local = 0u; Local < NodeCount; ++Local)
    {
        const float* Node = &Nodes[(static_cast<size_t>(NodeOffset) + static_cast<size_t>(Local) * kNodeBlocks) * 4u];
        if (OutLevels[Local] == 0xFFFFu) continue;                       // unreachable node: not part of the tree
        const uint16_t Level = static_cast<uint16_t>(OutLevels[Local] + 1u);
        for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
        {
            if (!SlotIsInterior(Node, Slot)) continue;
            const uint32_t Child = InteriorChildIndex(Node, Slot);
            if (Child >= NodeCount) continue;                            // malformed: the gate counts it
            if (OutLevels[Child] == 0xFFFFu) OutLevels[Child] = Level;
            OutMaxLevel = std::max(OutMaxLevel, uint32_t(OutLevels[Child]));
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                     ⟳  RefitLevelOrder — D8's refit, level by level
//------------------------------------------------------------------------------------------------------------------------

bool BlasBuildMirror::RefitLevelOrder(std::vector<float>& Nodes, uint32_t NodeOffset, std::vector<float>& Leaves,
                                      uint32_t LeafOffset, const std::vector<TriangleIndex>& Triangles,
                                      const std::vector<uint16_t>& Levels, BlasBuildMirrorMetrics& OutMetrics) noexcept
{
    const auto Start = std::chrono::steady_clock::now();
    const uint32_t NodeCount = static_cast<uint32_t>(Levels.size());
    uint32_t MaxLevel = 0u;
    for (uint16_t Level : Levels) if (Level != 0xFFFFu) MaxLevel = std::max(MaxLevel, uint32_t(Level));

    // ① leaves, exactly as D8 rewrites them: the blob is self-describing (each entry carries its own primitive index),
    //    so no side table is needed.
    uint32_t Rewritten = 0u;
    for (uint32_t Local = 0u; Local < NodeCount; ++Local)
    {
        const float* Node = &Nodes[(static_cast<size_t>(NodeOffset) + static_cast<size_t>(Local) * kNodeBlocks) * 4u];
        const uint32_t TriBase = WordAt(Node, 5u);
        for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
        {
            if (SlotIsInterior(Node, Slot)) continue;
            const uint32_t First = LeafRunOffset(MetaOf(Node, Slot));
            const uint32_t Count = LeafCount(MetaOf(Node, Slot));
            for (uint32_t J = 0u; J < Count; ++J)
            {
                float* Entry = &Leaves[(static_cast<size_t>(LeafOffset) + TriBase + (First + J) * kTriBlocks) * 4u];
                uint32_t Primitive = 0u;
                std::memcpy(&Primitive, &Entry[11], sizeof(uint32_t));
                if (Primitive >= Triangles.size()) return false;
                const TriangleIndex& T = Triangles[Primitive];
                const float V0[3] = { T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ };
                const float V1[3] = { T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ  };
                const float V2[3] = { T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ };
                for (int C = 0; C < 3; ++C)
                {
                    Entry[0u * 4u + C] = V2[C] - V0[C];
                    Entry[1u * 4u + C] = V1[C] - V0[C];
                    Entry[2u * 4u + C] = V0[C];
                }
                ++Rewritten;
            }
        }
    }

    // ② nodes, deepest level first. A node's children are exactly one level below it, so every write reads only bytes
    //    that are already final this pass — the parallel form of D8's descending sweep.
    uint32_t RefitNodes = 0u;
    for (uint32_t Level = MaxLevel + 1u; Level-- > 0u; )
    {
        for (uint32_t Local = 0u; Local < NodeCount; ++Local)
        {
            if (Levels[Local] != Level) continue;
            float* Node = &Nodes[(static_cast<size_t>(NodeOffset) + static_cast<size_t>(Local) * kNodeBlocks) * 4u];
            const uint32_t TriBase = WordAt(Node, 5u);
            float SlotMin[8][3], SlotMax[8][3];
            bool  Present[8];
            for (uint32_t Slot = 0u; Slot < 8u; ++Slot) Present[Slot] = false;
            for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
            {
                const uint8_t Meta = MetaOf(Node, Slot);
                if (!SlotPresent(Meta)) continue;             // empty slots carry a zero meta byte
                Present[Slot] = true;
                if (IsInteriorMeta(Meta))
                {
                    const uint32_t Child = InteriorChildIndex(Node, Slot);
                    if (Child >= NodeCount) return false;
                    const float* C = &Nodes[(static_cast<size_t>(NodeOffset) + static_cast<size_t>(Child) * kNodeBlocks) * 4u];
                    // What the parent must contain is the child's DECODED extent — the union of its own slots, i.e.
                    //    exactly the volume the traversal will test under it. That is what makes a refit conservative.
                    //    A child with no present slot falls back to its own `p`, exactly as RefitBlas does.
                    float Lo[3] = { C[0], C[1], C[2] };
                    float Hi[3] = { C[0], C[1], C[2] };
                    float UnionLo[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
                    float UnionHi[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
                    for (uint32_t Inner = 0u; Inner < 8u; ++Inner)
                    {
                        if (!SlotPresent(MetaOf(C, Inner))) continue;
                        float SL[3], SH[3];
                        DecodeSlotBox(C, Inner, SL, SH);
                        for (int K = 0; K < 3; ++K) { UnionLo[K] = std::min(UnionLo[K], SL[K]); UnionHi[K] = std::max(UnionHi[K], SH[K]); }
                    }
                    if (UnionLo[0] <= UnionHi[0]) { for (int K = 0; K < 3; ++K) { Lo[K] = UnionLo[K]; Hi[K] = UnionHi[K]; } }
                    for (int K = 0; K < 3; ++K) { SlotMin[Slot][K] = Lo[K]; SlotMax[Slot][K] = Hi[K]; }
                }
                else
                {
                    const uint32_t First = LeafRunOffset(Meta);
                    const uint32_t Count = LeafCount(Meta);
                    float Lo[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
                    float Hi[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
                    for (uint32_t J = 0u; J < Count; ++J)
                    {
                        float V0[3], V1[3], V2[3];
                        uint32_t Ignored = 0u;
                        DecodeTriangle(Leaves, LeafOffset, TriBase + (First + J) * kTriBlocks, V0, V1, V2, Ignored);
                        const float* Tri[3] = { V0, V1, V2 };
                        for (int K = 0; K < 3; ++K) for (int C = 0; C < 3; ++C)
                        {
                            Lo[C] = std::min(Lo[C], Tri[K][C]);
                            Hi[C] = std::max(Hi[C], Tri[K][C]);
                        }
                    }
                    for (int K = 0; K < 3; ++K) { SlotMin[Slot][K] = Lo[K]; SlotMax[Slot][K] = Hi[K]; }
                }
            }
            QuantiseNode(Node, SlotMin, SlotMax, Present);
            ++RefitNodes;
        }
    }

    OutMetrics.RefitMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count());
    OutMetrics.LastRefitNodeCount = RefitNodes;
    OutMetrics.LastRefitTriangleCount = Rewritten;
    OutMetrics.MaxLevel = MaxLevel;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       ⚙  BuildHPloc — a full build into the packed layout
//------------------------------------------------------------------------------------------------------------------------

bool BlasBuildMirror::BuildHPloc(const std::vector<TriangleIndex>& Triangles, std::vector<float>& OutNodes,
                                 std::vector<float>& OutLeaves, BlasBuildMirrorMetrics& OutMetrics,
                                 BlasPartition Partition, bool PackFittedRanges) noexcept
{
    const auto Start = std::chrono::steady_clock::now();
    OutNodes.clear();
    OutLeaves.clear();
    const uint32_t Count = static_cast<uint32_t>(Triangles.size());
    OutMetrics.TriangleCount = Count;
    if (Count == 0u) return false;

    // ① per-triangle bounds, centroids and Morton codes (30 bits, 10 per axis, normalised into the object AABB). A
    //    degenerate axis gets a zero scale: every code collapses there, which is right — the octant partition simply
    //    cannot separate triangles along an axis that has no extent.
    std::vector<Sorted> Sorted_(Count);
    std::vector<float> TriMin(static_cast<size_t>(Count) * 3u), TriMax(static_cast<size_t>(Count) * 3u);
    float Lo[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
    float Hi[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const TriangleIndex& T = Triangles[I];
        const float PX[3] = { T.VertexAlphaX, T.VertexBetaX, T.VertexGammaX };
        const float PY[3] = { T.VertexAlphaY, T.VertexBetaY, T.VertexGammaY };
        const float PZ[3] = { T.VertexAlphaZ, T.VertexBetaZ, T.VertexGammaZ };
        float* Min = &TriMin[static_cast<size_t>(I) * 3u];
        float* Max = &TriMax[static_cast<size_t>(I) * 3u];
        Min[0] = *std::min_element(PX, PX + 3); Max[0] = *std::max_element(PX, PX + 3);
        Min[1] = *std::min_element(PY, PY + 3); Max[1] = *std::max_element(PY, PY + 3);
        Min[2] = *std::min_element(PZ, PZ + 3); Max[2] = *std::max_element(PZ, PZ + 3);
        for (int C = 0; C < 3; ++C)
        {
            Lo[C] = std::min(Lo[C], Min[C]);
            Hi[C] = std::max(Hi[C], Max[C]);
        }
        Sorted_[I].Primitive = I;
    }
    float Scale[3];
    for (int C = 0; C < 3; ++C) Scale[C] = (Hi[C] - Lo[C]) > 0.0f ? 1.0f / (Hi[C] - Lo[C]) : 0.0f;
    auto Spread = [](uint32_t V) -> uint32_t
    {
        uint32_t R = 0u;
        for (int Bit = 0; Bit < 10; ++Bit) R |= ((V >> Bit) & 1u) << (Bit * 3);
        return R;
    };
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float* Min = &TriMin[static_cast<size_t>(I) * 3u];
        const float* Max = &TriMax[static_cast<size_t>(I) * 3u];
        uint32_t Q[3];
        for (int C = 0; C < 3; ++C)
        {
            const float Centre = 0.5f * (Min[C] + Max[C]);
            const float F = (Centre - Lo[C]) * Scale[C];
            const int32_t Qq = static_cast<int32_t>(F * 1023.0f + 0.5f);
            Q[C] = static_cast<uint32_t>(Qq < 0 ? 0 : (Qq > 1023 ? 1023 : Qq));
        }
        Sorted_[I].Morton = (Spread(Q[0]) << 2) | (Spread(Q[1]) << 1) | Spread(Q[2]);
    }

    // ② sort by code. The kernel runs the same order out of shared memory (four 8-bit radix passes); here a stable sort
    //    over the 30-bit code — the two produce the same sequence up to ties, and ties are all inside one octant anyway.
    std::stable_sort(Sorted_.begin(), Sorted_.end(), [](const Sorted& A, const Sorted& B) { return A.Morton < B.Morton; });

    auto OctantAt = [](uint32_t Morton, uint32_t Depth) -> uint32_t
    {
        return (Morton >> (27u - Depth * 3u)) & 0x7u;
    };
    auto RangeBox = [&](uint32_t RLo, uint32_t RHi, float OutLo[3], float OutHi[3])
    {
        for (int C = 0; C < 3; ++C) { OutLo[C] =  1.0e30f; OutHi[C] = -1.0e30f; }
        for (uint32_t I = RLo; I < RHi; ++I)
        {
            const uint32_t P = Sorted_[I].Primitive;
            for (int C = 0; C < 3; ++C)
            {
                OutLo[C] = std::min(OutLo[C], TriMin[static_cast<size_t>(P) * 3u + C]);
                OutHi[C] = std::max(OutHi[C], TriMax[static_cast<size_t>(P) * 3u + C]);
            }
        }
    };

    // ③ partition the Morton-sorted range into the wide tree, breadth-first so a node's children are allocated together
    //    (the rank contract: children occupy consecutive arena slots, in ascending stored-slot order).
    //
    //    Both rules cut a node's range into CONTIGUOUS slices of the Morton order, so locality, the arena order and the
    //    leaf-run layout do not depend on the choice — only where the cuts go:
    //
    //      Clustered (shipped)  the range is cut into eight COUNT-BALANCED bins and the SAH then keeps whichever of the
    //                           seven boundaries pay for themselves, merging neighbours where the merged box is cheaper
    //                           than the extra child. A range that is spatially clumped — every triangle inside one
    //                           octant, which an octant split cannot split at all — is cut into eight anyway, and the
    //                           equal counts keep the tree shallow. This is the PLOC/H-PLOC-shaped half of the build.
    //      Octant               every occupied octant becomes a child: the tree mirrors the Morton octree. Kept because
    //                           it is what D9 shipped first, and because §⑨g of the two-level gate walks BOTH builds
    //                           over the same rays and reports the work each one costs.
    //
    //    Two rules belong to the FORMAT rather than to the split, and apply to every configuration that is not the
    //    reproducible D9-v1 baseline (`Octant` with `PackFittedRanges == false`):
    //      · a range that fits ONE node — eight leaf slots hold 24 triangles — is emitted as runs of three and never
    //        recursed into. D9's first build split ranges a single node could have held: 17 835 nodes for 63 854
    //        triangles, 64 % of the wide slots empty;
    //      · once the Morton bits are exhausted, a range too large for that is cut into eight count-balanced pieces —
    //        the Clustered rule's bins, without the SAH — because per-octant splitting there could ask for up to 64
    //        children from one node, and a node has eight slots.
    std::vector<StagedNode> Nodes;
    Nodes.emplace_back();
    struct Work { uint32_t Lo, Hi, Depth, Node; };
    std::vector<Work> Pending;
    Pending.push_back({ 0u, Count, 0u, 0u });
    std::vector<Sorted> Scratch(Count);

    // Which slot a child PREFERS: the octant of its first triangle at this depth. A preference only — the pool in the
    //    slot-assignment block decides, and nothing in the traversal depends on a slot meaning an octant.
    const auto PreferredSlot = [&](uint32_t Lo, uint32_t Depth) -> uint32_t
    {
        return Depth < kMortonDepth ? OctantAt(Sorted_[Lo].Morton, Depth) : 0u;
    };
    // A contiguous slice of the node's Morton range as a child. `Slot` starts as the preference; the pool resolves it.
    //    (Named MakeChild, not Child: the emit loops below iterate `StagedChild& Child`, which would hide it — C4456.)
    const auto MakeChild = [&](uint32_t Slot, uint32_t Lo, uint32_t Hi, uint32_t Depth) -> StagedChild
    {
        if (Hi - Lo <= kMaxTrianglesPerLeaf) return StagedChild{ false, Slot, Lo, Hi, Depth, 0u, Hi - Lo };
        return StagedChild{ true, Slot, Lo, Hi, Depth, 0u, 0u };
    };
    // Eight count-balanced boundaries over a range — the bins every non-octant path is built on.
    const auto BinBounds = [](uint32_t Lo, uint32_t Size, uint32_t Out[9])
    {
        Out[0] = Lo;
        for (uint32_t Bin = 0u; Bin < 8u; ++Bin) Out[Bin + 1u] = Lo + static_cast<uint32_t>(uint64_t(Size) * (Bin + 1u) / 8u);
    };
    const auto EmitRuns = [&](uint32_t Lo, uint32_t Hi, uint32_t Depth, std::vector<StagedChild>& Out)
    {
        for (uint32_t Run = Lo; Run < Hi; Run += kMaxTrianglesPerLeaf)
            Out.push_back(MakeChild(PreferredSlot(Run, Depth), Run, std::min(Hi, Run + kMaxTrianglesPerLeaf), Depth + 1u));
    };
    for (size_t Head = 0u; Head < Pending.size(); ++Head)
    {
        const Work W = Pending[Head];
        // ⚠️ No reference into `Nodes` may be held across the `emplace_back` below: growing the vector reallocates and
        //    every write through the stale reference lands in freed memory (it silently produced a tree of empty nodes).
        RangeBox(W.Lo, W.Hi, Nodes[W.Node].Min, Nodes[W.Node].Max);
        Nodes[W.Node].Depth = W.Depth;

        const uint32_t Size = W.Hi - W.Lo;
        const bool Fits = Size <= 8u * kMaxTrianglesPerLeaf;
        const bool Baseline = (Partition == BlasPartition::Octant) && !PackFittedRanges;   // D9's first build
        std::vector<StagedChild> Staged;

        if (Fits && !Baseline)
        {
            EmitRuns(W.Lo, W.Hi, W.Depth, Staged);
        }
        else if (Partition == BlasPartition::Clustered || Partition == BlasPartition::Collapse ||
                 W.Depth >= kMortonDepth)
        {
            // Eight count-balanced bins, then the SAH chooses which of the seven boundaries to keep — merging neighbours
            //    where the merged box is cheaper than the extra child. A mask of zero would mean a single child covering
            //    the whole range, i.e. no progress, so a grouping always keeps at least one boundary.
            //
            //    The cost is the standard SAH shape with a leaf term: a child costs one box test plus three units per
            //    triangle it holds (a Möller–Trumbore against a box test), weighted by its box area. A group of three or
            //    fewer gets the same expression as the leaf it becomes, which is what lets the search avoid padding
            //    small groups out into nodes of their own.
            uint32_t Bound[9];
            BinBounds(W.Lo, Size, Bound);
            float BinLo[8][3], BinHi[8][3];
            for (uint32_t Bin = 0u; Bin < 8u; ++Bin)
            {
                for (int C = 0; C < 3; ++C) { BinLo[Bin][C] = 1.0e30f; BinHi[Bin][C] = -1.0e30f; }
                for (uint32_t I = Bound[Bin]; I < Bound[Bin + 1u]; ++I)
                {
                    const uint32_t P = Sorted_[I].Primitive;
                    for (int C = 0; C < 3; ++C)
                    {
                        BinLo[Bin][C] = std::min(BinLo[Bin][C], TriMin[static_cast<size_t>(P) * 3u + C]);
                        BinHi[Bin][C] = std::max(BinHi[Bin][C], TriMax[static_cast<size_t>(P) * 3u + C]);
                    }
                }
            }

            // `Collapse` IS this cut with every boundary kept (the mask stays all-ones): eight children per node, full
            //    slots, and each child's box the box of one contiguous slice. The SAH search below is what `Clustered`
            //    adds on top, and §⑨g measures the two separately because the merge and the cut are different trades.
            uint32_t BestMask = 0xFFu;
            if (Partition == BlasPartition::Clustered && Size > 8u * kMaxTrianglesPerLeaf)
            {
                double BestCost = 1.0e30;
                for (uint32_t Mask = 1u; Mask < 256u; ++Mask)
                {
                    double Cost = 0.0;
                    uint32_t GroupStart = 0u;
                    for (uint32_t Bin = 0u; Bin < 8u; ++Bin)
                    {
                        if (Bin + 1u < 8u && (Mask & (1u << Bin)) == 0u) continue;   // this boundary is merged away
                        float GroupLo[3], GroupHi[3];   // not Lo/Hi — those are the function's Morton bounds (C4456)
                        uint32_t GroupCount = 0u;
                        for (int C = 0; C < 3; ++C) { GroupLo[C] = 1.0e30f; GroupHi[C] = -1.0e30f; }
                        for (uint32_t B = GroupStart; B <= Bin; ++B)
                        {
                            for (int C = 0; C < 3; ++C)
                            {
                                GroupLo[C] = std::min(GroupLo[C], BinLo[B][C]);
                                GroupHi[C] = std::max(GroupHi[C], BinHi[B][C]);
                            }
                            GroupCount += Bound[B + 1u] - Bound[B];
                        }
                        const float Ex = std::max(0.0f, GroupHi[0] - GroupLo[0]);
                        const float Ey = std::max(0.0f, GroupHi[1] - GroupLo[1]);
                        const float Ez = std::max(0.0f, GroupHi[2] - GroupLo[2]);
                        const double Area = double(Ex) * Ey + double(Ey) * Ez + double(Ez) * Ex;   // half a surface area: the factor cancels
                        Cost += (GroupCount == 0u ? 0.0 : Area * (1.0 + 3.0 * static_cast<double>(GroupCount)));
                        GroupStart = Bin + 1u;
                    }
                    if (Cost < BestCost) { BestCost = Cost; BestMask = Mask; }
                }
            }

            uint32_t GroupStart = 0u;
            for (uint32_t Bin = 0u; Bin < 8u; ++Bin)
            {
                if (Bin + 1u < 8u && (BestMask & (1u << Bin)) == 0u) continue;
                const uint32_t RangeLo = Bound[GroupStart], RangeHi = Bound[Bin + 1u];   // not Lo/Hi — Morton bounds above (C4456)
                if (RangeHi > RangeLo) Staged.push_back(MakeChild(PreferredSlot(RangeLo, W.Depth), RangeLo, RangeHi, W.Depth + 1u));
                GroupStart = Bin + 1u;
            }
        }
        else
        {
            // Stable counting partition by octant. The range is Morton-sorted, so equal octants are already adjacent and
            //    this pass is the identity permutation — kept because the octant rule is *defined* in terms of the ranges
            //    it produces, not because the order needs repairing.
            uint32_t Counts[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
            uint32_t Starts[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
            for (uint32_t I = W.Lo; I < W.Hi; ++I) ++Counts[OctantAt(Sorted_[I].Morton, W.Depth)];
            uint32_t Cursor = W.Lo;
            for (uint32_t Slot = 0u; Slot < 8u; ++Slot) { Starts[Slot] = Cursor; Cursor += Counts[Slot]; }
            uint32_t Fill[8];
            for (uint32_t Slot = 0u; Slot < 8u; ++Slot) Fill[Slot] = Starts[Slot];
            for (uint32_t I = W.Lo; I < W.Hi; ++I)
            {
                const uint32_t Slot = OctantAt(Sorted_[I].Morton, W.Depth);
                Scratch[Fill[Slot]++] = Sorted_[I];
            }
            for (uint32_t I = W.Lo; I < W.Hi; ++I) Sorted_[I] = Scratch[I];

            // One child per occupied octant: at most eight, which is what a node has.
            for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
            {
                if (Counts[Slot] == 0u) continue;
                Staged.push_back(MakeChild(Slot, Starts[Slot], Starts[Slot] + Counts[Slot], W.Depth + 1u));
            }
        }

        if (Staged.empty() || Staged.size() > 8u) return false;

        // Smoothing pass, then the slots. An interior child's box is the EXACT box of its range (`RangeBox` below), so the
        //    SAH is choosing between groupings of the same children — but the grouping that a bin merge produces can hide
        //    cells no ray can reach, and D9 measured that as the difference between 64 % empty slots and 90 % used ones.
        //
        //    Slot assignment, from a POOL of free slots: the octant is a preference, never a requirement. A slot only
        //    decides which box lane a child's quantised box lives in and where its meta byte goes, while the traversal's
        //    child index comes from the imask byte and the stored slot.
        std::vector<uint32_t> FreeSlots;
        for (uint32_t Slot = 0u; Slot < 8u; ++Slot) FreeSlots.push_back(Slot);
        for (StagedChild& C : Staged)
        {
            size_t At = FreeSlots.size();
            for (size_t I = 0u; I < FreeSlots.size(); ++I) if (FreeSlots[I] == C.Slot) { At = I; break; }
            if (At >= FreeSlots.size()) At = 0u;
            C.Slot = FreeSlots[At];
            FreeSlots.erase(FreeSlots.begin() + ptrdiff_t(At));
        }
        std::stable_sort(Staged.begin(), Staged.end(), [](const StagedChild& A, const StagedChild& B)
        {
            return A.Slot < B.Slot;
        });

        // The cover invariant the traversal leans on: leaf runs are laid out in ascending stored slot, so the running
        //    triangle total IS the `firstTri` each run will carry, and the traversal reads those as bit positions inside a
        //    24-bit group — every one of them has to stay under bit 24. (Eight runs of three end exactly at 24.)
        uint32_t RunPosition = 0u;
        for (const StagedChild& C : Staged)
        {
            if (C.Interior) continue;
            if (C.Count == 0u || RunPosition + C.Count > 24u) return false;
            RunPosition += C.Count;
        }
        Nodes[W.Node].Children = Staged;        // Allocate the interior children contiguously, in ascending slot order (the leaves need no arena index).
        const uint32_t FirstInterior = static_cast<uint32_t>(Nodes.size());
        uint32_t InteriorCount = 0u;
        for (const StagedChild& Child : Staged) if (Child.Interior) ++InteriorCount;
        for (uint32_t I = 0u; I < InteriorCount; ++I) Nodes.emplace_back();
        Nodes[W.Node].ChildBase = FirstInterior;
        uint32_t Next = FirstInterior;
        for (const StagedChild& Child : Staged)
            if (Child.Interior)
                Pending.push_back({ Child.RangeLo, Child.RangeHi, Child.Depth, Next++ });
    }

    // ④ triangle array: every node's leaf children in one run, positions assigned in slot order (so a slot's `First` is
    //    the offset the traversal adds to the node's triangle base and uses as its hit bit).
    for (StagedNode& Node : Nodes)
    {
        Node.TriBase = static_cast<uint32_t>(OutLeaves.size() / 4u);
        uint32_t Position = 0u;
        for (StagedChild& Child : Node.Children)
        {
            if (Child.Interior) continue;
            Child.First = Position;
            for (uint32_t I = Child.RangeLo; I < Child.RangeHi; ++I)
                AppendTriangle(OutLeaves, Triangles[Sorted_[I].Primitive], Sorted_[I].Primitive);
            Position += Child.Count;
        }
    }

    // ⑤ emit. Nodes are in breadth-first order, so a parent's children are consecutive and every child's index is
    //    greater than its parent's — the same monotonicity D8's refit sweep relies on.
    OutNodes.assign(Nodes.size() * kNodeBlocks * 4u, 0.0f);
    uint32_t EmittedLeafSlots = 0u, EmittedSlots = 0u;
    for (uint32_t Local = 0u; Local < Nodes.size(); ++Local)
    {
        const StagedNode& SNode = Nodes[Local];   // no growth inside this loop: a reference is safe here
        if (SNode.Depth > OutMetrics.MaxLevel) OutMetrics.MaxLevel = SNode.Depth;
        float* Node = &OutNodes[static_cast<size_t>(Local) * kNodeBlocks * 4u];
        float SlotMin[8][3], SlotMax[8][3];
        bool  Present[8];
        for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
        {
            Present[Slot] = false;
            for (int C = 0; C < 3; ++C) { SlotMin[Slot][C] = 0.0f; SlotMax[Slot][C] = 0.0f; }
        }
        uint32_t Mask = 0u, NextInterior = SNode.ChildBase;
        for (const StagedChild& Child : SNode.Children)
        {
            Present[Child.Slot] = true;
            if (Child.Interior)
            {
                const StagedNode& Target = Nodes[NextInterior];
                for (int C = 0; C < 3; ++C) { SlotMin[Child.Slot][C] = Target.Min[C]; SlotMax[Child.Slot][C] = Target.Max[C]; }
                Mask |= 1u << Child.Slot;
                ++NextInterior;
                ++EmittedSlots;
            }
            else
            {
                for (int C = 0; C < 3; ++C) { SlotMin[Child.Slot][C] = 1.0e30f; SlotMax[Child.Slot][C] = -1.0e30f; }
                for (uint32_t I = Child.RangeLo; I < Child.RangeHi; ++I)
                {
                    const uint32_t P = Sorted_[I].Primitive;
                    for (int C = 0; C < 3; ++C)
                    {
                        SlotMin[Child.Slot][C] = std::min(SlotMin[Child.Slot][C], TriMin[static_cast<size_t>(P) * 3u + C]);
                        SlotMax[Child.Slot][C] = std::max(SlotMax[Child.Slot][C], TriMax[static_cast<size_t>(P) * 3u + C]);
                    }
                }
                ++EmittedLeafSlots;
                ++EmittedSlots;
            }
        }
        QuantiseNode(Node, SlotMin, SlotMax, Present);
        uint8_t* Bytes = NodeBytes(Node);
        Bytes[15] = static_cast<uint8_t>(Mask);
        for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
        {
            Bytes[24u + Slot] = 0u;
            for (const StagedChild& Child : SNode.Children)
            {
                if (Child.Slot != Slot) continue;
                Bytes[24u + Slot] = Child.Interior ? InteriorMeta(Slot)
                                                   : static_cast<uint8_t>((CountToUnary(Child.Count) << 5u) | Child.First);
            }
        }
        StoreWord(Node, 4u, SNode.ChildBase);
        StoreWord(Node, 5u, SNode.TriBase);
        OutMetrics.EmptySlots += 8u - static_cast<uint32_t>(SNode.Children.size());
    }

    OutMetrics.NodeCount  = static_cast<uint32_t>(Nodes.size());
    OutMetrics.NodeBlocks = OutMetrics.NodeCount * kNodeBlocks;
    OutMetrics.LeafSlots  = EmittedLeafSlots;
    OutMetrics.LeafBlocks = static_cast<uint32_t>(OutLeaves.size() / 4u);
    OutMetrics.BuildMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count());
    if (OutLeaves.empty()) OutLeaves.resize(kTriBlocks * 4u, 0.0f);   // never hand the traversal an empty blob
    return true;
}
}

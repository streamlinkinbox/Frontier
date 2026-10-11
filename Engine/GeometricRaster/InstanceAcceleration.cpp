//============================================================================================================================================
//                                                   INSTANCEACCELERATION.CPP
//============================================================================================================================================
// 🧩 D6/D7 — object-space BLASes in one shared pair of CWBVH blobs, an instance TLAS over their world AABBs, and the
//    CPU reference trace the proofs use. The BLAS builders and the CPU walker are TraversalIndex (the same code the
//    single world-space tree uses), so "the identity case agrees" is a statement about structure, not about two
//    different builders.
//
//    ⚠️ TINYBVH_IMPLEMENTATION lives in TraversalIndex.cpp and ONLY there. This file includes tiny_bvh.h for the
//    declarations (BVH, BLASInstance, BVHNode) and links against that translation unit, which is why it must be
//    compiled with the same SIMD flags — the class layouts must not be allowed to diverge across the two TUs.

#include "InstanceAcceleration.h"
#include "BlasBuildMirror.h"
#include "TraversalIndex.h"
#include "../DeviceExchange/SwapchainExchange.h"   // TriangleIndex

// ⚠️ The layout-affecting switch must match TraversalIndex.cpp, or the two translation units disagree about the
//    classes they pass between them. -Wall -Wextra -Werror also necessitates the same suppression block: tiny_bvh.h is
//    third-party header code and is noisy about the SIMD it could not enable on this toolchain.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define NO_DOUBLE_PRECISION_SUPPORT
#include <tiny_bvh.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier {

namespace
{
    double NowMilliseconds() noexcept
    {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HELPERS
//------------------------------------------------------------------------------------------------------------------------

bool InvertMatrix(const float Matrix[16], float OutInverse[16]) noexcept
{
    // Cofactor inverse of a column-major 4×4 (MESA's / tinybvh's form, so an inverse computed here matches the one
    //    tinybvh computes for the same input — the identity gate leans on that when it compares against a
    //    BLASInstance row).
    const float* T = Matrix;
    float* iT = OutInverse;
    iT[0]  =  T[5]*T[10]*T[15] - T[5]*T[11]*T[14] - T[9]*T[6]*T[15] + T[9]*T[7]*T[14] + T[13]*T[6]*T[11] - T[13]*T[7]*T[10];
    iT[1]  = -T[1]*T[10]*T[15] + T[1]*T[11]*T[14] + T[9]*T[2]*T[15] - T[9]*T[3]*T[14] - T[13]*T[2]*T[11] + T[13]*T[3]*T[10];
    iT[2]  =  T[1]*T[6]*T[15]  - T[1]*T[7]*T[14]  - T[5]*T[2]*T[15] + T[5]*T[3]*T[14] + T[13]*T[2]*T[7]  - T[13]*T[3]*T[6];
    iT[3]  = -T[1]*T[6]*T[11]  + T[1]*T[7]*T[10]  + T[5]*T[2]*T[11] - T[5]*T[3]*T[10] - T[9]*T[2]*T[7]   + T[9]*T[3]*T[6];
    iT[4]  = -T[4]*T[10]*T[15] + T[4]*T[11]*T[14] + T[8]*T[6]*T[15] - T[8]*T[7]*T[14] - T[12]*T[6]*T[11] + T[12]*T[7]*T[10];
    iT[5]  =  T[0]*T[10]*T[15] - T[0]*T[11]*T[14] - T[8]*T[2]*T[15] + T[8]*T[3]*T[14] + T[12]*T[2]*T[11] - T[12]*T[3]*T[10];
    iT[6]  = -T[0]*T[6]*T[15]  + T[0]*T[7]*T[14]  + T[4]*T[2]*T[15] - T[4]*T[3]*T[14] - T[12]*T[2]*T[7]  + T[12]*T[3]*T[6];
    iT[7]  =  T[0]*T[6]*T[11]  - T[0]*T[7]*T[10]  - T[4]*T[2]*T[11] + T[4]*T[3]*T[10] + T[8]*T[2]*T[7]   - T[8]*T[3]*T[6];
    iT[8]  =  T[4]*T[9]*T[15]  - T[4]*T[11]*T[13] - T[8]*T[5]*T[15] + T[8]*T[7]*T[13] + T[12]*T[5]*T[11] - T[12]*T[7]*T[9];
    iT[9]  = -T[0]*T[9]*T[15]  + T[0]*T[11]*T[13] + T[8]*T[1]*T[15] - T[8]*T[3]*T[13] - T[12]*T[1]*T[11] + T[12]*T[3]*T[9];
    iT[10] =  T[0]*T[5]*T[15]  - T[0]*T[7]*T[13]  - T[4]*T[1]*T[15] + T[4]*T[3]*T[13] + T[12]*T[1]*T[7]  - T[12]*T[3]*T[5];
    iT[11] = -T[0]*T[5]*T[11]  + T[0]*T[7]*T[10]  + T[4]*T[1]*T[11] - T[4]*T[3]*T[10] - T[8]*T[1]*T[7]   + T[8]*T[3]*T[5];
    iT[12] = -T[4]*T[9]*T[14]  + T[4]*T[10]*T[13] + T[8]*T[5]*T[14] - T[8]*T[6]*T[13] - T[12]*T[5]*T[10] + T[12]*T[6]*T[9];
    iT[13] =  T[0]*T[9]*T[14]  - T[0]*T[10]*T[13] - T[8]*T[1]*T[14] + T[8]*T[2]*T[13] + T[12]*T[1]*T[10] - T[12]*T[2]*T[9];
    iT[14] = -T[0]*T[5]*T[14]  + T[0]*T[6]*T[13]  + T[4]*T[1]*T[14] - T[4]*T[2]*T[13] - T[12]*T[1]*T[6]  + T[12]*T[2]*T[5];
    iT[15] =  T[0]*T[5]*T[10]  - T[0]*T[6]*T[9]   - T[4]*T[1]*T[10] + T[4]*T[2]*T[9]  + T[8]*T[1]*T[6]   - T[8]*T[2]*T[5];

    const float Det = T[0]*iT[0] + T[1]*iT[4] + T[2]*iT[8] + T[3]*iT[12];
    if (!(std::fabs(Det) > 1.0e-30f)) return false;
    const float Inv = 1.0f / Det;
    for (int I = 0; I < 16; ++I) iT[I] *= Inv;
    return true;
}

bool MultiplyMatrix(const float A[16], const float B[16], float Out[16]) noexcept
{
    // Column-major: element (row R, column C) lives at C * 4 + R, and (A·B) columns are B's columns through A.
    for (uint32_t Column = 0u; Column < 4u; ++Column)
        for (uint32_t Row = 0u; Row < 4u; ++Row)
        {
            float Sum = 0.0f;
            for (uint32_t K = 0u; K < 4u; ++K) Sum += A[K * 4u + Row] * B[Column * 4u + K];
            Out[Column * 4u + Row] = Sum;
        }
    return true;
}

bool RelativeMatrix(const float WorldNow[16], const float WorldRest[16], float Out[16]) noexcept
{
    float RestInverse[16];
    if (!InvertMatrix(WorldRest, RestInverse)) return false;
    return MultiplyMatrix(WorldNow, RestInverse, Out);
}

void TransformAabb(const float Matrix[16], const float Min[3], const float Max[3], float OutMin[3], float OutMax[3]) noexcept
{
    OutMin[0] = OutMin[1] = OutMin[2] =  1.0e30f;
    OutMax[0] = OutMax[1] = OutMax[2] = -1.0e30f;
    for (int Corner = 0; Corner < 8; ++Corner)
    {
        const float P[3] = { (Corner & 1) ? Max[0] : Min[0], (Corner & 2) ? Max[1] : Min[1], (Corner & 4) ? Max[2] : Min[2] };
        for (int R = 0; R < 3; ++R)
        {
            const float W = Matrix[0 * 4 + R] * P[0] + Matrix[1 * 4 + R] * P[1] + Matrix[2 * 4 + R] * P[2] + Matrix[3 * 4 + R];
            if (W < OutMin[R]) OutMin[R] = W;
            if (W > OutMax[R]) OutMax[R] = W;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

struct InstanceAcceleration::Implementation
{
    std::vector<std::unique_ptr<TraversalIndex>> Blas;            // one per prototype
    std::vector<std::vector<TriangleIndex>>      PrototypeTris;   // the object-space soup each BLAS was built from
    std::vector<tinybvh::BLASInstance>           TlasRows;        // tinybvh's row form (transform + AABB), for Build
    std::vector<tinybvh::BVHBase*>               TlasBlasList;    // pointers tinybvh stores but never dereferences (blasList == nullptr below)
    tinybvh::BVH                                 Tlas;
    bool                                         HighQuality = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                     D8 — REFIT ONE BLAS IN PLACE (deformation)
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // The CWBVH node block layout, exactly as tinybvh's ConvertFrom writes it and as TraversalCWBVH.slang reads it:
    //     block 0 : [ p.xyz | bytes(ex, ey, ez, imask) ]
    //     block 1 : [ childBase | triangleBase | meta0..3 | meta4..7 ]    (meta byte i belongs to child slot i)
    //     block 2 : [ qlo.x | qlo.y | qlo.z | qhi.x ]   ─┐  the 8-bit quantised child bounds, one byte per child,
    //     block 3 : [ qhi.y | qhi.z | qlo.x | qlo.y ]    │  packed as 6 bytes per child across three vec4s:
    //     block 4 : [ qlo.z | qhi.x | qhi.y | qhi.z ]   ─┘  qlo.x at byte i, qlo.y at i+8, qlo.z at i+16,
    //                                                        qhi.x at i+24, qhi.y at i+32, qhi.z at i+40.
    // A meta byte's bit 7 is the interior flag (set = the child is a node), bits 5-6 are the unary-encoded triangle
    //    count of a leaf child (001/011/111 = 1/2/3), and bits 0-4 are that leaf's first triangle's slot within the
    //    node's triangle run — which is also the bit position the traversal tests the triangle hit mask against.
    constexpr uint32_t kNodeBlocks   = 5u;
    constexpr uint32_t kTriBlocks    = 3u;

    // Child slot i's quantised bounds, decoded back to floats. Used by the refit to read an interior child's CURRENT
    //    bound (which is the bound the child's own refit has just written) and to read a leaf child's bound from the
    //    triangles themselves.
    void DecodeChildBounds(const float* Node, uint32_t Slot, float OutMin[3], float OutMax[3]) noexcept
    {
        const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(Node);
        const float P[3] = { Node[0], Node[1], Node[2] };
        // ⚠️ The exponents and the interior mask ride in p.w — bytes 12..15 of the node, NOT block 1. The traversal
        //    reads them as floatBitsToUint(n0.w) with ex = the low byte and the mask in the top byte; reading block 1
        //    instead picks up childBase as an exponent, which is a silently wrong bound rather than a crash.
        const uint32_t Packed = reinterpret_cast<const uint32_t*>(Node)[3];
        const int8_t Ex = static_cast<int8_t>( Packed        & 0xFFu);
        const int8_t Ey = static_cast<int8_t>((Packed >>  8u) & 0xFFu);
        const int8_t Ez = static_cast<int8_t>((Packed >> 16u) & 0xFFu);
        const uint8_t* Base = Bytes + 32u;   // block 2's bytes: qlo.x starts here
        OutMin[0] = P[0] + static_cast<float>(Base[Slot +  0u]) * std::pow(2.0f, static_cast<float>(Ex));
        OutMin[1] = P[1] + static_cast<float>(Base[Slot +  8u]) * std::pow(2.0f, static_cast<float>(Ey));
        OutMin[2] = P[2] + static_cast<float>(Base[Slot + 16u]) * std::pow(2.0f, static_cast<float>(Ez));
        OutMax[0] = P[0] + static_cast<float>(Base[Slot + 24u]) * std::pow(2.0f, static_cast<float>(Ex));
        OutMax[1] = P[1] + static_cast<float>(Base[Slot + 32u]) * std::pow(2.0f, static_cast<float>(Ey));
        OutMax[2] = P[2] + static_cast<float>(Base[Slot + 40u]) * std::pow(2.0f, static_cast<float>(Ez));
    }

    // Re-quantise one node's eight child slots from the bounds handed in. The arithmetic now lives in ONE place —
    //    BlasBuildMirror::QuantiseNode — because D9's GPU refit kernel is transcribed from that same function, and a
    //    host refit and a device refit that disagree by one byte would be a silent correctness split. This forwarder
    //    used to hold the body; moving it changed no bytes, which the §⑧ gate re-verified.
    void EncodeNode(float* Node, const float ChildMin[8][3], const float ChildMax[8][3], const bool ChildPresent[8]) noexcept
    {
        BlasBuildMirror::QuantiseNode(Node, ChildMin, ChildMax, ChildPresent);
    }

    // Interior child slot -> the child's node index, LOCAL TO THIS BLAS' NODE ARENA (the blob stores childBase that
    //    way; only the traversal adds NodeBase). The traversal derives this from the octant-ordered hit mask; the meta
    //    bytes are stored in slot order, so the k-th interior slot is the (k-1)-th node after childBase — which is why
    //    a refit can walk the node range backwards without a stack.
    uint32_t InteriorChildIndex(const float* Node, uint32_t Slot) noexcept
    {
        const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(Node);
        const uint32_t ChildBase = reinterpret_cast<const uint32_t*>(Node)[4];
        uint32_t Rank = 0u;
        for (uint32_t S = 0u; S < Slot; ++S)
            if ((Bytes[24u + S] & 0x18u) == 0x18u) ++Rank;
        return ChildBase + Rank;
    }

    // Is anything in this slot at all? Every empty slot's meta byte is zero, which reads as "leaf with a zero-triangle
    //    run" — that is how the layout represents the empty children of a node with fewer than eight of them.
    bool SlotPresent(const float* Node, uint32_t Slot) noexcept
    {
        const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(Node);
        const uint8_t Meta = Bytes[24u + Slot];
        if ((Meta & 0x18u) == 0x18u) return true;
        const uint32_t Unary = (Meta >> 5u) & 0x07u;
        return Unary == 0x01u || Unary == 0x03u || Unary == 0x07u;
    }

    // Does this node slot hold an interior child?
    // ⚠️ The interior flag is BITS 4 AND 3 SET TOGETHER. The collapse writes `(1 << 5) | (24 + slot)` for an interior
    //    child — 24 = 0b11000, so bits 4 and 3 are always set — and `unaryCount << 5 | firstTriangle` for a leaf. The
    //    traversal's own test is `(meta & (meta << 1)) & 0x10` per byte, whose bit 4 is exactly meta's bit 4 AND bit 3.
    //    Two near-misses to avoid: testing bit 7 misreads every 3-triangle leaf (0b111 << 5 = 0xE0) as a node, and
    //    testing bits 5 and 4 misreads a leaf whose run starts at triangle 16..23 (0x20 | 0b10xxx = 0x30..0x37).
    bool SlotIsInterior(const float* Node, uint32_t Slot) noexcept
    {
        const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(Node);
        const uint8_t Meta = Bytes[24u + Slot];
        return (Meta & 0x18u) == 0x18u;
    }

    // How many triangles a leaf slot owns, and where its run starts within the node's triangle block.
    void LeafSlotRun(const float* Node, uint32_t Slot, uint32_t& OutFirst, uint32_t& OutCount) noexcept
    {
        const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(Node);
        const uint8_t Meta = Bytes[24u + Slot];
        const uint32_t Unary = (Meta >> 5u) & 0x07u;
        OutCount = (Unary == 0x01u) ? 1u : (Unary == 0x03u) ? 2u : (Unary == 0x07u) ? 3u : 0u;
        OutFirst = Meta & 0x1Fu;
    }

    // The descending-sweep property, measured rather than assumed: for every node, every interior child must sit at a
    //    HIGHER local node index (the collapse emits children into a growing arena, so this holds by construction — but
    //    a refit whose correctness rests on it should say so with a number). OutMaxJump is the largest child-parent
    //    distance, which is the width a GPU refit window must have.
    void MeasureChildJumps(const std::vector<float>& NodeBlob, uint32_t NodeBase, uint32_t NodeBlocks,
                           uint32_t& OutMaxJump, bool& OutSweepable) noexcept
    {
        OutMaxJump = 0u;
        OutSweepable = true;
        const uint32_t NodeCount = NodeBlocks / 5u;
        for (uint32_t Local = 0u; Local < NodeCount; ++Local)
        {
            const float* Node = &NodeBlob[(static_cast<size_t>(NodeBase) + static_cast<size_t>(Local) * 5u) * 4u];
            for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
            {
                if (!SlotIsInterior(Node, Slot)) continue;
                const uint32_t ChildLocal = InteriorChildIndex(Node, Slot);   // already a BLAS-local node index
                if (ChildLocal <= Local) { OutSweepable = false; continue; }
                OutMaxJump = std::max(OutMaxJump, ChildLocal - Local);
            }
        }
    }

    // Bounds of `Count` consecutive triangle entries starting at `LeafRunBlock`, a BLOCK offset into the leaf blob
    //    (⚠️ three vec4 per triangle — the node's triangleBase is a block offset, and the meta byte's low five bits are
    //    a TRIANGLE index into that run, so the two must not be added to each other without scaling. Mixing them reads
    //    a triangle three times further along the run, which yields plausible-looking bounds around the wrong geometry —
    //    a bug that only shows up as rays missing, never as a crash).
    void TriangleBounds(const float* Tris, uint32_t LeafRunBlock, uint32_t Count, float OutMin[3], float OutMax[3]) noexcept
    {
        OutMin[0] = OutMin[1] = OutMin[2] =  1.0e30f;
        OutMax[0] = OutMax[1] = OutMax[2] = -1.0e30f;
        for (uint32_t T = 0u; T < Count; ++T)
        {
            // ⚠️ LeafRunBlock is a vec4 BLOCK index (three per triangle, as the node stores it) and Tris is a float
            //    pointer: the *4 is the whole point. Without it every bound is computed from a triangle four entries
            //    earlier, which reads plausible coordinates out of a neighbour's fields — a silent wrong answer.
            const uint32_t Addr = (LeafRunBlock + T * kTriBlocks) * 4u;
            const float* E1 = &Tris[Addr + 0u * 4u];
            const float* E2 = &Tris[Addr + 1u * 4u];
            const float* V0 = &Tris[Addr + 2u * 4u];
            for (int C = 0; C < 3; ++C)
            {
                OutMin[C] = std::min(OutMin[C], std::min(V0[C], std::min(V0[C] + E1[C], V0[C] + E2[C])));
                OutMax[C] = std::max(OutMax[C], std::max(V0[C], std::max(V0[C] + E1[C], V0[C] + E2[C])));
            }
        }
    }
} // namespace

namespace
{
    // The object-space AABB of a BLAS after a deformation: the union of its triangles' boxes. The instance rows
    //    expand this by their own transform every frame, so it has to track the geometry rather than the mesh's
    //    rest pose — a deformed character's AABB is not its bind-pose AABB, and a stale one would drop the instance
    //    out of the top level entirely.
    void TrianglesObjectAabb(const std::vector<TriangleIndex>& Triangles, float OutMin[3], float OutMax[3]) noexcept
    {
        OutMin[0] = OutMin[1] = OutMin[2] =  1.0e30f;
        OutMax[0] = OutMax[1] = OutMax[2] = -1.0e30f;
        for (const TriangleIndex& T : Triangles)
        {
            const float* V[3] = { &T.VertexAlphaX, &T.VertexBetaX, &T.VertexGammaX };
            for (int I = 0; I < 3; ++I)
                for (int C = 0; C < 3; ++C)
                {
                    OutMin[C] = std::min(OutMin[C], V[I][C]);
                    OutMax[C] = std::max(OutMax[C], V[I][C]);
                }
        }
    }
} // namespace

InstanceAcceleration::InstanceAcceleration() noexcept : Impl(std::make_unique<Implementation>()) {}
InstanceAcceleration::~InstanceAcceleration() = default;

bool InstanceAcceleration::Build(const std::vector<MeshPrototype>& Prototypes, const std::vector<InstanceRow>& Rows,
                                 bool HighQuality) noexcept
{
    Instances.clear(); BlasRecords.clear(); BlasPlacements.clear(); NodeBlob.clear(); LeafBlob.clear();
    TlasNodePayload.clear(); TlasPrimitives.clear();
    Metrics = {};
    Impl->Blas.clear(); Impl->PrototypeTris.clear(); Impl->TlasRows.clear();
    Impl->HighQuality = HighQuality;
    if (Prototypes.empty() || Rows.empty()) return false;

    const auto Start = NowMilliseconds();

    Metrics.RefitSweepable = true;   // ANDed with each BLAS below: one unsweepable BLAS disables the descending sweep

    // ── ① BLASes, in object space, one per prototype ────────────────────────────────────────────────────────────────
    Impl->Blas.reserve(Prototypes.size());
    Impl->PrototypeTris.reserve(Prototypes.size());
    BlasRecords.reserve(Prototypes.size());

    for (const MeshPrototype& P : Prototypes)
    {
        if (P.Triangles == nullptr || P.TriangleCount == 0u)
        {
            Instances.clear(); BlasRecords.clear(); BlasPlacements.clear(); NodeBlob.clear(); LeafBlob.clear();
    TlasNodePayload.clear(); TlasPrimitives.clear();
            return false;
        }

        std::vector<TriangleIndex> Tris(P.Triangles, P.Triangles + P.TriangleCount);
        float Min[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
        float Max[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
        for (const TriangleIndex& T : Tris)
        {
            const float V[9] = { T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ,
                                 T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ,
                                 T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ };
            for (int C = 0; C < 3; ++C)
                for (int A = 0; A < 3; ++A)
                {
                    const float Value = V[C * 3 + A];
                    if (Value < Min[A]) Min[A] = Value;
                    if (Value > Max[A]) Max[A] = Value;
                }
        }

        auto Blas = std::make_unique<TraversalIndex>();
        if (!Blas->BuildBottomLevel(Tris, HighQuality))
        {
            Instances.clear(); BlasRecords.clear(); BlasPlacements.clear(); NodeBlob.clear(); LeafBlob.clear();
    TlasNodePayload.clear(); TlasPrimitives.clear();
            return false;
        }

        // Append this BLAS' blobs to the shared buffers and record where they landed (vec4 blocks — the unit both the
        //   5-vec4 node addressing and the 3-vec4 triangle addressing work in, on CPU and in the shader).
        const std::vector<float>& Nodes = Blas->QueryNodeBlob();
        const std::vector<float>& Leaves = Blas->QueryLeafBlob();
        BlasRecord Record{};
        Record.NodeOffset  = static_cast<uint32_t>(NodeBlob.size() / 4u);
        Record.NodeBlocks  = static_cast<uint32_t>(Nodes.size() / 4u);
        Record.LeafOffset  = static_cast<uint32_t>(LeafBlob.size() / 4u);
        Record.LeafBlocks  = static_cast<uint32_t>(Leaves.size() / 4u);
        Record.ObjectAabbMin[0] = Min[0]; Record.ObjectAabbMin[1] = Min[1]; Record.ObjectAabbMin[2] = Min[2];
        Record.ObjectPad        = 0.0f;
        Record.ObjectAabbMax[0] = Max[0]; Record.ObjectAabbMax[1] = Max[1]; Record.ObjectAabbMax[2] = Max[2];
        Record.PrimitiveCount   = P.TriangleCount;
        NodeBlob.insert(NodeBlob.end(), Nodes.begin(), Nodes.end());
        LeafBlob.insert(LeafBlob.end(), Leaves.begin(), Leaves.end());
        BlasRecords.push_back(Record);

        BlasPlacement Placement{};
        Placement.NodeOffset     = Record.NodeOffset;
        Placement.LeafOffset     = Record.LeafOffset;
        Placement.PrimitiveCount = Record.PrimitiveCount;
        Placement.Reserved       = 0u;
        BlasPlacements.push_back(Placement);

        uint32_t Jump = 0u;
        bool Sweepable = true;
        MeasureChildJumps(NodeBlob, Record.NodeOffset, Record.NodeBlocks, Jump, Sweepable);
        Metrics.MaxChildIndexJump = std::max(Metrics.MaxChildIndexJump, Jump);
        Metrics.RefitSweepable = Metrics.RefitSweepable && Sweepable;

        Metrics.PrimitiveCount += P.TriangleCount;
        Impl->Blas.push_back(std::move(Blas));
        Impl->PrototypeTris.push_back(std::move(Tris));
    }

    Metrics.BlasCount     = static_cast<uint32_t>(Impl->Blas.size());
    Metrics.NodeBytes     = NodeBlob.size() * sizeof(float);
    Metrics.LeafBytes     = LeafBlob.size() * sizeof(float);
    Metrics.BlasBytes     = BlasRecords.size() * sizeof(BlasRecord);

    // ── ② rows + TLAS ───────────────────────────────────────────────────────────────────────────────────────────────
    Instances.resize(Rows.size());
    Impl->TlasRows.resize(Rows.size());
    Impl->TlasBlasList.assign(Impl->Blas.size(), nullptr);

    if (!UpdateTopLevel(Rows)) return false;

    Metrics.BuildMilliseconds = static_cast<float>(NowMilliseconds() - Start);
    Metrics.InstanceBytes     = Instances.size() * sizeof(TlasInstanceRecord);
    return true;
}

bool InstanceAcceleration::UpdateTopLevel(const std::vector<InstanceRow>& Rows) noexcept
{
    if (Instances.size() != Rows.size() || Rows.empty()) return false;
    for (const InstanceRow& R : Rows)
        if (R.BlasIndex >= Impl->Blas.size()) return false;

    const double Start = NowMilliseconds();

    for (size_t I = 0; I < Rows.size(); ++I)
    {
        const InstanceRow& Row = Rows[I];
        const BlasRecord&  Blas = BlasRecords[Row.BlasIndex];

        TlasInstanceRecord& Out = Instances[I];
        std::memcpy(Out.Inverse, Row.Transform, sizeof(Out.Inverse));
        float Inverse[16];
        if (!InvertMatrix(Row.Transform, Inverse))
        {
            // A singular transform is a scene bug, not a frame to skip: keep the row's forward matrix out of the
            //    trace by collapsing its AABB to the origin instead of building a garbage tree.
            std::memset(Out.Inverse, 0, sizeof(Out.Inverse));
            Out.AabbMin[0] = Out.AabbMin[1] = Out.AabbMin[2] = Out.AabbMin[3] = 0.0f;
            Out.AabbMax[0] = Out.AabbMax[1] = Out.AabbMax[2] = Out.AabbMax[3] = 0.0f;
        }
        else
        {
            std::memcpy(Out.Inverse, Inverse, sizeof(Out.Inverse));
            TransformAabb(Row.Transform, Blas.ObjectAabbMin, Blas.ObjectAabbMax, Out.AabbMin, Out.AabbMax);
        }
        Out.AabbMin[3] = 0.0f;
        Out.AabbMax[3] = 0.0f;
        Out.BlasIndex     = Row.BlasIndex;
        Out.FirstTriangle = Row.FirstTriangle;
        Out.Flags         = Row.Flags;
        Out.Pad           = 0u;

        // tinybvh's row: the same transform pair and AABB, in the layout its TLAS builder reads. blasIdx is set but
        //   the BLAS pointer list passed to Build is null, so tinybvh never dereferences it (it would insist on a
        //   BLAS layout it can traverse itself, and ours are CWBVH — the CPU walker below is the reference).
        tinybvh::BLASInstance& TlasRow = Impl->TlasRows[I];
        for (int K = 0; K < 16; ++K) TlasRow.transform[K] = Row.Transform[K];
        for (int K = 0; K < 16; ++K) TlasRow.invTransform[K] = Out.Inverse[K];
        TlasRow.aabbMin = tinybvh::bvhvec3(Out.AabbMin[0], Out.AabbMin[1], Out.AabbMin[2]);
        TlasRow.aabbMax = tinybvh::bvhvec3(Out.AabbMax[0], Out.AabbMax[1], Out.AabbMax[2]);
        TlasRow.blasIdx = Row.BlasIndex;
        TlasRow.mask    = 0xFFFFFFFFu;
    }

    const double TlasStart = NowMilliseconds();
    Impl->Tlas.Build(Impl->TlasRows.data(), static_cast<uint32_t>(Impl->TlasRows.size()), nullptr,
                     static_cast<uint32_t>(Impl->Blas.size()));
    const double TlasEnd = NowMilliseconds();

    // ── the kernel's copy of the top level ──────────────────────────────────────────────────────────────────────────
    // tinybvh's 32 B node, written out as 8 floats: [min.xyz, leftFirst bits, max.xyz, primitive-count bits]. The leaf
    //    indirection through primIdx is baked into TlasPrimitives so the traversal never resolves it. Bit-casts keep
    //    the integer fields intact through a float array — the same trick the triangle blob already uses for its
    //    primitive index in v0.w.
    const uint32_t NodeCount = Impl->Tlas.usedNodes;
    TlasNodePayload.resize(size_t(NodeCount) * 8u);
    for (uint32_t N = 0u; N < NodeCount; ++N)
    {
        const tinybvh::BVH::BVHNode& Source = Impl->Tlas.bvhNode[N];
        float* Out = &TlasNodePayload[size_t(N) * 8u];
        Out[0] = Source.aabbMin.x; Out[1] = Source.aabbMin.y; Out[2] = Source.aabbMin.z;
        const uint32_t LeftFirst = Source.leftFirst;
        const uint32_t PrimCount = Source.triCount;
        std::memcpy(&Out[3], &LeftFirst, sizeof(uint32_t));
        Out[4] = Source.aabbMax.x; Out[5] = Source.aabbMax.y; Out[6] = Source.aabbMax.z;
        std::memcpy(&Out[7], &PrimCount, sizeof(uint32_t));
    }
    TlasPrimitives.assign(Impl->Tlas.primIdx, Impl->Tlas.primIdx + Impl->Tlas.idxCount);

    Metrics.InstanceCount    = static_cast<uint32_t>(Rows.size());
    Metrics.TlasNodeCount    = Impl->Tlas.usedNodes;
    Metrics.UpdateMilliseconds   = static_cast<float>(TlasEnd - Start);
    Metrics.TlasOnlyMilliseconds = static_cast<float>(TlasEnd - TlasStart);
    Metrics.InstanceBytes    = Instances.size() * sizeof(TlasInstanceRecord);
    return true;
}


void InstanceAcceleration::UpdateBlasObjectAabb(uint32_t BlasIndex, const std::vector<TriangleIndex>& Triangles) noexcept
{
    if (BlasIndex >= BlasRecords.size()) return;
    float Lo[3], Hi[3];
    TrianglesObjectAabb(Triangles, Lo, Hi);
    if (!(Lo[0] <= Hi[0])) return;
    for (int C = 0; C < 3; ++C) { BlasRecords[BlasIndex].ObjectAabbMin[C] = Lo[C]; BlasRecords[BlasIndex].ObjectAabbMax[C] = Hi[C]; }
}

bool InstanceAcceleration::QueryBlasObjectAabb(uint32_t BlasIndex, float OutMin[3], float OutMax[3]) const noexcept
{
    if (BlasIndex >= BlasRecords.size()) return false;
    const BlasRecord& Record = BlasRecords[BlasIndex];
    for (int C = 0; C < 3; ++C) { OutMin[C] = Record.ObjectAabbMin[C]; OutMax[C] = Record.ObjectAabbMax[C]; }
    return true;
}

// D8's refit, and the only shape a per-BLAS update can take in a two-level structure.
//
//    The layout gives every BLAS a fixed slice of the shared buffer, addressed by the offsets in BlasPlacements —
//    which is what lets ONE descriptor set and ONE dispatch cover the whole scene. So an update must keep this
//    BLAS' block counts exactly, and the supported re-emit path does NOT: re-collapsing the MBVH8 after a refit
//    re-runs the collapse's grouping, whose node count depends on the (new) bounds. Measured on the level's own
//    soup: +125 node blocks at 4 608 triangles, +540 at 10 368, +230 at 4 000 clustered — never zero for a
//    deformed mesh, so a collapse-based refit would push every following BLAS' slice along and invalidate the
//    placements and the TLAS payload that reference them.
//
//    Hence the sweep: rewrite this BLAS' leaf triangles, then re-quantise its nodes IN PLACE, descending, reusing
//    the grouping that is already there (a refit does not re-split, so it is still the right grouping). Cost is
//    O(this BLAS's nodes) with no collapse and no compression, and the block counts cannot change by construction.
//
//    Two things make the sweep correct rather than merely fast, and a gate measures both instead of assuming them:
//      · every interior child sits at a HIGHER block index than its parent (Metrics.MaxChildIndexJump records the
//        largest such jump), so descending order visits every child before its parent — no stack, no recursion, and
//        the same property is what will let a GPU kernel run this as descending windows (D9);
//      · a node's stored box is exactly what the traversal decodes, so a parent's re-quantisation of its children is
//        conservative by construction (floor/ceil over the child's own decoded box).
//
//    The CPU porter walks the inner binary tree, not this blob, so the tree has to be refitted as well — that is the
//    cheap half (0.22 ms at 16 k triangles) and skipping it would leave the two ways of tracing the same BLAS
//    describing different geometry.
bool InstanceAcceleration::RefitBlas(uint32_t BlasIndex, const std::vector<TriangleIndex>& DeformedTriangles) noexcept
{
    if (BlasIndex >= BlasRecords.size() || BlasIndex >= Impl->Blas.size())
    {
        ++Metrics.RefitRefusedCount;
        return false;
    }
    BlasRecord& Record = BlasRecords[BlasIndex];
    if (!Impl->Blas[BlasIndex]->IsRefittable() || DeformedTriangles.size() != Record.PrimitiveCount)
    {
        ++Metrics.RefitRefusedCount;   // topology changed, or a split build: that is a rebuild
        return false;
    }

    const double Start = NowMilliseconds();

    // The CPU porter walks the inner binary tree, so that half has to be refitted too — it is the cheap 0.22 ms,
    //    and skipping it would leave the two ways of tracing the same BLAS describing different geometry.
    if (!Impl->Blas[BlasIndex]->RefitTriangleTree(DeformedTriangles))
    {
        ++Metrics.RefitRefusedCount;
        return false;
    }
    const uint32_t NodeBase = Record.NodeOffset;
    const uint32_t LeafBase = Record.LeafOffset;

    // ── ① leaves: re-emit every triangle the BLAS owns, keeping each entry's own primitive index ─────────────────────
    // The blob's triangle order is the TREE's order, not the soup's, so the mapping is read back out of the blob itself:
    //    block 2's w carries the object-space triangle index (the same field the traversal returns as v0.w). That is why
    //    a refit needs no side table: the blob is self-describing.
    uint32_t Rewritten = 0u;
    for (uint32_t Block = 0u; Block < Record.LeafBlocks; Block += kTriBlocks)
    {
        float* Entry = &LeafBlob[(static_cast<size_t>(LeafBase) + Block) * 4u];
        uint32_t PrimitiveIndex = 0u;
        std::memcpy(&PrimitiveIndex, &Entry[11], sizeof(uint32_t));   // v0.w
        if (PrimitiveIndex >= DeformedTriangles.size()) { ++Metrics.RefitRefusedCount; return false; }
        const TriangleIndex& T = DeformedTriangles[PrimitiveIndex];
        const float V0[3] = { T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ };
        const float V1[3] = { T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ  };
        const float V2[3] = { T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ };
        // e1 = v2 − v0, e2 = v1 − v0, v0 — tinybvh's convention, which the kernel's Möller–Trumbore reads.
        for (int C = 0; C < 3; ++C)
        {
            Entry[0u * 4u + C] = V2[C] - V0[C];
            Entry[1u * 4u + C] = V1[C] - V0[C];
            Entry[2u * 4u + C] = V0[C];
        }
        ++Rewritten;
    }

    // ── ② nodes: bottom-up, in DESCENDING block order ────────────────────────────────────────────────────────────────
    // A CWBVH node's interior children always sit at higher block indices than the node itself (the collapse emits them
    //    depth-first into a growing arena), so walking the node range backwards visits every child before its parent —
    //    no recursion, no stack, and the same property is what lets the GPU kernel run as descending windows. Build
    //    measures the largest such jump (Metrics.MaxChildIndexJump) so the host can size those windows.
    const uint32_t NodeCount = Record.NodeBlocks / kNodeBlocks;
    uint32_t RefitNodes = 0u;
    for (uint32_t Local = NodeCount; Local-- > 0u; )
    {
        float* Node = &NodeBlob[(static_cast<size_t>(NodeBase) + static_cast<size_t>(Local) * kNodeBlocks) * 4u];

        float ChildMin[8][3];
        float ChildMax[8][3];
        bool  ChildPresent[8];
        for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
        {
            ChildPresent[Slot] = false;
            if (SlotIsInterior(Node, Slot))
            {
                const uint32_t ChildLocal = InteriorChildIndex(Node, Slot);   // BLAS-local, as stored
                if (ChildLocal >= NodeCount) { ++Metrics.RefitRefusedCount; return false; }
                const float* Child = &NodeBlob[(static_cast<size_t>(NodeBase) + static_cast<size_t>(ChildLocal) * kNodeBlocks) * 4u];
                ChildPresent[Slot] = true;
                for (int C = 0; C < 3; ++C) { ChildMin[Slot][C] = Child[C]; ChildMax[Slot][C] = Child[C]; }
                // The child's bound is its quantised box, which is what the parent must contain: read it back exactly as
                //    the traversal will, so the parent's bytes cannot disagree with the child's own extent.
                float DecodedMin[3], DecodedMax[3];
                DecodeChildBounds(Child, 0u, DecodedMin, DecodedMax);
                float BoxMin[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
                float BoxMax[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
                for (uint32_t Inner = 0u; Inner < 8u; ++Inner)
                {
                    if (!SlotPresent(Child, Inner)) continue;
                    DecodeChildBounds(Child, Inner, DecodedMin, DecodedMax);
                    for (int C = 0; C < 3; ++C) { BoxMin[C] = std::min(BoxMin[C], DecodedMin[C]); BoxMax[C] = std::max(BoxMax[C], DecodedMax[C]); }
                }
                if (BoxMin[0] <= BoxMax[0])
                    for (int C = 0; C < 3; ++C) { ChildMin[Slot][C] = BoxMin[C]; ChildMax[Slot][C] = BoxMax[C]; }
            }
            else
            {
                uint32_t First = 0u, Count = 0u;
                LeafSlotRun(Node, Slot, First, Count);
                if (Count == 0u) continue;
                ChildPresent[Slot] = true;
                const uint32_t TriBaseBlocks = static_cast<uint32_t>(reinterpret_cast<const uint32_t*>(Node)[5]);
                TriangleBounds(&LeafBlob[static_cast<size_t>(LeafBase) * 4u], TriBaseBlocks + First * kTriBlocks, Count,
                               ChildMin[Slot], ChildMax[Slot]);
            }
        }

        EncodeNode(Node, ChildMin, ChildMax, ChildPresent);
        ++RefitNodes;
    }

    Impl->PrototypeTris[BlasIndex] = DeformedTriangles;
    UpdateBlasObjectAabb(BlasIndex, DeformedTriangles);

    Metrics.RefitMilliseconds      = static_cast<float>(NowMilliseconds() - Start);
    Metrics.LastRefitNodeCount     = RefitNodes;
    Metrics.LastRefitTriangleCount = Rewritten;
    ++Metrics.RefitCount;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             CPU REFERENCE TRACE (proofs)
//------------------------------------------------------------------------------------------------------------------------

bool InstanceAcceleration::TraceClosest(const float Origin[3], const float Direction[3], float MaxDistance,
                                        uint32_t& OutInstance, uint32_t& OutPrimitive, float& OutDistance) const noexcept
{
    OutInstance = 0xFFFFFFFFu; OutPrimitive = 0xFFFFFFFFu; OutDistance = MaxDistance;
    if (!Impl->Tlas.bvhNode || Instances.empty() || Impl->Tlas.usedNodes == 0u) return false;

    // ⚠️ THE DIRECTION IS NORMALISED ONCE, HERE, and then never again.
    //
    //    Normalising up front gives this function the same contract as the world path (TraversalIndex::TraceClosest →
    //    tinybvh::Ray): t is METRES and MaxDistance is metres. For the unit directions this engine traces — camera
    //    rays, light directions, the kernel's own — the division is by exactly 1.0f, i.e. a no-op, and that is what
    //    makes the identity transform BIT-IDENTICAL rather than merely close: the object-space direction is then
    //    `M⁻¹·D` with M⁻¹ = the exact identity, so the BLAS walk sees the very same ray the world tree would.
    //
    //    From there on nothing normalises (TraversalIndex::TraceClosestObjectSpace takes the direction as given, the
    //    way TraversalCWBVH.slang's TraverseClosest does with rD = 1/D), so a rigidly moved instance costs a 4×4
    //    transform and one divide — no tree work, no re-fitted bounds, no re-emitted blobs.
    const float DL = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1] + Direction[2] * Direction[2]);
    if (!(DL > 0.0f)) return false;
    const float InvDL = 1.0f / DL;
    const tinybvh::bvhvec3 O(Origin[0], Origin[1], Origin[2]);
    const tinybvh::bvhvec3 D(Direction[0] * InvDL, Direction[1] * InvDL, Direction[2] * InvDL);
    const tinybvh::bvhvec3 rD = tinybvh::tinybvh_rcp(D);   // ±inf on axis-aligned rays: the slab test handles it

    // Slab test against a 32 B node AABB, in the ray's own parameterisation (t along D, not normalised).
    const auto NodeIntersect = [&](const tinybvh::bvhvec3& BMin, const tinybvh::bvhvec3& BMax, float TMax) -> bool
    {
        const float Tx0 = (BMin.x - O.x) * rD.x, Tx1 = (BMax.x - O.x) * rD.x;
        const float Ty0 = (BMin.y - O.y) * rD.y, Ty1 = (BMax.y - O.y) * rD.y;
        const float Tz0 = (BMin.z - O.z) * rD.z, Tz1 = (BMax.z - O.z) * rD.z;
        const float TMin = std::max(std::max(std::min(Tx0, Tx1), std::min(Ty0, Ty1)), std::min(Tz0, Tz1));
        const float TExit = std::min(std::min(std::max(Tx0, Tx1), std::max(Ty0, Ty1)), std::max(Tz0, Tz1));
        return TMin <= std::min(TExit, TMax) && TExit >= 0.0f;
    };

    float Best = MaxDistance;
    uint32_t StackNode[64];
    int StackCount = 0;
    StackNode[StackCount++] = 0u;   // root

    while (StackCount > 0)
    {
        const uint32_t NodeIndex = StackNode[--StackCount];
        const tinybvh::BVH::BVHNode& Node = Impl->Tlas.bvhNode[NodeIndex];
        if (!NodeIntersect(Node.aabbMin, Node.aabbMax, Best)) continue;

        if (Node.isLeaf())
        {
            for (uint32_t K = 0; K < Node.triCount; ++K)
            {
                const uint32_t InstanceIndex = Impl->Tlas.primIdx[Node.leftFirst + K];
                const TlasInstanceRecord& Row = Instances[InstanceIndex];
                if (Row.BlasIndex >= Impl->Blas.size()) continue;

                // Object space: O' = M⁻¹·O, D' = M⁻¹·D (no normalisation — t is preserved).
                const float* Inv = Row.Inverse;
                const float OO[3] = { Inv[0]*O.x + Inv[4]*O.y + Inv[8]*O.z  + Inv[12],
                                      Inv[1]*O.x + Inv[5]*O.y + Inv[9]*O.z  + Inv[13],
                                      Inv[2]*O.x + Inv[6]*O.y + Inv[10]*O.z + Inv[14] };
                const float OD[3] = { Inv[0]*D.x + Inv[4]*D.y + Inv[8]*D.z,
                                      Inv[1]*D.x + Inv[5]*D.y + Inv[9]*D.z,
                                      Inv[2]*D.x + Inv[6]*D.y + Inv[10]*D.z };

                // ⚠️ t NEEDS NO RESCALE, and that is the whole trick. The walker works in the parameterisation of the
                //    direction it is given, so for a hit at parameter t: M·(O' + t·D') = O + t·(M·D') = O + t·D — the
                //    same t, on the same world ray, in metres when the caller's D is unit. Scale, rotation and
                //    translation all come out in the wash; nothing here divides by |M⁻¹·D| or re-fits a bound. (This is
                //    also exactly what the kernel does: it hands TraverseClosest the object-space O, D and rD = 1/D.)
                float T = Best;
                uint32_t LocalPrimitive = 0u;
                if (Impl->Blas[Row.BlasIndex]->TraceClosestObjectSpace(OO, OD, Best, T, LocalPrimitive) && T < Best && T > 0.0f)
                {
                    Best = T;
                    OutInstance = InstanceIndex;
                    OutPrimitive = LocalPrimitive;
                }
            }
            continue;
        }

        if (StackCount + 2 <= 64)
        {
            StackNode[StackCount++] = Node.leftFirst;
            StackNode[StackCount++] = Node.leftFirst + 1u;
        }
        else
        {
            // 64 slots is four times the depth a balanced tree over 2^31 instances needs; if a malformed tree ever got
            //   here the honest answer is a miss, not a stack smash.
            OutInstance = 0xFFFFFFFFu; OutPrimitive = 0xFFFFFFFFu; OutDistance = MaxDistance;
            return false;
        }
    }

    if (OutInstance == 0xFFFFFFFFu) return false;
    OutDistance = Best;
    return true;
}

const std::vector<TriangleIndex>& InstanceAcceleration::QueryPrototypeTriangles(uint32_t BlasIndex) const noexcept
{
    static const std::vector<TriangleIndex> Empty;
    if (BlasIndex >= Impl->PrototypeTris.size()) return Empty;
    return Impl->PrototypeTris[BlasIndex];
}


//------------------------------------------------------------------------------------------------------------------------
//                                     D8 — POLICY AND DEFORMATION MEASUREMENT
//------------------------------------------------------------------------------------------------------------------------

BlasUpdateDecision BlasUpdatePolicy::Decide(float Displacement, float PrimitiveSize, bool TopologyChanged,
                                            uint32_t FramesSinceRebuild, bool RebuildOutstanding) const noexcept
{
    if (TopologyChanged) return BlasUpdateDecision::Rebuild;
    // A degenerate mesh has no scale to compare against: rebuilding is the only safe answer.
    if (!(PrimitiveSize > 0.0f)) return BlasUpdateDecision::Rebuild;
    if (!(Displacement > 0.0f)) return BlasUpdateDecision::None;
    if (Displacement < RefitDisplacementRatio * PrimitiveSize) return BlasUpdateDecision::Refit;
    if (RebuildOutstanding || FramesSinceRebuild < RebuildCooldownFrames) return BlasUpdateDecision::Refit;   // refit is still
                                                    // better than nothing while a rebuild is pending or cooling down
    return BlasUpdateDecision::Rebuild;
}

void MeasureDeformation(const std::vector<TriangleIndex>& Before, const std::vector<TriangleIndex>& After,
                        float& OutDisplacement, float& OutPrimitiveSize) noexcept
{
    OutDisplacement = 0.0f;
    OutPrimitiveSize = 0.0f;
    const size_t Count = std::min(Before.size(), After.size());
    if (Count == 0u) return;

    double EdgeSum = 0.0;
    for (size_t I = 0; I < Count; ++I)
    {
        const TriangleIndex& A = Before[I];
        const TriangleIndex& B = After[I];
        const float* AV[3] = { &A.VertexAlphaX, &A.VertexBetaX, &A.VertexGammaX };
        const float* BV[3] = { &B.VertexAlphaX, &B.VertexBetaX, &B.VertexGammaX };
        for (int V = 0; V < 3; ++V)
        {
            const float Dx = BV[V][0] - AV[V][0], Dy = BV[V][1] - AV[V][1], Dz = BV[V][2] - AV[V][2];
            OutDisplacement = std::max(OutDisplacement, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        }
        for (int E = 0; E < 3; ++E)
        {
            const float* P = AV[E];
            const float* Q = AV[(E + 1) % 3];
            const float Dx = Q[0] - P[0], Dy = Q[1] - P[1], Dz = Q[2] - P[2];
            EdgeSum += std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        }
    }
    OutPrimitiveSize = static_cast<float>(EdgeSum / (3.0 * static_cast<double>(Count)));
}

} // namespace Frontier

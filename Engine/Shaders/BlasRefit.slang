//============================================================================================================================================
// 📦 Engine/Shaders/BlasRefit.slang — D9: the wide-AABB refit of one BLAS, in place, one dispatch per level
//============================================================================================================================================
// 🧩 The GPU half of D8's update path: take a BLAS whose topology has NOT changed, rewrite its leaf triangles from the
//    deformed object-space soup, and re-quantise its nodes bottom-up — in place, so every block count, every offset in
//    BlasPlacements and every TLAS payload entry stay valid. That is the whole reason it exists: the collapse path
//    re-emits and re-groups the BLAS (D8 measured +125/+230/+540 node blocks for a deformed mesh), which would push
//    every following BLAS' slice along.
//
// TWO STAGES, both dispatched over the whole BLAS (the host loops the stages, the level loop is stage 1's):
//    Stage 0 — LEAVES.  One thread per triangle record: read the record's own primitive index out of v0.w, fetch the
//               deformed vertices from the soup, write e1 = v2 − v0, e2 = v1 − v0, v0. The blob is self-describing, so
//               no side table and no permutation of the soup is needed.
//    Stage 1 — NODES, deepest level first.  One thread per node of that level (early-out on the level table, so the
//               dispatch can cover the BLAS' whole node range). Each node's slot bounds come from what the traversal
//               will actually test: an interior child contributes the union of its own present decoded slot boxes
//               (falling back to its `p` when it has none), a leaf child contributes the exact bounds of the triangles
//               in its run, read back out of the leaf blob stage 0 just rewrote. Then BlasQuantiseNode.
//
// WHY LEVELS AND NOT A DESCENDING SWEEP: every interior child sits at a HIGHER node index than its parent, so a
//    descending index sweep is also correct — but a *parallel* one needs each window to exclude parent/child pairs,
//    and D8 measured the largest child-index jump at 4 125 out of 4 827 nodes on this level, i.e. one safe window would
//    be the whole arena. Levels are the usable independent sets: stage 1's dispatch per level touches only nodes whose
//    children were finished by an earlier dispatch. §⑨c measures the table, §⑨e proves the two orders agree BYTE FOR
//    BYTE on the same blob (4 827 nodes and 31 927 triangles compared, 0 differences), which is what makes this transcription safe.
//
// ⚠️ NOT RUN ON A GPU. There is no Vulkan device in the development sandbox, so this file is compile-gated by
//    Tools/Build/CheckShaders.sh and text-pinned by §⑩ of Exhibits/Workbench/Traversal/TwoLevelBvhProof.cpp; the CPU
//    mirror it is transcribed from (BlasBuildMirror::RefitLevelOrder) is what §⑨ actually executes. The host's half is
//    written down and gated too — `Engine/GeometricRaster/BlasDevicePayload.{h,cpp}` holds this kernel's push block, the
//    deformed-soup and level-table packers, and the dispatch plan §⑨i checks (leaves, then one dispatch per level from
//    the table's deepest down to 0) — so what remains for a device session is the Vulkan plumbing around it: buffers,
//    descriptors, barriers, and the run itself. See Docs/DynamicGeometry.md §5.
//    ⚠️ TO RUN IT ON A MACHINE WITH A GPU: `bash Exhibits/Workbench/Traversal/RunBlasDevice.sh --refit`, which checks this
//       kernel's output against `BlasBuildMirror::RefitLevelOrder`'s byte for byte.
//
// NOTE: GLSL source lowered by glslc -fshader-stage=compute.
#version 460
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "TraversalRecords.slang"   // TraversalBlasPlacement: NodeOffset / LeafOffset / PrimitiveCount in vec4 blocks
#include "BlasLayout.slang"

layout(std430, binding = 0) buffer BlasNodeExtent      { vec4 BlasNodes[]; };   // D9: this BLAS' nodes, read/write
layout(std430, binding = 1) buffer BlasLeafExtent      { vec4 BlasLeaves[]; };  // D9: this BLAS' leaf records, read/write
layout(std430, binding = 2) readonly buffer BlasSoupExtent { vec4 BlasSoup[]; };// D9: deformed object-space soup, 3 × vec4
layout(std430, binding = 3) readonly buffer BlasPlacementExtent { TraversalBlasPlacement BlasPlacements[]; };
layout(std430, binding = 4) readonly buffer BlasLevelExtent { uint BlasLevels[]; };  // one BFS level per node, 0xFFFFFFFF = unreachable

// 16 B. A refit is per-BLAS work, so the dispatch says which BLAS and which slice of the work it is.
layout(push_constant) uniform BlasRefitConstants
{
    uint BlasIndex;   // [idx] index into BlasPlacements — picks the node/leaf ranges
    uint Stage;       // [-]   0 = rewrite the leaf triangles, 1 = re-quantise one level's nodes
    uint Level;       // [lvl] stage 1: which BFS level (the host counts down from the deepest)
    uint Pad;         // [-]   0
};

void main()
{
    const TraversalBlasPlacement Placement = BlasPlacements[BlasIndex];
    const uint NodeCount = Placement.Reserved;   // D9: Reserved carries the BLAS' node count (NodeBlocks / 5)
    const uint Thread = gl_GlobalInvocationID.x;

    // ─── stage 0: the leaf triangles ─────────────────────────────────────────────────────────────────────────────────
    if (Stage == 0u)
    {
        if (Thread >= Placement.PrimitiveCount) return;
        const uint Block = Placement.LeafOffset + kBlasTriBlocks * Thread;
        // The record carries its own object-space triangle index in v0.w (the same field the traversal returns to the
        //    caller). A record whose index is out of range is left alone rather than clamped: a refit that invented
        //    geometry would be worse than one that refused.
        const uint Primitive = floatBitsToUint(BlasLeaves[Block + 2u].w);
        if (Primitive >= Placement.PrimitiveCount) return;
        const vec4 V0 = BlasSoup[3u * Primitive + 0u];
        const vec4 V1 = BlasSoup[3u * Primitive + 1u];
        const vec4 V2 = BlasSoup[3u * Primitive + 2u];
        BlasLeaves[Block + 0u] = vec4(V2.xyz - V0.xyz, 0.0f);              // e1 = v2 − v0
        BlasLeaves[Block + 1u] = vec4(V1.xyz - V0.xyz, 0.0f);              // e2 = v1 − v0
        BlasLeaves[Block + 2u] = vec4(V0.xyz, uintBitsToFloat(Primitive)); // v0 keeps the primitive index
        return;
    }

    // ─── stage 1: one level's nodes, deepest first ───────────────────────────────────────────────────────────────────
    if (Thread >= NodeCount) return;
    if (BlasLevels[Placement.NodeOffset / kBlasNodeBlocks + Thread] != Level) return;

    const uint NodeAt = Placement.NodeOffset + kBlasNodeBlocks * Thread;
    vec4 Node[5];
    for (uint W = 0u; W < kBlasNodeBlocks; ++W) Node[W] = BlasNodes[NodeAt + W];

    vec3 SlotMin[8];
    vec3 SlotMax[8];
    uint Presence = 0u;
    for (uint Slot = 0u; Slot < 8u; ++Slot)
    {
        const uint Meta = BlasMeta(Node, Slot);
        if (!BlasSlotPresent(Meta)) continue;
        Presence |= 1u << Slot;
        SlotMin[Slot] = vec3( 1.0e30f);
        SlotMax[Slot] = vec3(-1.0e30f);

        if (BlasIsInteriorMeta(Meta))
        {
            // Fallback first: a child with no present slot is covered by its own `p`, exactly as the CPU sweep does it.
            const uint Child = BlasChildBase(Node) + BlasInteriorRank(Node, Slot);
            if (Child >= NodeCount) continue;                 // malformed: leave this slot as it was
            const uint ChildAt = Placement.NodeOffset + kBlasNodeBlocks * Child;
            vec4 C[5];
            for (uint W = 0u; W < kBlasNodeBlocks; ++W) C[W] = BlasNodes[ChildAt + W];
            SlotMin[Slot] = C[0].xyz;
            SlotMax[Slot] = C[0].xyz;
            bool Any = false;
            vec3 Lo = vec3( 1.0e30f);
            vec3 Hi = vec3(-1.0e30f);
            for (uint Inner = 0u; Inner < 8u; ++Inner)
            {
                if (!BlasSlotPresent(BlasMeta(C, Inner))) continue;
                vec3 InnerLo, InnerHi;
                BlasSlotBox(C, Inner, InnerLo, InnerHi);
                Lo = min(Lo, InnerLo);
                Hi = max(Hi, InnerHi);
                Any = true;
            }
            if (Any) { SlotMin[Slot] = Lo; SlotMax[Slot] = Hi; }
        }
        else
        {
            const uint First = BlasLeafRunOffset(Meta);
            const uint Count = BlasLeafCount(Meta);
            const uint TriBase = BlasTriBase(Node);
            vec3 Lo = vec3( 1.0e30f);
            vec3 Hi = vec3(-1.0e30f);
            for (uint J = 0u; J < Count; ++J)
            {
                const uint Block = Placement.LeafOffset + TriBase + (First + J) * kBlasTriBlocks;
                vec3 TriLo, TriHi;
                BlasTriangleBounds(BlasLeaves[Block + 0u], BlasLeaves[Block + 1u], BlasLeaves[Block + 2u], TriLo, TriHi);
                Lo = min(Lo, TriLo);
                Hi = max(Hi, TriHi);
            }
            SlotMin[Slot] = Lo;
            SlotMax[Slot] = Hi;
        }
    }

    BlasQuantiseNode(Node, SlotMin, SlotMax, Presence);
    for (uint W = 0u; W < kBlasNodeBlocks; ++W) BlasNodes[NodeAt + W] = Node[W];
}

//============================================================================================================================================
// 📦 Engine/Shaders/BlasBuild.slang — D9: a full wide build into the packed CWBVH layout, for topology changes
//============================================================================================================================================
// 🧩 A refit cannot cover a topology change (LOD swap, destruction, a mesh authored on the fly): the node count itself
//    moves, so the blob has to be built rather than updated. This kernel builds the SAME layout the refit writes and the
//    traversal reads — a second traversal path is the cost the plan named, and this avoids it.
//
// THE RULE (measured, not chosen by taste — §⑨g of the two-level gate walks three builds of the M10 level over the same
// 12 000 rays and clocks them):
//
//   | build rule                       | nodes  | build  | walk    | triangle tests |
//   |----------------------------------|--------|--------|---------|----------------|
//   | D9 v1: octant, no pack           | 17 835 | 17.9 ms| 10.93 ms|      36 700    |
//   | clustered: count-balanced + SAH  |  4 681 | 15.8 ms| 19.73 ms|     165 229    |
//   | SHIPPED: octant + the format rule|  7 185 | 12.5 ms| 10.11 ms|      63 462    |
//
//   1. A node whose Morton range is at most 24 triangles — eight leaf slots of three — becomes LEAF RUNS of three and is
//      never recursed into. D9's first build re-split those ranges (four times the nodes, 64 % of the wide slots empty).
//   2. Otherwise the range is cut by OCTANT: the tree mirrors the Morton octree, and each child's box is the tight box
//      of its own octant. This is the part a "smarter" clustering broke: cutting by count makes shallow trees with full
//      wide nodes (4 681 nodes, 26 % empty slots) and walks 60-95 % SLOWER, because merging bins that are far apart makes
//      a child's box the union of the far parts — a ray the octant rule rejects at the box descends and tests triangles.
//      A wide-node build is a box-tightness trade, not a node-count one. The clustered rule is kept in the CPU mirror
//      (`BlasBuildMirror::BlasPartition::Clustered`) precisely so that this stays measurable rather than folklore; the
//      device ships the octant rule only.
//   3. Once the Morton bits are exhausted (level 10) a range too large for a node is cut into eight count-balanced
//      PIECES instead — per-octant splitting there could ask for 64 children from a node that has eight slots.
//
// Stages, all in this one entry point (the host loops them; `Stage` picks one):
//    Stage 0 — PREPASS     one thread per triangle: exact bounds, centroid, 30-bit Morton code (10 bits per axis,
//                          normalised against the object AABB in the push block), written as uvec2(key, primitive) into
//                          the array in soup order, plus the root's range and level.
//    Stage 1 — PARTITION   one workgroup per node of the level. This is what replaces a separate sort: at level d every
//                          node's range is stably counting-sorted by the octant its keys name at depth d, so after d
//                          levels the array IS Morton-ordered inside every node — no radix passes, no key prefix sum, and
//                          a packed node (rule 1) needs no scatter at all because it has no children below it. Stability
//                          is not decoration: a different order inside an octant changes which triangles share a leaf,
//                          i.e. the blob's bytes, so the tile loop computes each element's rank within its octant
//                          explicitly instead of using atomics. The stage also writes each child's range, its preferred
//                          slot and the node's total leaf triangles.
//    Stage 2 — COUNT+SCAN  one workgroup per 128 node slots: each lane counts its own node's interior children, the
//                          workgroup scans them, and the block total goes to `BlasBlockSums`. Children are numbered in
//                          ASCENDING SLOT ORDER (the rank contract the traversal's imask pop depends on), which is why this
//                          is a scan over the level in node order rather than an atomic ticket per node.
//    Stage 4 — BLOCK SCAN  one workgroup: exclusive prefix of the block totals, and the next level's base/count. Still a
//                          serial loop, but one iteration per BLOCK (nodes / 128) rather than per node.
//    Stage 3 — EMIT        one lane per node: take each child's exact bounds from the geometry its range names,
//                          assign the eight slots (octant preference, free-slot pool), write the node's p/exponents/
//                          qlo/qhi, its meta bytes, its imask and its childBase, hand each interior child its range, and
//                          record its stored slot and level.
//    Stage 5 — RUNS COUNT+SCAN   the run scan's first half: leaf triangles per node, scanned within the block.
//    Stage 6 — RUNS BLOCK SCAN   the second half: block prefixes, and the arena's total leaf triangles.
//    Stage 7 — RUNS EMIT         every node's triangleBase in node order (leaf runs are concatenated in BFS node order,
//                                and inside a node in ascending stored slot, which is what makes `firstTri` a bit position
//                                in the node's own run) and the leaf records themselves.
//
// ⚠️ The two scans are what the file used to admit were serial: stage 2 was one workgroup looping over every node of the
//    level, and the run assignment was one workgroup looping over the whole arena. Both are now block-local scans plus a
//    block prefix sum, and the guard lines §⑩ pins (the single-workgroup ones) are gone with them: the plan's group
//    counts and every pin that named those lines moved with the change.
//
// ⚠️ NOT RUN ON A GPU. No Vulkan device in the development sandbox: this file is compile-gated by
//    Tools/Build/CheckShaders.sh and text-pinned by §⑩ of Exhibits/Workbench/Traversal/TwoLevelBvhProof.cpp against the
//    CPU mirror (`BlasBuildMirror::BuildHPloc`), which §⑨a/⑨b/⑨c/⑨g then walk, measure and clock.
//    ⚠️ TO RUN IT ON A MACHINE WITH A GPU: `bash Exhibits/Workbench/Traversal/RunBlasDevice.sh --refit`. That script
//       lowers this file, compiles the runner with the Vulkan-Headers only (the loader is opened at runtime), dispatches
//       the plan in BlasDevicePayload.cpp, and compares the device's node/leaf/level bytes with the mirror's.
//
// NOTE: GLSL source lowered by glslc -fshader-stage=compute.
#version 460
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;

#include "BlasLayout.slang"

layout(std430, binding = 0) buffer BlasNodeExtent   { vec4 BlasNodes[]; };    // D9: the node arena being built
layout(std430, binding = 1) buffer BlasLeafExtent   { vec4 BlasLeaves[]; };   // D9: the leaf arena being built
layout(std430, binding = 2) readonly buffer BlasSoupExtent { vec4 BlasSoup[]; };// D9: object-space soup, 3 × vec4 per triangle
layout(std430, binding = 3) buffer BlasSortedExtent    { uvec2 BlasSortedA[]; };   // ping (each level writes the other)
layout(std430, binding = 4) buffer BlasSortedAltExtent { uvec2 BlasSortedB[]; };   // pong
layout(std430, binding = 5) buffer BlasScratchExtent { uint BlasScratch[]; }; // one block per node slot + a header
layout(std430, binding = 6) buffer BlasLevelExtent   { uint BlasLevels[]; };  // written here, read by the refit kernel
// D9b: the per-workgroup totals of a parallel scan (one entry per block of 128 node slots). Both scans use it, at
//    different times: the childBase scan during each level, the run scan once at the end. ⚠️ It is a SCAN SCRATCH, not a
//    result: after the block-scan stage it holds the exclusive prefix of the blocks, which is what the stages after it
//    read. ⚠️ Sized ceil(NodeCount / 128) by the host — `BlasBlockSumsWords()` in BlasDevicePayload.h is the one place
//    that expression lives.
layout(std430, binding = 7) buffer BlasBlockSumExtent { uint BlasBlockSums[]; };

// ─── the scratch: one block per node slot, holding that node's children ──────────────────────────────────────────────
// Header — ⚠️ the level's base/count is PING-PONGED BY LEVEL PARITY, because a level's block scan computes the NEXT
//    level's pair while the rest of the current level still reads its own: with one pair the emit would see the next
//    level's base and write the wrong childBase, and a barrier between them would be the only other way out.
//    [0], [1]   even levels: the level's base node slot · its node count
//    [2], [3]   odd levels:  the same pair (stage 4 writes the next level's pair into the other one)
//    [4]        total leaf triangles written (stage 6 — the host reads it back as a check against the mirror)
//    [5..7]     reserved
// Per node, `kBlasScratchStride` uints:
//    +0..15   eight child entries: the range of each ([lo, hi)). An entry with lo == hi is EMPTY.
//    +16      the node's total leaf triangles   (the format's cap is 8 slots × 3 = 24, which rule 1 relies on)
//    +17, +18 the node's own range
//    +19      this node's slice of the childBase scan: its EXCLUSIVE prefix WITHIN its 128-slot block (stage 2). The
//             absolute base is that plus the block's own prefix (stage 4) plus the level's tile start — added in the
//             emit, because a block-local scan cannot know where its block sits in the level.
//    +20      how many entries stage 1 filled (informational; emptiness is lo == hi)
//    +21      each entry's PREFERRED slot, three bits per entry (its first triangle's octant at this depth)
//    +22      each entry's STORED slot, three bits per entry (stage 3, after the pool resolves the preferences)
//    +23      this node's slice of the RUN scan: its exclusive prefix of leaf triangles within its block (stage 5)
//
// ⚠️ The entries are what makes both halves of the rule expressible in one kernel: the octant case fills all eight (one
//    per occupied octant), the packed case fills ceil(n/3) runs, and the depth-exhausted case fills eight pieces — and
//    stages 3 and 4 never need to know which of them produced the entries.
const uint kBlasScratchHeader = 8u;
const uint kBlasScratchStride = 24u;

// 48 B — four uints, then two vec4 (16-byte aligned): 16 + 16 + 16. The object AABB comes from the host: it has one
//    already (InstanceAcceleration::QueryBlasObjectAabb), and a device-side reduction over the soup would be a second
//    implementation of the same normalisation.
layout(push_constant) uniform BlasBuildConstants
{
    uint Stage;         // [-]   0 prepass · 1 partition · 2 count+scan · 3 emit · 4 block scan · 5 runs count+scan ·
                        //       6 block scan · 7 runs emit
    uint Level;         // [lvl] the level being partitioned / emitted
    uint TriangleCount; // [cnt] triangles in the soup and in the leaf arena
    uint NodeCount;     // [cnt] node slots the arena may use (a cap, not a reservation)
    vec4 ObjectMin;     // [m]   object AABB minimum
    vec4 ObjectExtent;  // [m]   object AABB extent (a zero axis pins every key's bits on that axis to 0)
};

uint BlasNodeSlotAt(uint Slot) { return kBlasScratchHeader + kBlasScratchStride * Slot; }
uint BlasEntrySlotAt(uint NodeSlot, uint Entry) { return BlasNodeSlotAt(NodeSlot) + 2u * Entry; }
uint BlasPreferredSlots(uint NodeSlot) { return BlasScratch[BlasNodeSlotAt(NodeSlot) + 21u]; }
uint BlasStoredSlots(uint NodeSlot)     { return BlasScratch[BlasNodeSlotAt(NodeSlot) + 22u]; }

// Stage 1's workgroup state. GLSL only allows `shared` at file scope, and one workgroup is one node, so a single set of
//    counters serves every node the level dispatches.
shared uint Cursor[8];     // where the next element of each octant goes (the scatter offsets)
shared uint Start[8];      // where that octant's run began — the slot ranges stage 3/4 read back
shared uint Octants[128];  // this tile's octant per lane (a stable rank needs the tile's own order, not atomics)
shared uint TileCount[8];

// A 10-bit fixed-point axis value, clamped: the code's precision is the tree's precision, and a centroid exactly on the
//    far face must not overflow into the next octant.
uint BlasAxisBits(float Value, float Min, float Extent)
{
    if (!(Extent > 0.0f)) return 0u;
    float F = clamp((Value - Min) / Extent, 0.0f, 1.0f);
    return uint(F * 1023.0f + 0.5f);
}

// Spread 10 bits of one axis into the even 3-bit groups of a 30-bit code (x in bit 0, y in bit 1, z in bit 2).
uint BlasSpread(uint Value)
{
    uint Result = 0u;
    for (uint Bit = 0u; Bit < 10u; ++Bit) Result |= ((Value >> Bit) & 1u) << (Bit * 3u);
    return Result;
}

// The workgroup's own inclusive scan (Hillis–Steele over the 128 lanes) — the first half of both prefix sums this kernel
//    needs. ⚠️ Every lane of the workgroup MUST reach it: the barriers below are workgroup-wide, so a lane that returned
//    early because its node slot is past the level would hang the group. Out-of-range lanes therefore enter with zero.
//    Returns this lane's inclusive prefix; the block's total is the last lane's value.
shared uint BlasScan[128];
uint BlasBlockScan(uint Value)
{
    const uint Lane = gl_LocalInvocationID.x;
    const uint Lanes = gl_WorkGroupSize.x;
    BlasScan[Lane] = Value;
    memoryBarrierShared();
    barrier();
    for (uint Step = 1u; Step < Lanes; Step <<= 1u)
    {
        uint Read = 0u;
        if (Lane >= Step) Read = BlasScan[Lane - Step];
        memoryBarrierShared();
        barrier();
        if (Lane >= Step) BlasScan[Lane] += Read;
        memoryBarrierShared();
        barrier();
    }
    return BlasScan[Lane];
}

// The level's base/count pair for a given level — the parity ping-pong is the header's, not the caller's.
uint BlasLevelBase(uint ForLevel) { return BlasScratch[2u * (ForLevel & 1u) + 0u]; }
uint BlasLevelNodes(uint ForLevel) { return BlasScratch[2u * (ForLevel & 1u) + 1u]; }

void main()
{
    const uint Thread = gl_GlobalInvocationID.x;

    // ─── stage 0: keys, and the root ─────────────────────────────────────────────────────────────────────────────────
    if (Stage == 0u)
    {
        if (Thread < TriangleCount)
        {
            const vec4 V0 = BlasSoup[3u * Thread + 0u];
            const vec4 V1 = BlasSoup[3u * Thread + 1u];
            const vec4 V2 = BlasSoup[3u * Thread + 2u];
            const vec3 Centre = (V0.xyz + V1.xyz + V2.xyz) / 3.0f;
            const uint Qx = BlasAxisBits(Centre.x, ObjectMin.x, ObjectExtent.x);
            const uint Qy = BlasAxisBits(Centre.y, ObjectMin.y, ObjectExtent.y);
            const uint Qz = BlasAxisBits(Centre.z, ObjectMin.z, ObjectExtent.z);
            const uint Key = (BlasSpread(Qx) << 2u) | (BlasSpread(Qy) << 1u) | BlasSpread(Qz);
            BlasSortedA[Thread] = uvec2(Key, Thread);
        }
        if (Thread == 0u)
        {
            BlasScratch[0] = 0u;                   // the root is node slot 0 — level 0's pair (even, so slots 0 and 1)
            BlasScratch[1] = TriangleCount > 0u ? 1u : 0u;
            BlasScratch[2] = 0u;                   // level 1's pair starts empty: stage 4 fills it
            BlasScratch[3] = 0u;
            const uint Root = BlasNodeSlotAt(0u);
            BlasScratch[Root + 17u] = 0u;
            BlasScratch[Root + 18u] = TriangleCount;
            BlasLevels[0] = 0u;
        }
        return;
    }

    // ─── stage 1: the level's partition — this is the sort, one level at a time ──────────────────────────────────────
    if (Stage == 1u)
    {
        const uint NodeBase = BlasLevelBase(Level);
        const uint Nodes = BlasLevelNodes(Level);
        const uint Slot = NodeBase + gl_WorkGroupID.x;
        if (gl_WorkGroupID.x >= Nodes) return;
        const uint At = BlasNodeSlotAt(Slot);
        const uint Lo = BlasScratch[At + 17u];
        const uint Hi = BlasScratch[At + 18u];
        const uint Lane = gl_LocalInvocationID.x;
        const uint Lanes = gl_WorkGroupSize.x;
        const bool SortedIsA = (Level & 1u) == 0u;   // the array the previous level left the range in

        // ── rule 1: a range that fits this node's own eight slots is leaf runs of three, and nothing below it needs
        //    ordering — every child is a leaf, so the range is never partitioned again. (The mirror emits the same runs
        //    from the same arithmetic; §⑨g's D9-v1 column is what happens without this.)
        if (Hi - Lo <= 8u * kBlasTriPerLeaf)
        {
            if (Lane == 0u)
            {
                uint Preferred = 0u, LeafTriangles = 0u, Entry = 0u;
                for (uint Run = Lo; Run < Hi; Run += kBlasTriPerLeaf)
                {
                    const uint RunHi = min(Hi, Run + kBlasTriPerLeaf);
                    const uint Key = SortedIsA ? BlasSortedA[Run].x : BlasSortedB[Run].x;
                    BlasScratch[At + 2u * Entry + 0u] = Run;
                    BlasScratch[At + 2u * Entry + 1u] = RunHi;
                    Preferred = BlasSetByte(Preferred, Entry, BlasOctantAt(Key, Level));
                    LeafTriangles += RunHi - Run;
                    ++Entry;
                }
                for (uint Fill = Entry; Fill < 8u; ++Fill)
                {
                    BlasScratch[At + 2u * Fill + 0u] = 0u;
                    BlasScratch[At + 2u * Fill + 1u] = 0u;
                }
                BlasScratch[At + 16u] = LeafTriangles;
                BlasScratch[At + 20u] = Entry;
                BlasScratch[At + 21u] = Preferred;
            }
            return;
        }

        // ── rule 3: the Morton bits are exhausted. Eight count-balanced pieces — never more than eight children, and
        //    each strictly smaller than the range, so the recursion terminates. (Per-octant splitting here could produce
        //    up to sixty-four children from a node with eight slots.)
        if (Level >= kBlasMortonDepth)
        {
            if (Lane == 0u)
            {
                const uint Count = Hi - Lo;
                uint LeafTriangles = 0u;
                for (uint Entry = 0u; Entry < 8u; ++Entry)
                {
                    // 32-bit is enough: Count is a triangle count (well under 2^28) and the multiplier is at most 8.
                    const uint PLo = Lo + (Count * Entry) / 8u;
                    const uint PHi = Lo + (Count * (Entry + 1u)) / 8u;
                    BlasScratch[At + 2u * Entry + 0u] = PLo;
                    BlasScratch[At + 2u * Entry + 1u] = PHi;
                    if (PHi - PLo <= kBlasTriPerLeaf) LeafTriangles += PHi - PLo;
                }
                BlasScratch[At + 16u] = LeafTriangles;
                BlasScratch[At + 20u] = 8u;
                BlasScratch[At + 21u] = 0u;   // no octant left to prefer
            }
            return;
        }

        // ── rule 2: the octant partition, which is also the sort for this level ─────────────────────────────────────
        if (Lane == 0u)
        {
            for (uint S = 0u; S < 8u; ++S) { Cursor[S] = Lo; Start[S] = Lo; TileCount[S] = 0u; }
        }
        memoryBarrierShared();
        barrier();

        for (uint Tile = Lo; Tile < Hi; Tile += Lanes)
        {
            const uint Index = Tile + Lane;
            uint Key = 0u;
            uint Octant = 0u;
            if (Index < Hi)
            {
                Key = SortedIsA ? BlasSortedA[Index].x : BlasSortedB[Index].x;
                Octant = BlasOctantAt(Key, Level);
            }
            Octants[Lane] = Octant;
            memoryBarrierShared();
            barrier();
            if (Index < Hi) atomicAdd(TileCount[Octant], 1u);
            memoryBarrierShared();
            barrier();
            if (Index < Hi)
            {
                // Stable rank inside the tile: count the same-octant lanes before this one. O(Lanes) per element, and
                //    the reason this loop does not use a global atomic ticket: the order must not depend on scheduling.
                uint Rank = 0u;
                for (uint Other = 0u; Other < Lane; ++Other) if (Octants[Other] == Octant) ++Rank;
                const uint Destination = Cursor[Octant] + Rank;
                if (SortedIsA) BlasSortedB[Destination] = uvec2(Key, BlasSortedA[Index].y);
                else           BlasSortedA[Destination] = uvec2(Key, BlasSortedB[Index].y);
            }
            memoryBarrierShared();
            barrier();
            if (Lane == 0u) for (uint S = 0u; S < 8u; ++S) { Cursor[S] += TileCount[S]; TileCount[S] = 0u; }
            memoryBarrierShared();
            barrier();
        }

        if (Lane == 0u)
        {
            uint LeafTriangles = 0u;
            for (uint S = 0u; S < 8u; ++S)
            {
                const uint RunLo = Start[S];
                const uint RunHi = Cursor[S];
                BlasScratch[At + 2u * S + 0u] = RunLo;      // lo == hi is an empty entry: the octant was unoccupied
                BlasScratch[At + 2u * S + 1u] = RunHi;
                if (RunHi - RunLo <= kBlasTriPerLeaf) LeafTriangles += RunHi - RunLo;
            }
            BlasScratch[At + 16u] = LeafTriangles;
            BlasScratch[At + 20u] = 8u;
            // The entry's own index is the octant, so the preference is the identity here; the pool in stage 3 still
            //    resolves it, which is what keeps the two rules' slot assignment identical in shape.
            BlasScratch[At + 21u] = (0u << 0u) | (1u << 3u) | (2u << 6u) | (3u << 9u) | (4u << 12u) | (5u << 15u) |
                                    (6u << 18u) | (7u << 21u);
        }
        return;
    }

    // ─── stage 2: count each node's interior children, and scan them WITHIN the block ───────────────────────────────
    // One workgroup per 128 node slots of the level. The level's slots are dense ([NodeBase, NodeBase + Nodes)), so the
    //    children of the whole level tile the arena from NodeBase + Nodes on — and each parent's childBase is its slice
    //    of that tile, in node order, which is what puts every interior child at a HIGHER slot than its parent (§⑨c
    //    measures that invariant).
    // ⚠️ This stage used to be one workgroup with a serial O(level nodes) loop. It is the same sum, computed the way a
    //    device computes it: each lane counts its own node, the workgroup scans its 128 counts, and the per-block totals
    //    go to `BlasBlockSums` for the block scan (stage 4) that turns them into absolute bases. Nothing about the
    //    RESULT changes — the emit adds the two halves back together — which is why the mirror's bytes still stand.
    if (Stage == 2u)
    {
        const uint NodeBase = BlasLevelBase(Level);
        const uint Nodes = BlasLevelNodes(Level);
        const uint Index = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        uint Interior = 0u;
        if (Index < Nodes)
        {
            const uint At = BlasNodeSlotAt(NodeBase + Index);
            for (uint Entry = 0u; Entry < 8u; ++Entry)
            {
                const uint Count = BlasScratch[At + 2u * Entry + 1u] - BlasScratch[At + 2u * Entry + 0u];
                if (Count > kBlasTriPerLeaf) ++Interior;
            }
        }
        const uint Inclusive = BlasBlockScan(Interior);            // every lane enters, in range or not
        if (Index < Nodes) BlasScratch[BlasNodeSlotAt(NodeBase + Index) + 19u] = Inclusive - Interior;
        if (gl_LocalInvocationID.x == gl_WorkGroupSize.x - 1u) BlasBlockSums[gl_WorkGroupID.x] = Inclusive;
        return;
    }

    // ─── stage 4: the block scan — one workgroup over the per-block totals of stage 2 ────────────────────────────────
    // ⚠️ The second workgroup-wide step, and the only one that is still a serial loop: it is O(nodes / 128) — one
    //    iteration per BLOCK, not per node, which is the 128× reduction that made the parallel scan worth doing. It
    //    turns the block totals into exclusive prefixes IN PLACE and publishes the next level's pair: the level's
    //    children tile the arena from NodeBase + Nodes on (the mirror's §⑨c invariant), so the next level's base is that
    //    tile's start and its count is the level's total interior children.
    //    A level with no nodes writes a zero count, and every later stage of that level — and the levels the host still
    //    dispatches after it — no-op, which is what lets the host loop to a fixed cap with no readback.
    if (Stage == 4u)
    {
        if (gl_LocalInvocationID.x != 0u || gl_WorkGroupID.x != 0u) return;
        const uint NodeBase = BlasLevelBase(Level);
        const uint Nodes = BlasLevelNodes(Level);
        const uint Blocks = (Nodes + gl_WorkGroupSize.x - 1u) / gl_WorkGroupSize.x;
        uint Running = 0u;
        for (uint Block = 0u; Block < Blocks; ++Block)
        {
            const uint Total = BlasBlockSums[Block];
            BlasBlockSums[Block] = Running;
            Running += Total;
        }
        BlasScratch[2u * ((Level + 1u) & 1u) + 0u] = NodeBase + Nodes;
        BlasScratch[2u * ((Level + 1u) & 1u) + 1u] = Running;
        return;
    }

    // ─── stage 3: emit the level's nodes, one lane per node ─────────────────────────────────────────────────────────
    // Every node of a level is emitted by its own lane, so the O(child range) bounds gathering below is spread across the
    //    level instead of serialised behind one thread. What each node writes is untouched by that: the childBase is the
    //    level's tile start (NodeBase + Nodes) plus the node's block prefix (stage 4) plus its within-block offset
    //    (stage 2) — the same number stage 2 used to compute in one serial pass, assembled from its two halves.
    if (Stage == 3u)
    {
        const uint NodeBase = BlasLevelBase(Level);
        const uint Nodes = BlasLevelNodes(Level);
        const uint Index = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (Index >= Nodes) return;
        const uint Slot = NodeBase + Index;
        const uint At = BlasNodeSlotAt(Slot);
        const bool ScatteredIsA = ((Level + 1u) & 1u) == 0u;   // the array this level's partition scattered into
        const uint Preferred = BlasScratch[At + 21u];

        // Each child's exact bounds, from the geometry its range names.
        vec3 SlotMin[8];
        vec3 SlotMax[8];
        bool Present[8];
        for (uint Entry = 0u; Entry < 8u; ++Entry)
        {
            const uint RunLo = BlasScratch[At + 2u * Entry + 0u];
            const uint RunHi = BlasScratch[At + 2u * Entry + 1u];
            Present[Entry] = RunHi > RunLo;
            SlotMin[Entry] = vec3( 1.0e30f);
            SlotMax[Entry] = vec3(-1.0e30f);
            if (!Present[Entry]) continue;
            for (uint I = RunLo; I < RunHi; ++I)
            {
                const uint Primitive = ScatteredIsA ? BlasSortedA[I].y : BlasSortedB[I].y;
                const vec4 V0 = BlasSoup[3u * Primitive + 0u];
                const vec4 V1 = BlasSoup[3u * Primitive + 1u];
                const vec4 V2 = BlasSoup[3u * Primitive + 2u];
                SlotMin[Entry] = min(SlotMin[Entry], min(V0.xyz, min(V1.xyz, V2.xyz)));
                SlotMax[Entry] = max(SlotMax[Entry], max(V0.xyz, max(V1.xyz, V2.xyz)));
            }
        }

        // Slot assignment: the octant is a PREFERENCE, the free-slot pool is what keeps the children consecutive and
        //    collision-free (BlasBuildMirror::BuildHploc — a slot decides a box lane and a meta byte, while the
        //    traversal's child index comes from the imask and the stored slot).
        uint EntryOfSlot[8];
        for (uint S = 0u; S < 8u; ++S) EntryOfSlot[S] = 8u;
        uint Free = 0xFFu;
        for (uint Entry = 0u; Entry < 8u; ++Entry)
        {
            if (!Present[Entry]) continue;
            const uint Want = BlasByteOf(Preferred, Entry);
            uint Take = 8u;
            if ((Free & (1u << Want)) != 0u) Take = Want;
            else for (uint F = 0u; F < 8u; ++F) if ((Free & (1u << F)) != 0u) { Take = F; break; }
            if (Take > 7u) return;                       // more than eight children: the format cannot hold this node
            Free &= ~(1u << Take);
            EntryOfSlot[Take] = Entry;
        }

        vec4 Node[5];
        for (uint W = 0u; W < kBlasNodeBlocks; ++W) Node[W] = vec4(0.0f);
        uint Meta = 0u, Imask = 0u, RunPosition = 0u, NextChild = 0u;
        const uint ChildBase = NodeBase + Nodes + BlasBlockSums[gl_WorkGroupID.x] + BlasScratch[At + 19u];
        NextChild = ChildBase;
        uint Stored = 0u;
        for (uint S = 0u; S < 8u; ++S)   // ascending STORED slot: the order the children are numbered in, the order the
        {                                 // traversal ranks them by, and the order a node's leaf runs are laid out in
            const uint Entry = EntryOfSlot[S];
            if (Entry >= 8u) continue;
            Stored = BlasSetByte(Stored, Entry, S);
            const uint RunLo = BlasScratch[At + 2u * Entry + 0u];
            const uint RunHi = BlasScratch[At + 2u * Entry + 1u];
            const uint Count = RunHi - RunLo;
            if (Count > kBlasTriPerLeaf)
            {
                // Interior child: a new node slot, its range handed over, its level recorded for the refit kernel.
                const uint Child = NextChild++;
                const uint ChildAt = BlasNodeSlotAt(Child);
                BlasScratch[ChildAt + 17u] = RunLo;
                BlasScratch[ChildAt + 18u] = RunHi;
                BlasLevels[Child] = Level + 1u;
                Meta = BlasSetByte(Meta, S, BlasInteriorMeta(S));
                Imask |= 1u << S;
            }
            else
            {
                // Leaf child: its share of the node's run, in the slot's own order (the mirror's ④).
                Meta = BlasSetByte(Meta, S, (BlasCountToUnary(Count) << 5u) | RunPosition);
                RunPosition += Count;
            }
        }
        BlasScratch[At + 22u] = Stored;

        // ⚠️ The quantiser reads its slots by STORED slot, so the bounds have to be gathered the same way: the child
        //    in stored slot S is the entry EntryOfSlot[S], and its box is that entry's exact box.
        vec3 StoredMin[8];
        vec3 StoredMax[8];
        uint Presence = 0u;
        for (uint S = 0u; S < 8u; ++S)
        {
            const uint Entry = EntryOfSlot[S];
            StoredMin[S] = vec3( 1.0e30f);
            StoredMax[S] = vec3(-1.0e30f);
            if (Entry >= 8u) continue;
            Presence |= 1u << S;
            StoredMin[S] = SlotMin[Entry];
            StoredMax[S] = SlotMax[Entry];
        }
        BlasQuantiseNode(Node, StoredMin, StoredMax, Presence);
        BlasSetChildBase(Node, ChildBase);
        BlasSetImask(Node, Imask);
        for (uint S = 0u; S < 8u; ++S) BlasSetMeta(Node, S, BlasByteOf(Meta, S));
        for (uint W = 0u; W < kBlasNodeBlocks; ++W) BlasNodes[kBlasNodeBlocks * Slot + W] = Node[W];
        return;
    }

    // ─── stage 5: each node's leaf triangles, scanned WITHIN its block ──────────────────────────────────────────────
    // The runs are concatenated in node order and, inside a node, in ascending STORED SLOT order — so a node's meta
    //    offsets are positions inside its own run, and the primitive order inside a run is the geometry's, not the soup's.
    //    Same two-step scan as stage 2, over the WHOLE arena this time (node order, not one level's), because the runs
    //    are laid out in node order. The count comes out of the entries stage 1/3 left behind, so a slot whose run is
    //    empty or interior contributes nothing.
    if (Stage == 5u)
    {
        const uint Index = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        uint LeafTriangles = 0u;
        if (Index < NodeCount)
        {
            const uint At = BlasNodeSlotAt(Index);
            for (uint Entry = 0u; Entry < 8u; ++Entry)
            {
                const uint Count = BlasScratch[At + 2u * Entry + 1u] - BlasScratch[At + 2u * Entry + 0u];
                if (Count >= 1u && Count <= kBlasTriPerLeaf) LeafTriangles += Count;
            }
        }
        const uint Inclusive = BlasBlockScan(LeafTriangles);       // every lane enters, in range or not
        if (Index < NodeCount) BlasScratch[BlasNodeSlotAt(Index) + 23u] = Inclusive - LeafTriangles;
        if (gl_LocalInvocationID.x == gl_WorkGroupSize.x - 1u) BlasBlockSums[gl_WorkGroupID.x] = Inclusive;
        return;
    }

    // ─── stage 6: the block scan over stages 5's totals — the same shape as stage 4 ─────────────────────────────────
    if (Stage == 6u)
    {
        if (gl_LocalInvocationID.x != 0u || gl_WorkGroupID.x != 0u) return;
        const uint Blocks = (NodeCount + gl_WorkGroupSize.x - 1u) / gl_WorkGroupSize.x;
        uint Running = 0u;
        for (uint Block = 0u; Block < Blocks; ++Block)
        {
            const uint Total = BlasBlockSums[Block];
            BlasBlockSums[Block] = Running;
            Running += Total;
        }
        BlasScratch[4u] = Running;   // the leaf arena's triangle count, for the host's post-build check
        return;
    }

    // ─── stage 7: triangleBase, and the leaf records — one lane per node ────────────────────────────────────────────
    if (Stage == 7u)
    {
        const uint Slot = gl_WorkGroupID.x * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (Slot >= NodeCount) return;
        const uint At = BlasNodeSlotAt(Slot);
        const uint Triangles = BlasBlockSums[gl_WorkGroupID.x] + BlasScratch[At + 23u];
        const uint Stored = BlasScratch[At + 22u];
        const bool SortedIsA = (BlasLevels[Slot] & 1u) == 0u;

        vec4 Node[5];
        for (uint W = 0u; W < kBlasNodeBlocks; ++W) Node[W] = BlasNodes[kBlasNodeBlocks * Slot + W];
        BlasSetTriBase(Node, Triangles * kBlasTriBlocks);

        // Entries in ascending stored slot: find each leaf child's position inside the run first, so a slot's `firstTri`
        //    is the offset a ray's hit bit will carry.
        uint FirstOfEntry[8];
        uint RunPosition = 0u;
        for (uint S = 0u; S < 8u; ++S)
        {
            for (uint Entry = 0u; Entry < 8u; ++Entry)
            {
                if (BlasByteOf(Stored, Entry) != S) continue;
                const uint RunLo = BlasScratch[At + 2u * Entry + 0u];
                const uint RunHi = BlasScratch[At + 2u * Entry + 1u];
                if (RunHi <= RunLo || (RunHi - RunLo) > kBlasTriPerLeaf) continue;   // empty, or an interior child
                FirstOfEntry[Entry] = RunPosition;
                RunPosition += RunHi - RunLo;
            }
        }
        // ...then the records themselves, straight out of the soup.
        for (uint Entry = 0u; Entry < 8u; ++Entry)
        {
            const uint RunLo = BlasScratch[At + 2u * Entry + 0u];
            const uint RunHi = BlasScratch[At + 2u * Entry + 1u];
            if (RunHi <= RunLo || (RunHi - RunLo) > kBlasTriPerLeaf) continue;
            for (uint I = RunLo; I < RunHi; ++I)
            {
                const uint Primitive = SortedIsA ? BlasSortedA[I].y : BlasSortedB[I].y;
                if (Primitive >= TriangleCount) continue;
                const vec4 V0 = BlasSoup[3u * Primitive + 0u];
                const vec4 V1 = BlasSoup[3u * Primitive + 1u];
                const vec4 V2 = BlasSoup[3u * Primitive + 2u];
                const uint Block = kBlasTriBlocks * (Triangles + FirstOfEntry[Entry] + (I - RunLo));
                BlasLeaves[Block + 0u] = vec4(V2.xyz - V0.xyz, 0.0f);
                BlasLeaves[Block + 1u] = vec4(V1.xyz - V0.xyz, 0.0f);
                BlasLeaves[Block + 2u] = vec4(V0.xyz, uintBitsToFloat(Primitive));
            }
        }
        for (uint W = 0u; W < kBlasNodeBlocks; ++W) BlasNodes[kBlasNodeBlocks * Slot + W] = Node[W];
        return;
    }
}

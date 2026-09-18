//============================================================================================================================================
// 📦 Engine/Shaders/BlasLayout.slang — D9: the packed CWBVH layout and its quantiser, on the device side
//============================================================================================================================================
// 🧩 The device twin of Engine/GeometricRaster/BlasBuildMirror.{h,cpp}. BlasRefit.slang and BlasBuild.slang both include
//    this file so the two kernels cannot disagree about a byte, and §⑩ of Exhibits/Workbench/Traversal/TwoLevelBvhProof.cpp
//    pins every rule below against the CPU mirror that the §⑨ gates measure. Nothing here is invented: each rule was read
//    out of tinybvh's converter (tiny_bvh.h:6087-6223) and then re-measured on a real blob.
//
// NODE — 5 vec4 = 80 B, one node:
//    word 0..2   p.xyz          the node's own box minimum                            [m]
//    word 3  (p.w)              bytes 12,13,14,15 = ex, ey, ez, imask                 ⚠️ NOT block 1
//    word 4                     childBase   — the node index of the FIRST interior child
//    word 5                     triangleBase — the node's leaf-run start, in BLOCK units
//    bytes 24..31               one meta byte per slot
//    bytes 32..79               qlo.x at 32+slot, qlo.y at 40+slot, qlo.z at 48+slot, qhi.x at 56+slot,
//                               qhi.y at 64+slot, qhi.z at 72+slot   (i.e. block 2's byte `slot` and so on)
//
// SLOT META — which of the eight slots hold what:
//    interior   (meta & 0x18) == 0x18      the collapse writes (1 << 5) | (24 + slot); bits 4 and 3 are the test.
//                                          ⚠️ Two near-misses: bit 7 is set for every 3-triangle leaf (0b111 << 5 =
//                                          0xE0), and bits 5+4 also match a leaf whose run starts at triangle 16..23.
//    leaf       unary << 5 | firstTri      unary is a MASK (001/011/111 ⇒ 1/2/3 triangles), low five bits are the
//                                          slot's first triangle inside the node's run — and, in the traversal's hit
//                                          mask, the bit position where that run is reported.
//    empty      meta == 0                  a node with fewer than eight children leaves the rest at zero, which reads
//                                          as a leaf with a zero-triangle run.
//
// CHILD INDEX — the traversal's own rule, from the node's imask byte and the STORED slot:
//    childNodeIndex = (childBase + popcount(imask & ((1 << slot) - 1))) * 5 + NodeBase
//    The octant permutation in TraverseChildren reorders only the hit BITS; the index is rank over the stored slots.
//
// LEAF RECORD — 3 vec4 per triangle: e1 = v2 − v0, e2 = v1 − v0, v0 with the object-space primitive index bit-cast
//    into v0.w. tinybvh's convention, and what the kernel's Möller–Trumbore reads.
//
// QUANTISATION — per axis, one exponent for the whole node:
//    exponent = ceil(log2(extent / 255))        (0 when the axis has no extent: log2(0) is −inf)
//    qlo      = floor((childLo − nodeLo) / 2^exponent), qhi = ceil((childHi − nodeLo) / 2^exponent), both as u8
//    so a parent covers its child's DECODED box exactly, which is what makes a one-pass refit conservative.
//
// NOTE: GLSL source lowered by glslc -fshader-stage=compute; included by BlasRefit.slang and BlasBuild.slang.
#ifndef FRONTIER_BLAS_LAYOUT
#define FRONTIER_BLAS_LAYOUT

const uint kBlasNodeBlocks  = 5u;   // [blk] vec4 per node
const uint kBlasTriBlocks   = 3u;   // [blk] vec4 per leaf triangle
const uint kBlasTriPerLeaf  = 3u;   // [cnt] the format's per-slot maximum (unary 0b111)
const uint kBlasMortonDepth = 10u;  // [lvl] 30-bit Morton code, 3 bits per level

// ─── byte access into a float-owned word (the exponents and imask live INSIDE p.w; this is how they are read) ─────────
uint BlasByteOf(uint Word, uint Byte) { return (Word >> (Byte * 8u)) & 0xFFu; }
uint BlasSetByte(uint Word, uint Byte, uint Value) { return (Word & ~(0xFFu << (Byte * 8u))) | ((Value & 0xFFu) << (Byte * 8u)); }
uint BlasByteOf(float Word, uint Byte) { return BlasByteOf(floatBitsToUint(Word), Byte); }
float BlasSetByteF(float Word, uint Byte, uint Value) { return uintBitsToFloat(BlasSetByte(floatBitsToUint(Word), Byte, Value)); }

// An exponent travels as an int8: 0xFA is −6. Reading it needs a sign extension, writing it needs the wrap of a
//    negative value into a byte — GLSL leaves `uint(negative)` undefined, so both directions are explicit here.
int  BlasI8(uint Byte) { return (Byte & 0x80u) != 0u ? int(Byte) - 256 : int(Byte); }
uint BlasByteOfI8(int Value) { return Value < 0 ? uint(Value + 256) : uint(Value); }

// ─── node field accessors (the kernels read a node into vec4[5] and write it back) ───────────────────────────────────
uint BlasNodeByte(vec4 Node[5], uint Byte) { return BlasByteOf(Node[Byte >> 2u][Byte & 3u], 0u); }
void BlasSetNodeByte(inout vec4 Node[5], uint Byte, uint Value)
{
    Node[Byte >> 2u][Byte & 3u] = BlasSetByteF(Node[Byte >> 2u][Byte & 3u], 0u, Value);
}
uint BlasChildBase(vec4 Node[5]) { return floatBitsToUint(Node[1].x); }
void BlasSetChildBase(inout vec4 Node[5], uint Value) { Node[1].x = uintBitsToFloat(Value); }
uint BlasTriBase(vec4 Node[5]) { return floatBitsToUint(Node[1].y); }
void BlasSetTriBase(inout vec4 Node[5], uint Value) { Node[1].y = uintBitsToFloat(Value); }
uint BlasImask(vec4 Node[5]) { return BlasNodeByte(Node, 15u); }
void BlasSetImask(inout vec4 Node[5], uint Value) { BlasSetNodeByte(Node, 15u, Value); }
uint BlasMeta(vec4 Node[5], uint Slot) { return BlasNodeByte(Node, 24u + Slot); }
void BlasSetMeta(inout vec4 Node[5], uint Slot, uint Value) { BlasSetNodeByte(Node, 24u + Slot, Value); }

// ─── meta rules (BlasBuildMirror.cpp: IsInteriorMeta / LeafCount / LeafRunOffset / SlotPresent / CountToUnary) ───────
bool BlasIsInteriorMeta(uint Meta) { return (Meta & 0x18u) == 0x18u; }
uint BlasLeafRunOffset(uint Meta) { return Meta & 0x1Fu; }
uint BlasLeafCount(uint Meta)
{
    // Only 001/011/111 are counts. A zero byte is an EMPTY slot; anything else is not a leaf this format can produce —
    //    treating those as "three" would invent geometry, which is why this test is exhaustive rather than a clamp.
    uint Unary = (Meta >> 5u) & 0x7u;
    return Unary == 1u ? 1u : (Unary == 3u ? 2u : (Unary == 7u ? 3u : 0u));
}
bool BlasSlotPresent(uint Meta) { return BlasIsInteriorMeta(Meta) || BlasLeafCount(Meta) > 0u; }
uint BlasCountToUnary(uint Count) { return Count <= 1u ? 1u : (Count == 2u ? 3u : 7u); }
uint BlasInteriorMeta(uint Slot) { return (1u << 5u) | (24u + Slot); }

// The rank the traversal derives from the imask: interior children at a lower STORED slot.
uint BlasInteriorRank(vec4 Node[5], uint Slot)
{
    uint Rank = 0u;
    for (uint S = 0u; S < Slot; ++S) if (BlasIsInteriorMeta(BlasMeta(Node, S))) ++Rank;
    return Rank;
}

// ─── a slot's quantised box, exactly as TraverseChildren decodes it ──────────────────────────────────────────────────
void BlasSlotBox(vec4 Node[5], uint Slot, out vec3 Lo, out vec3 Hi)
{
    float Qx = exp2(float(BlasI8(BlasNodeByte(Node, 12u))));
    float Qy = exp2(float(BlasI8(BlasNodeByte(Node, 13u))));
    float Qz = exp2(float(BlasI8(BlasNodeByte(Node, 14u))));
    vec3 P = Node[0].xyz;
    Lo = vec3(P.x + float(BlasNodeByte(Node, 32u + Slot +  0u)) * Qx,
              P.y + float(BlasNodeByte(Node, 32u + Slot +  8u)) * Qy,
              P.z + float(BlasNodeByte(Node, 32u + Slot + 16u)) * Qz);
    Hi = vec3(P.x + float(BlasNodeByte(Node, 32u + Slot + 24u)) * Qx,
              P.y + float(BlasNodeByte(Node, 32u + Slot + 32u)) * Qy,
              P.z + float(BlasNodeByte(Node, 32u + Slot + 40u)) * Qz);
}

// ─── the shared quantiser — BlasBuildMirror::QuantiseNode, line for line ─────────────────────────────────────────────
// PresenceMask bit `s` is the C++ version's SlotPresent[s]. The node's imask byte is PRESERVED: the caller owns it, and a
//    refit that recomputed it from the slots would be free to disagree with the topology it is not allowed to change.
void BlasQuantiseNode(inout vec4 Node[5], in vec3 SlotMin[8], in vec3 SlotMax[8], in uint PresenceMask)
{
    vec3 Lo = vec3( 1.0e30f);
    vec3 Hi = vec3(-1.0e30f);
    for (uint S = 0u; S < 8u; ++S)
    {
        if ((PresenceMask & (1u << S)) == 0u) continue;
        Lo = min(Lo, SlotMin[S]);
        Hi = max(Hi, SlotMax[S]);
    }
    if (!(Lo.x <= Hi.x)) return;   // no live child: leave the node exactly as it was

    Node[0].xyz = Lo;
    vec3 Extent = Hi - Lo;
    ivec3 Ex = ivec3(0);
    for (int C = 0; C < 3; ++C)
        if (Extent[C] > 0.0f) Ex[C] = int(ceil(log2(Extent[C] / 255.0f)));   // a flat axis pins to 2^0 = 1.0, as on the CPU
    BlasSetNodeByte(Node, 12u, BlasByteOfI8(Ex.x));
    BlasSetNodeByte(Node, 13u, BlasByteOfI8(Ex.y));
    BlasSetNodeByte(Node, 14u, BlasByteOfI8(Ex.z));
    // byte 15 (imask) is deliberately untouched

    vec3 Q = exp2(vec3(Ex));
    for (uint S = 0u; S < 8u; ++S)
    {
        if ((PresenceMask & (1u << S)) == 0u) continue;
        ivec3 QLo = ivec3(floor((SlotMin[S] - Lo) / Q));
        ivec3 QHi = ivec3(ceil ((SlotMax[S] - Lo) / Q));
        BlasSetNodeByte(Node, 32u + S +  0u, BlasByteOfI8(QLo.x));
        BlasSetNodeByte(Node, 32u + S +  8u, BlasByteOfI8(QLo.y));
        BlasSetNodeByte(Node, 32u + S + 16u, BlasByteOfI8(QLo.z));
        BlasSetNodeByte(Node, 32u + S + 24u, BlasByteOfI8(QHi.x));
        BlasSetNodeByte(Node, 32u + S + 32u, BlasByteOfI8(QHi.y));
        BlasSetNodeByte(Node, 32u + S + 40u, BlasByteOfI8(QHi.z));
    }
}

// The octant a 30-bit Morton key names at a given depth (root = 0), three bits at a time from the MSB. Past the last
//    level the bits are exhausted — every key's remaining octant is 0 — which is why the guard (and the CPU mirror's) is
//    depth < 10 rather than a clamp of a negative shift.
uint BlasOctantAt(uint Key, uint Depth) { return Depth < kBlasMortonDepth ? ((Key >> (27u - Depth * 3u)) & 0x7u) : 0u; }

// A triangle's exact bounds from a leaf record (the record stores e1, e2 and v0, not three vertices).
void BlasTriangleBounds(vec4 E1, vec4 E2, vec4 V0, out vec3 Lo, out vec3 Hi)
{
    vec3 A = V0.xyz;
    vec3 B = A + E2.xyz;
    vec3 C = A + E1.xyz;
    Lo = min(A, min(B, C));
    Hi = max(A, max(B, C));
}

#endif

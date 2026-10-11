//============================================================================================================================================
// 📦 Engine/Shaders/TraversalRecords.slang — D6/D7: the two-level traversal's device records, mirrored 1:1 in C++
//============================================================================================================================================
// 🧩 The shader-side declaration of what Engine/GeometricRaster/InstanceAcceleration.h builds. Every struct here has a
//    static_assert on the C++ side pinning the same byte count, and every field carries the C++ field it mirrors, so a
//    layout drift is a compile error on one side and a documented mismatch on the other rather than a wrong pixel.
//
//    ⚠️ std430 rules decide the declarations, not readability:
//      · an array of structures has a stride of the structure's size rounded up to its ALIGNMENT, and a vec4 member
//        forces 16-byte alignment. TlasInstance must therefore stay a multiple of 16 bytes (112 B = 7 × 16), and its
//        4 scalar fields must sit inside a 16-byte tail rather than being split by a vec3.
//      · BlasPlacement is four scalars (alignment 4, size 16) — 16 bytes with no padding, which is why its C++ twin
//        is four uint32s and not the 48 B BlasRecord (that one carries CPU-only fields).
//
//    The top level's nodes are tinybvh's 32 B binary nodes, uploaded as 8 floats: [min.xyz, leftFirst bits, max.xyz,
//    primitive-count bits]. The two integer fields travel bit-cast inside the float array — the same trick the CWBVH
//    triangle blob already uses for its primitive index in v0.w — so the node buffer is one flat float4 array and needs
//    no second descriptor.

#ifndef FRONTIER_TRAVERSAL_RECORDS
#define FRONTIER_TRAVERSAL_RECORDS

// 112 B — TlasInstanceRecord (InstanceAcceleration.h). Inverse[16] + AabbMin[4] + AabbMax[4] + 4 scalars.
struct TraversalTlasInstance
{
    vec4 Inv0;            // [-] world → object, column 0 (x = mirrored C++ Inverse[0..3])
    vec4 Inv1;            // [-] column 1
    vec4 Inv2;            // [-] column 2
    vec4 Inv3;            // [-] column 3 (translation; w = 1)
    vec4 AabbMin;         // [m] world-space AABB minimum, w unused
    vec4 AabbMax;         // [m] world-space AABB maximum, w unused
    uint BlasIndex;       // [idx] which BLAS placement to walk
    uint FirstTriangle;   // [idx] where this instance's triangles start in the flat soup Triangles[]
    uint Flags;           // [bit] instance flags (reserved)
    uint Pad;             // [-] 0
};

// 16 B — BlasPlacement (InstanceAcceleration.h): where one BLAS' blobs live inside the shared buffers.
struct TraversalBlasPlacement
{
    uint NodeOffset;      // [vec4 blocks] start of this BLAS' nodes in CwbvhNodes[]
    uint LeafOffset;      // [vec4 blocks] start of this BLAS' triangles in CwbvhTris[]
    uint PrimitiveCount;  // [cnt] triangles in this BLAS
    uint Reserved;        // [-] 0
};

// 32 B — one top-level node, tinybvh's BVHNode as it is uploaded. Leaf when the primitive count is non-zero, and the
//    leaf's primitives are TlasPrimitives[leftFirst + j].
struct TraversalTlasNode
{
    vec4 MinAndLeftFirst;   // [m, bits] xyz = AABB minimum, w = leftFirst bit-cast
    vec4 MaxAndPrimCount;   // [m, bits] xyz = AABB maximum, w = primitive count bit-cast
};

// World → object for a POINT and for a DIRECTION. ⚠️ The stored inverse is a column-major world→object matrix, so
//    ROW r of it is (Inv0[r], Inv1[r], Inv2[r], Inv3[r]) — not (Invr.x, Invr.y, Invr.z, Invr.w). Writing the obvious
//    `dot(Inv0.xyz, P) + Inv0.w` applies the TRANSPOSE of the intended matrix, and the failure is well hidden: the
//    transpose of an identity is an identity, and a pure translation is symmetric, so the bug is invisible in the
//    identity case and in every translate-only scene. It shows up the moment an instance rotates.
//    The CPU mirror is InstanceAcceleration::TraceClosest, whose object-space x is
//    `Inv[0]*O.x + Inv[4]*O.y + Inv[8]*O.z + Inv[12]` — the same row. The proof's kernel-form gate (§③c) transcribes
//    both forms and compares them, because a GPU is not needed to catch a transposed matrix.
vec3 InstanceWorldToObject(TraversalTlasInstance I, vec3 P)
{
    return vec3(dot(vec3(I.Inv0.x, I.Inv1.x, I.Inv2.x), P) + I.Inv3.x,
                dot(vec3(I.Inv0.y, I.Inv1.y, I.Inv2.y), P) + I.Inv3.y,
                dot(vec3(I.Inv0.z, I.Inv1.z, I.Inv2.z), P) + I.Inv3.z);
}

vec3 InstanceWorldToObjectDirection(TraversalTlasInstance I, vec3 D)
{
    return vec3(dot(vec3(I.Inv0.x, I.Inv1.x, I.Inv2.x), D),
                dot(vec3(I.Inv0.y, I.Inv1.y, I.Inv2.y), D),
                dot(vec3(I.Inv0.z, I.Inv1.z, I.Inv2.z), D));
}

// The inverse transpose of an instance's world→object matrix, applied to an OBJECT-space normal: the stored matrix is
// M⁻¹ column-major, so (M⁻¹)ᵀ's rows are its columns and the product is three dots. A face normal read from the
// object-space triangle comes back pointing the right way in world space under any rotation, scale or shear.
vec3 InstanceNormalToWorld(TraversalTlasInstance Instance, vec3 ObjectNormal)
{
    return vec3(dot(Instance.Inv0.xyz, ObjectNormal),
                dot(Instance.Inv1.xyz, ObjectNormal),
                dot(Instance.Inv2.xyz, ObjectNormal));
}

#endif // FRONTIER_TRAVERSAL_RECORDS

//============================================================================================================================================
// 📦 Engine/Shaders/TraversalCWBVH.slang — 8-wide compressed BVH traversal (Ylitie, Karras, Laine 2017)
//============================================================================================================================================
// Port of tinybvh kernels/traverse_cwbvh.cl (Bikker, MIT; itself after AlanWBFT's CUDA) to GLSL. Consumes the two blobs
//    written by Engine/GeometricRaster/TraversalIndex (nodes 5 × vec4, triangles 3 × vec4: e1, e2, v0 with prim in v0.w).
//    Data layout, octant-inverse trick, hit-mask bookkeeping and the 24-bit split between node and triangle groups
//    are kept 1:1 so results match tinybvh's CPU traversal bit for bit (checked in the CPU harness, R3 proofs).
//    NVIDIA-only PTX shortcuts (activemask, bfind, prmt, vshl) are replaced by their portable equivalents, exactly
//    as the OpenCL kernel's non-NVIDIA path does. Traversal is stack-based (32 entries) — no dynamic-stack fallback
//    is needed for CWBVH depths on Sponza-class scenes (tinybvh asserts the same).
//
// Include contract: the including shader declares
//    layout(std430, binding = ?) readonly buffer TraversalNodeExtent { vec4 CwbvhNodes[]; };
//    layout(std430, binding = ?) readonly buffer TraversalLeafExtent { vec4 CwbvhTris[];  };
// before including this file, and calls TraverseClosest / TraverseOccluded.

#ifndef FRONTIER_TRAVERSAL_CWBVH
#define FRONTIER_TRAVERSAL_CWBVH

const uint kTraversalStackSize = 32u;

struct TraversalHit
{
    float t;          // [m]  distance, kInfinite when missed
    float u, v;       // [-]  barycentrics of (v1, v2) in tinybvh's e1 = v2 − v0, e2 = v1 − v0 convention
    uint  primitive;  // [-]  flat triangle index (0xFFFFFFFF when missed)
};

uint SignExtendS8x4(uint i)
{
    // tinybvh non-NVIDIA path: extend the sign bit of each byte across that byte.
    uint b0 = (i & 0x80000000u) != 0u ? 0xFF000000u : 0u;
    uint b1 = (i & 0x00800000u) != 0u ? 0x00FF0000u : 0u;
    uint b2 = (i & 0x00008000u) != 0u ? 0x0000FF00u : 0u;
    uint b3 = (i & 0x00000080u) != 0u ? 0x000000FFu : 0u;
    return b0 + b1 + b2 + b3;
}

vec4 UnpackU8x4(uint v) { return vec4(float(v & 255u), float((v >> 8u) & 255u), float((v >> 16u) & 255u), float(v >> 24u)); }

// Intersects the 8 children of one node; returns the hit mask (children as 8 high bits + triangle bits).
void TraverseChildren(in vec4 n0, in vec4 n1, in vec4 n2, in vec4 n3, in vec4 n4, in vec3 O, in vec3 rD, in float tmax, in uint octinv4,
                      out uint hitmask)
{
    uint ew = floatBitsToUint(n0.w);
    int ex = int(ew & 255u), ey = int((ew >> 8u) & 255u), ez = int((ew >> 16u) & 255u);
    ex = ex > 127 ? ex - 256 : ex; ey = ey > 127 ? ey - 256 : ey; ez = ez > 127 ? ez - 256 : ez;   // signed char
    float idirx = uintBitsToFloat(uint(ex + 127) << 23u) * rD.x;
    float idiry = uintBitsToFloat(uint(ey + 127) << 23u) * rD.y;
    float idirz = uintBitsToFloat(uint(ez + 127) << 23u) * rD.z;
    float origx = (n0.x - O.x) * rD.x, origy = (n0.y - O.y) * rD.y, origz = (n0.z - O.z) * rD.z;
    hitmask = 0u;

    for (uint side = 0u; side < 2u; ++side)
    {
        uint meta4      = floatBitsToUint(side == 0u ? n1.z : n1.w);
        uint isInner4   = (meta4 & (meta4 << 1u)) & 0x10101010u;
        uint innerMask4 = SignExtendS8x4(isInner4 << 3u);
        uint bitIndex4  = (meta4 ^ (octinv4 & innerMask4)) & 0x1F1F1F1Fu;
        uint childBits4 = (meta4 >> 5u) & 0x07070707u;

        uint qxLo = floatBitsToUint(side == 0u ? n2.x : n2.y), qxHi = floatBitsToUint(side == 0u ? n3.z : n3.w);
        uint qyLo = floatBitsToUint(side == 0u ? n2.z : n2.w), qyHi = floatBitsToUint(side == 0u ? n4.x : n4.y);
        uint qzLo = floatBitsToUint(side == 0u ? n3.x : n3.y), qzHi = floatBitsToUint(side == 0u ? n4.z : n4.w);
        vec4 lox4 = UnpackU8x4(rD.x < 0.0 ? qxHi : qxLo), hix4 = UnpackU8x4(rD.x < 0.0 ? qxLo : qxHi);
        vec4 loy4 = UnpackU8x4(rD.y < 0.0 ? qyHi : qyLo), hiy4 = UnpackU8x4(rD.y < 0.0 ? qyLo : qyHi);
        vec4 loz4 = UnpackU8x4(rD.z < 0.0 ? qzHi : qzLo), hiz4 = UnpackU8x4(rD.z < 0.0 ? qzLo : qzHi);

        vec4 tminx4 = lox4 * idirx + origx, tmaxx4 = hix4 * idirx + origx;
        vec4 tminy4 = loy4 * idiry + origy, tmaxy4 = hiy4 * idiry + origy;
        vec4 tminz4 = loz4 * idirz + origz, tmaxz4 = hiz4 * idirz + origz;
        vec4 cmin = max(max(max(tminx4, tminy4), tminz4), vec4(0.0));
        vec4 cmax = min(min(min(tmaxx4, tmaxy4), tmaxz4), vec4(tmax));

        if (cmin.x <= cmax.x) hitmask |= ( childBits4         & 255u) << ( bitIndex4         & 31u);
        if (cmin.y <= cmax.y) hitmask |= ((childBits4 >>  8u) & 255u) << ((bitIndex4 >>  8u) & 31u);
        if (cmin.z <= cmax.z) hitmask |= ((childBits4 >> 16u) & 255u) << ((bitIndex4 >> 16u) & 31u);
        if (cmin.w <= cmax.w) hitmask |= ( childBits4 >> 24u        ) << ( bitIndex4 >> 24u        );
    }
}

// Closest hit. O, D world space; rD = 1/D (caller computes once). tmax bounds the search.
TraversalHit TraverseClosest(vec3 O, vec3 D, vec3 rD, float tmax)
{
    TraversalHit hit; hit.t = tmax; hit.u = 0.0; hit.v = 0.0; hit.primitive = 0xFFFFFFFFu;

    uvec2 stack[kTraversalStackSize];
    uint  stackPtr = 0u;
    uint  octinv4  = (7u - ((D.x < 0.0 ? 4u : 0u) | (D.y < 0.0 ? 2u : 0u) | (D.z < 0.0 ? 1u : 0u))) * 0x01010101u;
    uvec2 ngroup   = uvec2(0u, 0x80000000u), tgroup = uvec2(0u);

    for (uint guard = 0u; guard < 100000u; ++guard)
    {
        if (ngroup.y > 0x00FFFFFFu)
        {
            uint hits = ngroup.y, imask = ngroup.y;
            uint childBitIndex     = uint(findMSB(hits));
            uint childNodeBaseIndex = ngroup.x;
            ngroup.y &= ~(1u << childBitIndex);
            if (ngroup.y > 0x00FFFFFFu) { stack[stackPtr++] = ngroup; }

            uint slotIndex      = (childBitIndex - 24u) ^ (octinv4 & 255u);
            uint relativeIndex  = bitCount(imask & ~(0xFFFFFFFFu << slotIndex));
            uint childNodeIndex = (childNodeBaseIndex + relativeIndex) * 5u;
            vec4 n0 = CwbvhNodes[childNodeIndex + 0u], n1 = CwbvhNodes[childNodeIndex + 1u], n2 = CwbvhNodes[childNodeIndex + 2u];
            vec4 n3 = CwbvhNodes[childNodeIndex + 3u], n4 = CwbvhNodes[childNodeIndex + 4u];
            ngroup.x = floatBitsToUint(n1.x);
            tgroup   = uvec2(floatBitsToUint(n1.y), 0u);
            uint hitmask;
            TraverseChildren(n0, n1, n2, n3, n4, O, rD, hit.t, octinv4, hitmask);
            ngroup.y = (hitmask & 0xFF000000u) | (floatBitsToUint(n0.w) >> 24u);
            tgroup.y = hitmask & 0x00FFFFFFu;
        }
        else { tgroup = ngroup; ngroup = uvec2(0u); }

        while (tgroup.y != 0u)
        {
            // Möller–Trumbore, iquilezles form; tinybvh stores e1 = v2 − v0, e2 = v1 − v0.
            uint triangleIndex = uint(findMSB(tgroup.y));
            uint triAddr = tgroup.x + triangleIndex * 3u;
            vec3 e1 = CwbvhTris[triAddr].xyz;
            vec3 e2 = CwbvhTris[triAddr + 1u].xyz;
            vec4 v0 = CwbvhTris[triAddr + 2u];
            tgroup.y -= 1u << triangleIndex;
            vec3  r = cross(D, e1);
            float a = dot(e2, r);
            float f = 1.0 / a;
            vec3  s = O - v0.xyz;
            float u = f * dot(s, r);
            vec3  q = cross(s, e2);
            float v = f * dot(D, q);
            if (u < 0.0 || v < 0.0 || u + v > 1.0) continue;
            float d = f * dot(e1, q);
            if (d <= 0.0 || d >= hit.t) continue;
            hit.u = u; hit.v = v; hit.t = d; hit.primitive = floatBitsToUint(v0.w);
        }

        if (ngroup.y <= 0x00FFFFFFu)
        {
            if (stackPtr > 0u) ngroup = stack[--stackPtr];
            else break;
        }
    }
    return hit;
}

// Any hit in (0, tmax): early-out shadow query.
bool TraverseOccluded(vec3 O, vec3 D, vec3 rD, float tmax)
{
    uvec2 stack[kTraversalStackSize];
    uint  stackPtr = 0u;
    uint  octinv4  = (7u - ((D.x < 0.0 ? 4u : 0u) | (D.y < 0.0 ? 2u : 0u) | (D.z < 0.0 ? 1u : 0u))) * 0x01010101u;
    uvec2 ngroup   = uvec2(0u, 0x80000000u), tgroup = uvec2(0u);

    for (uint guard = 0u; guard < 100000u; ++guard)
    {
        if (ngroup.y > 0x00FFFFFFu)
        {
            uint hits = ngroup.y, imask = ngroup.y;
            uint childBitIndex      = uint(findMSB(hits));
            uint childNodeBaseIndex = ngroup.x;
            ngroup.y &= ~(1u << childBitIndex);
            if (ngroup.y > 0x00FFFFFFu) { stack[stackPtr++] = ngroup; }

            uint slotIndex      = (childBitIndex - 24u) ^ (octinv4 & 255u);
            uint relativeIndex  = bitCount(imask & ~(0xFFFFFFFFu << slotIndex));
            uint childNodeIndex = (childNodeBaseIndex + relativeIndex) * 5u;
            vec4 n0 = CwbvhNodes[childNodeIndex + 0u], n1 = CwbvhNodes[childNodeIndex + 1u], n2 = CwbvhNodes[childNodeIndex + 2u];
            vec4 n3 = CwbvhNodes[childNodeIndex + 3u], n4 = CwbvhNodes[childNodeIndex + 4u];
            ngroup.x = floatBitsToUint(n1.x);
            tgroup   = uvec2(floatBitsToUint(n1.y), 0u);
            uint hitmask;
            TraverseChildren(n0, n1, n2, n3, n4, O, rD, tmax, octinv4, hitmask);
            ngroup.y = (hitmask & 0xFF000000u) | (floatBitsToUint(n0.w) >> 24u);
            tgroup.y = hitmask & 0x00FFFFFFu;
        }
        else { tgroup = ngroup; ngroup = uvec2(0u); }

        while (tgroup.y != 0u)
        {
            uint triangleIndex = uint(findMSB(tgroup.y));
            uint triAddr = tgroup.x + triangleIndex * 3u;
            vec3 e1 = CwbvhTris[triAddr].xyz;
            vec3 e2 = CwbvhTris[triAddr + 1u].xyz;
            vec4 v0 = CwbvhTris[triAddr + 2u];
            tgroup.y -= 1u << triangleIndex;
            vec3  r = cross(D, e1);
            float a = dot(e2, r);
            float f = 1.0 / a;
            vec3  s = O - v0.xyz;
            float u = f * dot(s, r);
            vec3  q = cross(s, e2);
            float v = f * dot(D, q);
            if (u < 0.0 || v < 0.0 || u + v > 1.0) continue;
            float d = f * dot(e1, q);
            if (d > 0.0 && d < tmax) return true;
        }

        if (ngroup.y <= 0x00FFFFFFu)
        {
            if (stackPtr > 0u) ngroup = stack[--stackPtr];
            else break;
        }
    }
    return false;
}

#endif // FRONTIER_TRAVERSAL_CWBVH

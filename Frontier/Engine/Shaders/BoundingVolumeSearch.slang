//==========================================================================================================================================
//                                                 BOUNDING VOLUME SEARCH
//==========================================================================================================================================
// 🧩 GTX-first software SAH BoundingVolumeAtlas ray queries in Slang compute (zero hardware RTX raytracing required):
//    - Ordered child-slab closest opaque ray search
//    - Two-interface curved dielectric glass refraction peel (Snell refraction + Beer-Lambert spectral absorption)
//    - Colored Beer-Lambert shadow transmittance ray search (interior seats lit through windshield!)

#pragma once

static const uint kBvhInteriorFlag = 0x80000000u;
static const uint kPackedTriStride = 5u;

struct GpuSurfaceHit
{
    bool   Hit;
    float  DistanceM;
    uint   ClassId;
    float3 WorldPos;
    float3 GeoNormal;
    float2 TexCoord;
};

struct GpuGlassPeelHit
{
    bool   HitGlass;
    float  FrontT;
    float  ThicknessM;
    float  Ior;
    float3 FrontNormal;
    float3 AbsorptionPerM;
};

bool IntersectGpuSlab(float3 Lo, float3 Hi, float3 Origin, float3 InvDir, float MinT, float MaxT, out float OutEntryT)
{
    float3 T0v = (Lo - Origin) * InvDir;
    float3 T1v = (Hi - Origin) * InvDir;
    float3 TLo = min(T0v, T1v);
    float3 THi = max(T0v, T1v);
    float  T0  = max(MinT, max(TLo.x, max(TLo.y, TLo.z)));
    float  T1  = min(MaxT, min(THi.x, min(THi.y, THi.z)));
    OutEntryT  = T0;
    return T1 >= T0;
}

GpuSurfaceHit SearchBoundingVolumeOpaque(StructuredBuffer<float4> BvhCells,
                                         StructuredBuffer<float4> BvhTriangles,
                                         uint                     CellCount,
                                         float3                   Origin,
                                         float3                   RayDir,
                                         float                    MinT,
                                         float                    MaxT)
{
    GpuSurfaceHit Out;
    Out.Hit       = false;
    Out.DistanceM = MaxT;
    Out.ClassId   = 0u;
    Out.WorldPos  = Origin;
    Out.GeoNormal = float3(0.0f, 1.0f, 0.0f);
    Out.TexCoord  = float2(0.0f);

    if (CellCount == 0u)
    {
        return Out;
    }

    float3 SafeD  = select(abs(RayDir) > 1.0e-8f, RayDir, select(RayDir >= 0.0f, float3(1.0e-8f), float3(-1.0e-8f)));
    float3 InvDir = 1.0f / SafeD;

    uint  Stack[48];
    int   StackTop = 0;
    float RootEntry;
    if (IntersectGpuSlab(BvhCells[0].xyz, BvhCells[1].xyz, Origin, InvDir, MinT, Out.DistanceM, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    uint  BestTriIdx = 0xFFFFFFFFu;
    float BestU      = 0.0f;
    float BestV      = 0.0f;

    while (StackTop > 0)
    {
        uint   CellIdx = Stack[--StackTop];
        float4 C0      = BvhCells[CellIdx * 2u + 0u];
        float4 C1      = BvhCells[CellIdx * 2u + 1u];
        uint   LeftOrF = asuint(C0.w);
        uint   RightOr = asuint(C1.w);

        if ((RightOr & kBvhInteriorFlag) != 0u)
        {
            uint  LeftIdx  = LeftOrF;
            uint  RightIdx = RightOr & ~kBvhInteriorFlag;
            float EntryL, EntryR;
            bool  HitL = IntersectGpuSlab(BvhCells[LeftIdx * 2u].xyz, BvhCells[LeftIdx * 2u + 1u].xyz, Origin, InvDir, MinT, Out.DistanceM, EntryL);
            bool  HitR = IntersectGpuSlab(BvhCells[RightIdx * 2u].xyz, BvhCells[RightIdx * 2u + 1u].xyz, Origin, InvDir, MinT, Out.DistanceM, EntryR);
            if (HitL && HitR)
            {
                if (EntryL < EntryR)
                {
                    if (StackTop + 2 <= 48)
                    {
                        Stack[StackTop++] = RightIdx;
                        Stack[StackTop++] = LeftIdx;
                    }
                }
                else
                {
                    if (StackTop + 2 <= 48)
                    {
                        Stack[StackTop++] = LeftIdx;
                        Stack[StackTop++] = RightIdx;
                    }
                }
            }
            else if (HitL && StackTop + 1 <= 48)
            {
                Stack[StackTop++] = LeftIdx;
            }
            else if (HitR && StackTop + 1 <= 48)
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            for (uint Offset = 0u; Offset < RightOr; ++Offset)
            {
                uint   TriIdx    = LeftOrF + Offset;
                uint   Base      = TriIdx * kPackedTriStride;
                float4 T2        = BvhTriangles[Base + 2u];
                uint   ClassMask = asuint(T2.w);
                if ((ClassMask & 0x80000000u) != 0u) // Skip transmissive glass during opaque closest search
                {
                    continue;
                }
                float3 V0   = BvhTriangles[Base + 0u].xyz;
                float3 E1   = BvhTriangles[Base + 1u].xyz;
                float3 E2   = T2.xyz;
                float3 Pvec = cross(RayDir, E2);
                float  Det  = dot(E1, Pvec);
                if (abs(Det) <= 1.0e-9f)
                {
                    continue;
                }
                float  InvDet = 1.0f / Det;
                float3 Tvec   = Origin - V0;
                float  U      = dot(Tvec, Pvec) * InvDet;
                if (U < 0.0f || U > 1.0f)
                {
                    continue;
                }
                float3 Qvec = cross(Tvec, E1);
                float  V    = dot(RayDir, Qvec) * InvDet;
                if (V < 0.0f || (U + V) > 1.0f)
                {
                    continue;
                }
                float HitT = dot(E2, Qvec) * InvDet;
                if (HitT >= MinT && HitT < Out.DistanceM)
                {
                    Out.DistanceM = HitT;
                    BestTriIdx    = TriIdx;
                    BestU         = U;
                    BestV         = V;
                }
            }
        }
    }

    if (BestTriIdx != 0xFFFFFFFFu)
    {
        uint   Base = BestTriIdx * kPackedTriStride;
        float4 T0   = BvhTriangles[Base + 0u];
        float4 T1   = BvhTriangles[Base + 1u];
        float4 T2   = BvhTriangles[Base + 2u];
        float4 T3   = BvhTriangles[Base + 3u];
        float  W    = 1.0f - BestU - BestV;
        float3 N    = normalize(cross(T1.xyz, T2.xyz));
        if (dot(N, RayDir) > 0.0f)
        {
            N = -N;
        }
        Out.Hit       = true;
        Out.ClassId   = asuint(T2.w) & 0x7FFFFFFFu;
        Out.WorldPos  = Origin + RayDir * Out.DistanceM;
        Out.GeoNormal = N;
        Out.TexCoord  = float2(T0.w, T1.w) * W + T3.xy * BestU + T3.zw * BestV;
    }
    return Out;
}

GpuGlassPeelHit SearchBoundingVolumeGlassPeel(StructuredBuffer<float4> BvhCells,
                                              StructuredBuffer<float4> BvhTriangles,
                                              uint                     CellCount,
                                              float3                   Origin,
                                              float3                   RayDir,
                                              float                    MinT,
                                              float                    MaxOpaqueT)
{
    GpuGlassPeelHit Peel;
    Peel.HitGlass       = false;
    Peel.FrontT         = MaxOpaqueT;
    Peel.ThicknessM     = 0.012f;
    Peel.Ior            = 1.52f;
    Peel.FrontNormal    = float3(0.0f, 1.0f, 0.0f);
    Peel.AbsorptionPerM = float3(0.12f, 0.04f, 0.06f);

    if (CellCount == 0u)
    {
        return Peel;
    }

    float3 SafeD  = select(abs(RayDir) > 1.0e-8f, RayDir, select(RayDir >= 0.0f, float3(1.0e-8f), float3(-1.0e-8f)));
    float3 InvDir = 1.0f / SafeD;

    uint  Stack[48];
    int   StackTop = 0;
    float RootEntry;
    if (IntersectGpuSlab(BvhCells[0].xyz, BvhCells[1].xyz, Origin, InvDir, MinT, MaxOpaqueT, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    float BackT = 0.0f;
    while (StackTop > 0)
    {
        uint   CellIdx = Stack[--StackTop];
        float4 C0      = BvhCells[CellIdx * 2u + 0u];
        float4 C1      = BvhCells[CellIdx * 2u + 1u];
        uint   LeftOrF = asuint(C0.w);
        uint   RightOr = asuint(C1.w);

        if ((RightOr & kBvhInteriorFlag) != 0u)
        {
            uint  LeftIdx  = LeftOrF;
            uint  RightIdx = RightOr & ~kBvhInteriorFlag;
            float EntryL, EntryR;
            if (IntersectGpuSlab(BvhCells[LeftIdx * 2u].xyz, BvhCells[LeftIdx * 2u + 1u].xyz, Origin, InvDir, MinT, MaxOpaqueT, EntryL) && StackTop < 48)
            {
                Stack[StackTop++] = LeftIdx;
            }
            if (IntersectGpuSlab(BvhCells[RightIdx * 2u].xyz, BvhCells[RightIdx * 2u + 1u].xyz, Origin, InvDir, MinT, MaxOpaqueT, EntryR) && StackTop < 48)
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            for (uint Offset = 0u; Offset < RightOr; ++Offset)
            {
                uint   Base      = (LeftOrF + Offset) * kPackedTriStride;
                float4 T2        = BvhTriangles[Base + 2u];
                uint   ClassMask = asuint(T2.w);
                if ((ClassMask & 0x80000000u) == 0u)
                {
                    continue;
                }
                float3 V0   = BvhTriangles[Base + 0u].xyz;
                float3 E1   = BvhTriangles[Base + 1u].xyz;
                float3 E2   = T2.xyz;
                float3 Pvec = cross(RayDir, E2);
                float  Det  = dot(E1, Pvec);
                if (abs(Det) <= 1.0e-9f)
                {
                    continue;
                }
                float  InvDet = 1.0f / Det;
                float3 Tvec   = Origin - V0;
                float  U      = dot(Tvec, Pvec) * InvDet;
                if (U < 0.0f || U > 1.0f)
                {
                    continue;
                }
                float3 Qvec = cross(Tvec, E1);
                float  V    = dot(RayDir, Qvec) * InvDet;
                if (V < 0.0f || (U + V) > 1.0f)
                {
                    continue;
                }
                float HitT = dot(E2, Qvec) * InvDet;
                if (HitT >= MinT && HitT < MaxOpaqueT)
                {
                    if (HitT < Peel.FrontT)
                    {
                        if (Peel.HitGlass)
                        {
                            BackT = max(BackT, Peel.FrontT);
                        }
                        Peel.HitGlass       = true;
                        Peel.FrontT         = HitT;
                        float3 N            = normalize(cross(E1, E2));
                        Peel.FrontNormal    = (dot(N, RayDir) > 0.0f) ? -N : N;
                        float4 T4           = BvhTriangles[Base + 4u];
                        Peel.AbsorptionPerM = T4.xyz;
                        Peel.Ior            = max(1.02f, T4.w);
                    }
                    else if (HitT > Peel.FrontT + 0.004f)
                    {
                        BackT = max(BackT, HitT);
                    }
                }
            }
        }
    }

    if (Peel.HitGlass && BackT > Peel.FrontT && BackT < MaxOpaqueT)
    {
        Peel.ThicknessM = clamp(BackT - Peel.FrontT, 0.008f, 0.35f);
    }
    return Peel;
}

float3 SearchBoundingVolumeShadowTransmittance(StructuredBuffer<float4> BvhCells,
                                               StructuredBuffer<float4> BvhTriangles,
                                               uint                     CellCount,
                                               float3                   Origin,
                                               float3                   RayDir,
                                               float                    MinT,
                                               float                    MaxT)
{
    if (CellCount == 0u)
    {
        return float3(1.0f);
    }

    float3 SafeD  = select(abs(RayDir) > 1.0e-8f, RayDir, select(RayDir >= 0.0f, float3(1.0e-8f), float3(-1.0e-8f)));
    float3 InvDir = 1.0f / SafeD;

    uint  Stack[48];
    int   StackTop = 0;
    float RootEntry;
    if (IntersectGpuSlab(BvhCells[0].xyz, BvhCells[1].xyz, Origin, InvDir, MinT, MaxT, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    float3 Trans = float3(1.0f);
    while (StackTop > 0)
    {
        uint   CellIdx = Stack[--StackTop];
        float4 C0      = BvhCells[CellIdx * 2u + 0u];
        float4 C1      = BvhCells[CellIdx * 2u + 1u];
        uint   LeftOrF = asuint(C0.w);
        uint   RightOr = asuint(C1.w);

        if ((RightOr & kBvhInteriorFlag) != 0u)
        {
            uint  LeftIdx  = LeftOrF;
            uint  RightIdx = RightOr & ~kBvhInteriorFlag;
            float EntryL, EntryR;
            if (IntersectGpuSlab(BvhCells[LeftIdx * 2u].xyz, BvhCells[LeftIdx * 2u + 1u].xyz, Origin, InvDir, MinT, MaxT, EntryL) && StackTop < 48)
            {
                Stack[StackTop++] = LeftIdx;
            }
            if (IntersectGpuSlab(BvhCells[RightIdx * 2u].xyz, BvhCells[RightIdx * 2u + 1u].xyz, Origin, InvDir, MinT, MaxT, EntryR) && StackTop < 48)
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            for (uint Offset = 0u; Offset < RightOr; ++Offset)
            {
                uint   Base      = (LeftOrF + Offset) * kPackedTriStride;
                float4 T2        = BvhTriangles[Base + 2u];
                uint   ClassMask = asuint(T2.w);
                float3 V0        = BvhTriangles[Base + 0u].xyz;
                float3 E1        = BvhTriangles[Base + 1u].xyz;
                float3 E2        = T2.xyz;
                float3 Pvec      = cross(RayDir, E2);
                float  Det       = dot(E1, Pvec);
                if (abs(Det) <= 1.0e-9f)
                {
                    continue;
                }
                float  InvDet = 1.0f / Det;
                float3 Tvec   = Origin - V0;
                float  U      = dot(Tvec, Pvec) * InvDet;
                if (U < 0.0f || U > 1.0f)
                {
                    continue;
                }
                float3 Qvec = cross(Tvec, E1);
                float  V    = dot(RayDir, Qvec) * InvDet;
                if (V < 0.0f || (U + V) > 1.0f)
                {
                    continue;
                }
                float HitT = dot(E2, Qvec) * InvDet;
                if (HitT >= MinT && HitT < MaxT)
                {
                    if ((ClassMask & 0x80000000u) == 0u)
                    {
                        return float3(0.0f);
                    }
                    float4 T4 = BvhTriangles[Base + 4u];
                    float  F0 = pow((max(1.02f, T4.w) - 1.0f) / (max(1.02f, T4.w) + 1.0f), 2.0f);
                    Trans    *= (1.0f - F0) * exp(-T4.xyz * 0.025f);
                }
            }
        }
    }
    return Trans;
}

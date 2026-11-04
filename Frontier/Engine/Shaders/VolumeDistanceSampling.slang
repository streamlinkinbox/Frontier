//==========================================================================================================================================
//                                                VOLUME DISTANCE SAMPLING
//==========================================================================================================================================
// 🧩 GPU trilinear evaluation, tetrahedron gradient normal, fast solar cone shadow, ambient occlusion, & sphere-tracing
//    over a float4-packed .sdfspace VolumeDistanceField (zero hardware RTX raytracing required).

#pragma once

float FetchPackedVoxelDistance(StructuredBuffer<float4> VolumeTable, uint LinearVoxelIndex)
{
    uint   QuadSlot = 4u + (LinearVoxelIndex >> 2u);
    uint   Lane     = LinearVoxelIndex & 3u;
    float4 Quad     = VolumeTable[QuadSlot];
    return (Lane == 0u) ? Quad.x : ((Lane == 1u) ? Quad.y : ((Lane == 2u) ? Quad.z : Quad.w));
}

float SampleGpuVolumeDistance(StructuredBuffer<float4> VolumeTable, float3 WorldPoint)
{
    float4 H3 = VolumeTable[3];
    if (H3.w < 0.5f)
    {
        return 1.0e6f;
    }
    float4 H0 = VolumeTable[0];
    float4 H1 = VolumeTable[1];
    float4 H2 = VolumeTable[2];

    uint NX = uint(H0.w + 0.5f);
    uint NY = uint(H1.w + 0.5f);
    uint NZ = uint(H2.w + 0.5f);
    if (NX == 0u || NY == 0u || NZ == 0u)
    {
        return 1.0e6f;
    }

    float3 ClampedP = clamp(WorldPoint, H0.xyz, H1.xyz);
    float  Exterior = length(WorldPoint - ClampedP);

    float3 GridPos  = clamp((ClampedP - H0.xyz) / max(H2.xyz, float3(1.0e-5f)) - 0.5f,
                            float3(0.0f),
                            float3(float(NX - 1u), float(NY - 1u), float(NZ - 1u)));
    uint3  Idx0     = uint3(GridPos);
    uint3  Idx1     = min(Idx0 + uint3(1u, 1u, 1u), uint3(NX - 1u, NY - 1u, NZ - 1u));
    float3 Frac     = GridPos - float3(Idx0);

    uint StrideY = NX;
    uint StrideZ = NX * NY;

    float D000 = FetchPackedVoxelDistance(VolumeTable, Idx0.z * StrideZ + Idx0.y * StrideY + Idx0.x);
    float D100 = FetchPackedVoxelDistance(VolumeTable, Idx0.z * StrideZ + Idx0.y * StrideY + Idx1.x);
    float D010 = FetchPackedVoxelDistance(VolumeTable, Idx0.z * StrideZ + Idx1.y * StrideY + Idx0.x);
    float D110 = FetchPackedVoxelDistance(VolumeTable, Idx0.z * StrideZ + Idx1.y * StrideY + Idx1.x);
    float D001 = FetchPackedVoxelDistance(VolumeTable, Idx1.z * StrideZ + Idx0.y * StrideY + Idx0.x);
    float D101 = FetchPackedVoxelDistance(VolumeTable, Idx1.z * StrideZ + Idx0.y * StrideY + Idx1.x);
    float D011 = FetchPackedVoxelDistance(VolumeTable, Idx1.z * StrideZ + Idx1.y * StrideY + Idx0.x);
    float D111 = FetchPackedVoxelDistance(VolumeTable, Idx1.z * StrideZ + Idx1.y * StrideY + Idx1.x);

    float C00 = lerp(D000, D100, Frac.x);
    float C10 = lerp(D010, D110, Frac.x);
    float C01 = lerp(D001, D101, Frac.x);
    float C11 = lerp(D011, D111, Frac.x);
    float C0  = lerp(C00, C10, Frac.y);
    float C1  = lerp(C01, C11, Frac.y);
    return lerp(C0, C1, Frac.z) + Exterior;
}

float3 SampleGpuVolumeNormal(StructuredBuffer<float4> VolumeTable, float3 WorldPoint)
{
    float4 H2  = VolumeTable[2];
    float  Eps = max(0.02f, 0.5f * min(H2.x, min(H2.y, H2.z)));
    float  Dxp = SampleGpuVolumeDistance(VolumeTable, WorldPoint + float3( Eps, -Eps, -Eps));
    float  Dxm = SampleGpuVolumeDistance(VolumeTable, WorldPoint + float3(-Eps, -Eps,  Eps));
    float  Dyp = SampleGpuVolumeDistance(VolumeTable, WorldPoint + float3(-Eps,  Eps, -Eps));
    float  Dzp = SampleGpuVolumeDistance(VolumeTable, WorldPoint + float3( Eps,  Eps,  Eps));
    float3 Grad = float3(Dxp - Dxm - Dyp + Dzp, -Dxp - Dxm + Dyp + Dzp, -Dxp + Dxm - Dyp + Dzp);
    float  Len  = length(Grad);
    return (Len > 1.0e-6f) ? (Grad / Len) : float3(0.0f, 1.0f, 0.0f);
}

float TraceGpuVolumeConeShadow(StructuredBuffer<float4> VolumeTable,
                               float3                   OriginMeters,
                               float3                   SurfaceNormal,
                               float3                   LightDirectionUnit,
                               float                    MaxDistanceMeters,
                               float                    ConeSharpness,
                               int                      MaxSteps)
{
    if (VolumeTable[3].w < 0.5f)
    {
        return 1.0f;
    }
    float3 StepSizes = VolumeTable[2].xyz;
    float  VoxelDiag = max(0.04f, max(StepSizes.x, max(StepSizes.y, StepSizes.z)));
    float3 StartPos  = OriginMeters + SurfaceNormal * (VoxelDiag * 1.15f);
    float  Travel    = VoxelDiag * 0.90f;
    float  Vis       = 1.0f;

    for (int Step = 0; Step < MaxSteps; ++Step)
    {
        if (Travel >= MaxDistanceMeters)
        {
            break;
        }
        float Dist = SampleGpuVolumeDistance(VolumeTable, StartPos + LightDirectionUnit * Travel);
        if (Dist <= 0.002f)
        {
            return 0.04f;
        }
        Vis     = min(Vis, saturate(ConeSharpness * Dist / max(Travel, 0.06f)));
        Travel += max(Dist * 0.82f, VoxelDiag * 0.55f);
    }
    return clamp(Vis, 0.04f, 1.0f);
}

float EvaluateGpuVolumeAmbientOcclusion(StructuredBuffer<float4> VolumeTable,
                                        float3                   OriginMeters,
                                        float3                   SurfaceNormal,
                                        float                    MaxRadiusMeters)
{
    if (VolumeTable[3].w < 0.5f)
    {
        return 1.0f;
    }
    float Occlusion = 0.0f;
    float Weight    = 1.0f;
    [unroll]
    for (int Tap = 1; Tap <= 5; ++Tap)
    {
        float ProbeDist = MaxRadiusMeters * (float(Tap) / 5.0f);
        float SignedD   = SampleGpuVolumeDistance(VolumeTable, OriginMeters + SurfaceNormal * ProbeDist);
        Occlusion      += Weight * max(0.0f, ProbeDist - SignedD) / max(ProbeDist, 1.0e-3f);
        Weight         *= 0.58f;
    }
    return saturate(1.0f - 0.55f * Occlusion);
}

bool TraceGpuVolumeRay(StructuredBuffer<float4> VolumeTable,
                       float3                   OriginMeters,
                       float3                   DirectionUnit,
                       float                    MaxDistanceMeters,
                       int                      MaxSteps,
                       out float                OutHitDistanceM)
{
    OutHitDistanceM = MaxDistanceMeters;
    if (VolumeTable[3].w < 0.5f)
    {
        return false;
    }
    float3 StepSizes = VolumeTable[2].xyz;
    float  VoxelMin  = max(0.03f, min(StepSizes.x, min(StepSizes.y, StepSizes.z)));
    float  Travel    = VoxelMin * 1.15f;
    for (int Step = 0; Step < MaxSteps; ++Step)
    {
        if (Travel >= MaxDistanceMeters)
        {
            break;
        }
        float Dist = SampleGpuVolumeDistance(VolumeTable, OriginMeters + DirectionUnit * Travel);
        if (Dist <= VoxelMin * 0.30f)
        {
            OutHitDistanceM = Travel;
            return true;
        }
        Travel += max(Dist * 0.88f, VoxelMin * 0.45f);
    }
    return false;
}

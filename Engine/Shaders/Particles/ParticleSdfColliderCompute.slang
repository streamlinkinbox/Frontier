/*============================================================================================================================================
                                                    PARTICLESDFCOLLIDERCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute narrow-band 8x8x8 sparse brick 3D SignedDistanceVolume particle collider with trilinear distance & gradient bounce

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]  ConstantBuffer<GpuDomainUniforms>     DomainParams;
[[vk::binding(1, 0)]]  RWStructuredBuffer<GpuKinematicsSlot> KinematicsBuffer;
[[vk::binding(3, 0)]]  RWStructuredBuffer<GpuAppearanceSlot> AppearanceBuffer;
[[vk::binding(12, 0)]] StructuredBuffer<uint>                SdfIndirectionBuffer;
[[vk::binding(13, 0)]] StructuredBuffer<float>               SdfCoarseDistBuffer;
[[vk::binding(14, 0)]] StructuredBuffer<uint>                SdfBrickVoxelBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                               SPARSE 8x8x8 SDF EVALUATION
//------------------------------------------------------------------------------------------------------------------------

static const uint SdfBrickEdge        = 8u;
static const uint SdfUnallocatedBrick = 0xFFFFFFFFu;

float FetchSdfVoxel(int VoxX, int VoxY, int VoxZ, uint3 BrickDims, float NarrowBandMeters)
{
    int MaxX = int(BrickDims.x * SdfBrickEdge) - 1;
    int MaxY = int(BrickDims.y * SdfBrickEdge) - 1;
    int MaxZ = int(BrickDims.z * SdfBrickEdge) - 1;

    uint Cx = uint(clamp(VoxX, 0, MaxX));
    uint Cy = uint(clamp(VoxY, 0, MaxY));
    uint Cz = uint(clamp(VoxZ, 0, MaxZ));

    uint Bx = Cx / SdfBrickEdge;
    uint By = Cy / SdfBrickEdge;
    uint Bz = Cz / SdfBrickEdge;
    uint MacroIdx = (Bz * BrickDims.y + By) * BrickDims.x + Bx;

    uint BrickSlot = SdfIndirectionBuffer[MacroIdx];
    if (BrickSlot == SdfUnallocatedBrick)
    {
        return SdfCoarseDistBuffer[MacroIdx];
    }

    uint Lx = Cx % SdfBrickEdge;
    uint Ly = Cy % SdfBrickEdge;
    uint Lz = Cz % SdfBrickEdge;
    uint LocalIdx  = (Lz * SdfBrickEdge + Ly) * SdfBrickEdge + Lx;
    uint WordIndex = BrickSlot * 256u + (LocalIdx >> 1u);
    uint Packed    = SdfBrickVoxelBuffer[WordIndex];

    // Unpack signed 16-bit quantized snorm sample (exact match to SignedDistanceVolume::SparseDistanceBrick)
    int Quantized = ((LocalIdx & 1u) == 0u)
        ? (int(Packed << 16u) >> 16)
        : (int(Packed) >> 16);

    return (float(Quantized) / 32767.0) * NarrowBandMeters;
}

float SampleSparseSignedDistance(float3 WorldPos, float3 BoundsMin, float3 BoundsMax, float VoxelSpacing, uint3 BrickDims, float NarrowBandMeters)
{
    float3 Clamped  = clamp(WorldPos, BoundsMin, BoundsMax);
    float  Exterior = length(WorldPos - Clamped);

    float  InvSpace = 1.0 / max(1.0e-4, VoxelSpacing);
    float3 GridPos  = (Clamped - BoundsMin) * InvSpace - float3(0.5, 0.5, 0.5);

    int3 Lower = int3(floor(GridPos));
    float3 Frac = GridPos - float3(Lower);

    float C000 = FetchSdfVoxel(Lower.x,     Lower.y,     Lower.z,     BrickDims, NarrowBandMeters);
    float C100 = FetchSdfVoxel(Lower.x + 1, Lower.y,     Lower.z,     BrickDims, NarrowBandMeters);
    float C010 = FetchSdfVoxel(Lower.x,     Lower.y + 1, Lower.z,     BrickDims, NarrowBandMeters);
    float C110 = FetchSdfVoxel(Lower.x + 1, Lower.y + 1, Lower.z,     BrickDims, NarrowBandMeters);
    float C001 = FetchSdfVoxel(Lower.x,     Lower.y,     Lower.z + 1, BrickDims, NarrowBandMeters);
    float C101 = FetchSdfVoxel(Lower.x + 1, Lower.y,     Lower.z + 1, BrickDims, NarrowBandMeters);
    float C011 = FetchSdfVoxel(Lower.x,     Lower.y + 1, Lower.z + 1, BrickDims, NarrowBandMeters);
    float C111 = FetchSdfVoxel(Lower.x + 1, Lower.y + 1, Lower.z + 1, BrickDims, NarrowBandMeters);

    float C00 = lerp(C000, C100, Frac.x);
    float C10 = lerp(C010, C110, Frac.x);
    float C01 = lerp(C001, C101, Frac.x);
    float C11 = lerp(C011, C111, Frac.x);
    float C0  = lerp(C00,  C10,  Frac.y);
    float C1  = lerp(C01,  C11,  Frac.y);
    float Trilinear = lerp(C0, C1, Frac.z);

    return (Exterior > 0.0) ? (max(0.0, Trilinear) + Exterior) : Trilinear;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void CollideSignedDistanceMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Slot        = DispatchThreadId.x;
    uint ActiveCount = DomainParams.DomainControl.x;
    if (Slot >= ActiveCount)
    {
        return;
    }

    uint3 BrickDims = DomainParams.SdfBrickDims.xyz;
    if (BrickDims.x == 0u || BrickDims.y == 0u || BrickDims.z == 0u)
    {
        return;
    }

    GpuKinematicsSlot Kin = KinematicsBuffer[Slot];
    if (Kin.PositionInvMass.w <= 0.0)
    {
        return;
    }

    float3 BoundsMin       = DomainParams.SdfBoundsMinVox.xyz;
    float  VoxelSpacing    = max(1.0e-4, DomainParams.SdfBoundsMinVox.w);
    float3 BoundsMax       = DomainParams.ScalarPackD.xyz;
    float  NarrowBandM     = asfloat(DomainParams.SdfBrickDims.w);
    float  Restitution     = saturate(DomainParams.TimingCounts.w);
    float  SlideFriction   = saturate(DomainParams.ScalarPackD.w);
    float  ParticleRadius  = max(0.015, AppearanceBuffer[Slot].VisualMetrics.x);

    float3 Pos = Kin.PositionInvMass.xyz;
    float SignedDist = SampleSparseSignedDistance(Pos, BoundsMin, BoundsMax, VoxelSpacing, BrickDims, NarrowBandM);

    if (SignedDist < ParticleRadius)
    {
        float Step = max(1.0e-3, 0.5 * VoxelSpacing);
        float Gx = SampleSparseSignedDistance(Pos + float3(Step, 0.0, 0.0), BoundsMin, BoundsMax, VoxelSpacing, BrickDims, NarrowBandM)
                 - SampleSparseSignedDistance(Pos - float3(Step, 0.0, 0.0), BoundsMin, BoundsMax, VoxelSpacing, BrickDims, NarrowBandM);
        float Gy = SampleSparseSignedDistance(Pos + float3(0.0, Step, 0.0), BoundsMin, BoundsMax, VoxelSpacing, BrickDims, NarrowBandM)
                 - SampleSparseSignedDistance(Pos - float3(0.0, Step, 0.0), BoundsMin, BoundsMax, VoxelSpacing, BrickDims, NarrowBandM);
        float Gz = SampleSparseSignedDistance(Pos + float3(0.0, 0.0, Step), BoundsMin, BoundsMax, VoxelSpacing, BrickDims, NarrowBandM)
                 - SampleSparseSignedDistance(Pos - float3(0.0, 0.0, Step), BoundsMin, BoundsMax, VoxelSpacing, BrickDims, NarrowBandM);

        float3 Grad = float3(Gx, Gy, Gz);
        float  GradLen = length(Grad);
        float3 Normal = (GradLen > 1.0e-5) ? (Grad / GradLen) : float3(0.0, 0.0, 1.0);

        float Penetration = ParticleRadius - SignedDist;
        Pos += Normal * Penetration;

        float3 Vel = Kin.VelocityDrag.xyz;
        float  Vn  = dot(Vel, Normal);
        if (Vn < 0.0)
        {
            float3 Tangent = Vel - Normal * Vn;
            Vel = Tangent * (1.0 - SlideFriction) - Normal * (Vn * Restitution);
        }

        Kin.PositionInvMass.xyz = Pos;
        Kin.VelocityDrag.xyz    = Vel;
        KinematicsBuffer[Slot]  = Kin;
    }
}

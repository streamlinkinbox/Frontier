/*==========================================================================================================================================
                                               VOLUMETRICTYREDEFORMATION.SLANG
==========================================================================================================================================*/
// 🧩 Deforms the 3,968 smooth rubber vertices of ControlVehicleTyre.facet per wheel using the 10,448-byte VehicleVisualRecord
//    (32 circumferential x 5 meridional Neo-Hookean Hex8 continuum FEM nodal displacements), while preserving rigid RimBase/Hub.

struct VehicleDeformationPushConstants
{
    uint  WheelIndex;          // [0..3] - FL, FR, RL, RR
    uint  TyreVertexCount;     // [-]    - 1,984 smooth rubber vertices (3,968 facets)
    uint  TotalVertexCount;    // [-]    - Tyre + Rim vertices in composite wheel buffer
    uint  MirroredRightSide;   // [-]    - 1 on FR/RR (negate Y in cage lookup)
};

// 2,612 float scalars (10,448 bytes):
//   [0..51]     header & per-wheel analytical state ([20+4*w]: Shredded, CarcassActive, ModelId, Rsv; [36+4*w]: Phi, Defl, Press, Lat)
//   [52..2611]  4 wheels * 160 nodes * float4(ux, uy, uz, strain) in wheel-local coordinates
StructuredBuffer<float4>   VehicleVisualStateBuffer : register(t0);
StructuredBuffer<float4>   RestWheelVertices        : register(t1);
RWStructuredBuffer<float4> DeformedWheelVertices    : register(u0);

[[vk::push_constant]]
ConstantBuffer<VehicleDeformationPushConstants> PushConstants;

static const float PiScalar          = 3.14159265f;
static const float TyreRimRadiusM    = 0.34480f;
static const float TyreHalfWidthM    = 0.11800f;
static const uint  CircumferentialN  = 32u;
static const uint  MeridionalN       = 5u;
static const uint  NodesPerWheel     = 160u;
static const uint  HeaderVec4Offset  = 13u; // 52 floats / 4

float3 SampleContinuumDisplacement(uint Wheel, float3 LocalPos)
{
    float Angle = atan2(LocalPos.z, LocalPos.x);
    if (Angle < 0.0f)
    {
        Angle += 2.0f * PiScalar;
    }

    float SectorCoord = (Angle / (2.0f * PiScalar)) * float(CircumferentialN);
    uint  Sector0     = uint(floor(SectorCoord)) % CircumferentialN;
    uint  Sector1     = (Sector0 + 1u) % CircumferentialN;
    float AlphaS      = frac(SectorCoord);

    // Map lateral Y in [-0.118..+0.118] across meridional bands m = 0..4
    float NormY  = clamp((LocalPos.y + TyreHalfWidthM) / (2.0f * TyreHalfWidthM), 0.0f, 1.0f) * float(MeridionalN - 1u);
    uint  Band0  = min(uint(floor(NormY)), MeridionalN - 2u);
    uint  Band1  = Band0 + 1u;
    float AlphaM = clamp(NormY - float(Band0), 0.0f, 1.0f);

    uint BaseVec4 = HeaderVec4Offset + Wheel * NodesPerWheel;
    float3 U00 = VehicleVisualStateBuffer[BaseVec4 + Sector0 * MeridionalN + Band0].xyz;
    float3 U10 = VehicleVisualStateBuffer[BaseVec4 + Sector1 * MeridionalN + Band0].xyz;
    float3 U01 = VehicleVisualStateBuffer[BaseVec4 + Sector0 * MeridionalN + Band1].xyz;
    float3 U11 = VehicleVisualStateBuffer[BaseVec4 + Sector1 * MeridionalN + Band1].xyz;

    float3 U0 = lerp(U00, U10, AlphaS);
    float3 U1 = lerp(U01, U11, AlphaS);
    float  RadialWeight = saturate((length(LocalPos.xz) - TyreRimRadiusM) / (0.50855f - TyreRimRadiusM));
    return lerp(U0, U1, AlphaM) * RadialWeight;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint VertexIndex = DispatchThreadId.x;
    if (VertexIndex >= PushConstants.TotalVertexCount)
    {
        return;
    }

    float4 Rest = RestWheelVertices[VertexIndex];
    if (VertexIndex >= PushConstants.TyreVertexCount)
    {
        // Hard-edged RimBase / Hub vertices remain rigid on the hub spindle.
        DeformedWheelVertices[VertexIndex] = Rest;
        return;
    }

    uint   Wheel      = min(PushConstants.WheelIndex, 3u);
    float4 WheelState = VehicleVisualStateBuffer[5u + Wheel]; // float offset 20 + 4*w
    if (WheelState.x > 0.5f)
    {
        // Shredded rubber collapses onto rim bead so RimBase / Hub stay exposed.
        float2 Radial = Rest.xz;
        float  Len    = max(1.0e-5f, length(Radial));
        DeformedWheelVertices[VertexIndex] = float4(Radial * (TyreRimRadiusM / Len), Rest.y, Rest.w);
        return;
    }

    if (WheelState.y < 0.5f)
    {
        DeformedWheelVertices[VertexIndex] = Rest;
        return;
    }

    float  SignY    = PushConstants.MirroredRightSide != 0u ? -1.0f : 1.0f;
    float3 QueryPos = float3(Rest.x, Rest.y * SignY, Rest.z);
    float3 Disp     = SampleContinuumDisplacement(Wheel, QueryPos);
    Disp.y         *= SignY;

    DeformedWheelVertices[VertexIndex] = float4(Rest.xyz + Disp, Rest.w);
}

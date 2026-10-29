/*============================================================================================================================================
                                                       AREARECONNECTIONGPU.SLANG
============================================================================================================================================*/
// 🧩 Sample continuous facet area and compare joint area/direction reconnection estimators using production BVH rays.
#define GenerateVertexPayload AreaReferenceGenerate
#define EvaluateVertexPayload AreaReferenceEvaluate
#include "RgbTraceGpu.slang"
#undef GenerateVertexPayload
#undef EvaluateVertexPayload

uint AreaHash(uint Bits)
{
    Bits ^= Bits >> 16; Bits *= 0x7feb352du;
    Bits ^= Bits >> 15; Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

float FacetArea(uint Index)
{
    return .5 * length(cross(Facets[Index].B.xyz - Facets[Index].A.xyz,
                            Facets[Index].C.xyz - Facets[Index].A.xyz));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    SurfacePoint Input = Receivers[Pixel];
    uint Seed = asuint(Input.PositionHit.x), Method = asuint(Input.PositionHit.y);
    uint FacetCount = Extent.Sampling.z;
    float TotalArea = 0;
    for (uint Index = 0; Index < FacetCount; ++Index) TotalArea += FacetArea(Index);
    float3 AreaEstimate = 0, DirectionEstimate = 0;
    float WrongArea = 0, WrongJacobian = 0;
    for (uint Draw = 0; Draw < 8; ++Draw)
    {
        float Choice = float(AreaHash(Seed ^ (3 * Draw + 1) * 0x9e3779b9u) >> 8) / 16777216.0;
        float First = float(AreaHash(Seed ^ (3 * Draw + 2) * 0x9e3779b9u) >> 8) / 16777216.0;
        float Second = float(AreaHash(Seed ^ (3 * Draw + 3) * 0x9e3779b9u) >> 8) / 16777216.0;
        uint Selected = min(uint(Choice * FacetCount), FacetCount - 1);
        if (Method == 1)
        {
            float Cumulative = 0;
            Selected = FacetCount - 1;
            for (uint Index = 0; Index < FacetCount; ++Index)
            {
                Cumulative += FacetArea(Index) / TotalArea;
                if (Choice < Cumulative) { Selected = Index; break; }
            }
        }
        float Root = sqrt(First), U = 1 - Root, V = Root * Second;
        float Probability = Method == 0 ? 1.0 / FacetCount : FacetArea(Selected) / TotalArea;
        float Density = Probability / FacetArea(Selected);
        // 📝 Anchored affine interpolation preserves exactly coplanar coordinates for the grazing case.
        float3 Point = Facets[Selected].A.xyz + U * (Facets[Selected].B.xyz - Facets[Selected].A.xyz) +
            V * (Facets[Selected].C.xyz - Facets[Selected].A.xyz);
        SurfacePoint Surface = FacetSurface(Selected, Point, -Facets[Selected].NormalA.xyz, float3(0, U, V));
        bool SuffixVisible;
        float3 Outgoing = TraceSuffix(Surface, 1, SuffixVisible);
        bool ConnectionVisible = TraceReconnect(Surface, Input.ViewMetallic.xyz);
        float3 Delta = Input.ViewMetallic.xyz - Surface.PositionHit.xyz;
        float Squared = dot(Delta, Delta);
        float3 Direction = Delta / sqrt(Squared);
        float Cosine = dot(Surface.NormalRoughness.xyz, Direction);
        float ReceiverCosine = max(0, Direction.z);
        float3 Incident = Outgoing * float3(.6, .5, .4) / 3.141592653589793 * ReceiverCosine *
            (ConnectionVisible ? 1 : 0);
        float3 Integrand = Incident * max(0, Cosine) / Squared;
        float DirectionDensity = abs(Cosine) > 0 ? Density * Squared / abs(Cosine) : 0;
        float3 Contribution = Integrand / Density;
        AreaEstimate += Contribution / 8;
        if (Cosine > 0) DirectionEstimate += Incident / DirectionDensity / 8;
        WrongArea += dot(Integrand / Probability, float3(1, 1, 1)) / 24;
        if (Cosine > 0) WrongJacobian += dot(Incident / Density, float3(1, 1, 1)) / 24;
        uint Packed = Selected | (SuffixVisible ? 1u << 16 : 0) | (ConnectionVisible ? 1u << 17 : 0);
        OutputColour[(2 + 2 * Draw) * Count + Pixel] = float4(asfloat(Packed), U, V, Density);
        OutputColour[(3 + 2 * Draw) * Count + Pixel] = float4(Contribution, DirectionDensity);
    }
    OutputColour[Pixel] = float4(AreaEstimate, 1);
    OutputColour[Count + Pixel] = float4(DirectionEstimate, 1);
    OutputColour[18 * Count + Pixel] = float4(SuffixCalls, ReconnectCalls, WrongArea, WrongJacobian);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // 📝 Fresh-only estimator is complete in generation;the existing submission retains its two-dispatch ABI.
}

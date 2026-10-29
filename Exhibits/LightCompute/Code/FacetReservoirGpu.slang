/*============================================================================================================================================
                                                       FACETRESERVOIRGPU.SLANG
============================================================================================================================================*/
// 🧩 Validate unpacked RGB facet descriptors with production facet interpolation and positive support.
#include "LightInfluence.slang"
#include "FacetReservoirDescriptor.h"

bool EmptyDescriptor(FacetReservoirDescriptor D)
{
    return D.FacetIndex == 0xffffffffu && D.Revision == 0 &&
        all(asuint(float4(D.U, D.V, D.Outgoing.xy)) == 0) &&
        all(asuint(float2(D.Outgoing.z, D.Weight)) == 0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    SurfacePoint Input = Receivers[Pixel];
    FacetReservoirDescriptor D;
    D.FacetIndex = asuint(Input.PositionHit.x); D.Revision = asuint(Input.PositionHit.y);
    D.U = Input.PositionHit.z; D.V = Input.PositionHit.w;
    D.Outgoing = Input.NormalRoughness.xyz; D.Weight = Input.NormalRoughness.w;
    OutputColour[4 * Count + Pixel] = float4(asfloat(D.FacetIndex), asfloat(D.Revision), D.U, D.V);
    OutputColour[5 * Count + Pixel] = float4(D.Outgoing, D.Weight);
    bool Empty = EmptyDescriptor(D);
    bool Valid = !Empty && D.FacetIndex < Extent.Sampling.z && D.Revision == uint(Input.ViewMetallic.w) &&
        all(isfinite(float4(D.U, D.V, D.Outgoing.xy))) && all(isfinite(float2(D.Outgoing.z, D.Weight))) &&
        D.U >= 0 && D.V >= 0 && D.U + D.V <= 1 && all(D.Outgoing >= 0) && D.Weight > 0;
    OutputColour[Pixel] = float4(0, 0, 0, Empty ? 0 : Valid ? 1 : 2);
    OutputColour[Count + Pixel] = 0;
    OutputColour[2 * Count + Pixel] = 0;
    OutputColour[3 * Count + Pixel] = 0;
    if (!Valid) return;
    FacetCorners F = Facets[D.FacetIndex];
    float3 Point = F.A.xyz * (1 - D.U - D.V) + F.B.xyz * D.U + F.C.xyz * D.V;
    float3 Intrinsic = Unit(F.NormalA.xyz * (1 - D.U - D.V) + F.NormalB.xyz * D.U + F.NormalC.xyz * D.V);
    SurfacePoint Surface = FacetSurface(D.FacetIndex, Point, -Intrinsic, float3(0, D.U, D.V));
    float3 Delta = Input.ViewMetallic.xyz - Surface.PositionHit.xyz;
    float Squared = dot(Delta, Delta);
    float Geometry = max(0, dot(Surface.NormalRoughness.xyz, Unit(Delta))) / max(Squared, 1e-6);
    float Positive = 1.0 / 64 + Geometry;
    OutputColour[Pixel] = float4(D.Outgoing * D.Weight * Geometry, 1);
    OutputColour[Count + Pixel] = float4(Surface.PositionHit.xyz, 1);
    OutputColour[2 * Count + Pixel] = float4(Surface.NormalRoughness.xyz, Geometry);
    OutputColour[3 * Count + Pixel] = float4(Positive, Geometry, D.Weight, 1);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // Generation is the complete descriptor test;no second estimate or ray query is implied.
}

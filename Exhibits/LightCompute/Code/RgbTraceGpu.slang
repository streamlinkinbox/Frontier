/*============================================================================================================================================
                                                          RGBTRACEGPU.SLANG
============================================================================================================================================*/
// 🧩 Trace a directional RGB suffix into shared facet descriptors and independently query reconnection visibility.
#include "LightInfluence.slang"
#include "FacetReservoirDescriptor.h"
static uint SuffixCalls = 0, ReconnectCalls = 0;

SurfacePoint TracePoint(uint Index, float U, float V)
{
    FacetCorners Corners = Facets[Index];
    float3 Point = Corners.A.xyz * (1 - U - V) + Corners.B.xyz * U + Corners.C.xyz * V;
    float3 Normal = Unit(Corners.NormalA.xyz * (1 - U - V) + Corners.NormalB.xyz * U + Corners.NormalC.xyz * V);
    return FacetSurface(Index, Point, -Normal, float3(0, U, V));
}

float3 TraceSuffix(SurfacePoint Surface, float Scale, out bool Visible)
{
    ++SuffixCalls;
    float3 Strike;
    Visible = NearestFacet(Surface.PositionHit.xyz + Surface.NormalRoughness.xyz * .002,
                          float3(.8, 0, .6), .00001, 100, Strike) == 0xffffffffu;
    return Visible ? Surface.AlbedoIor.xyz * float3(1, .7, .4) * Scale *
        max(0, dot(Surface.NormalRoughness.xyz, float3(.8, 0, .6))) / 3.141592653589793 : float3(0);
}

bool TraceReconnect(SurfacePoint Surface, float3 Receiver)
{
    ++ReconnectCalls;
    float3 Delta = Receiver - Surface.PositionHit.xyz, Strike;
    float Distance = length(Delta);
    return NearestFacet(Surface.PositionHit.xyz + Surface.NormalRoughness.xyz * .002,
                        Delta / Distance, .00001, Distance - .004, Strike) == 0xffffffffu;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    SurfacePoint Input = Receivers[Pixel];
    uint Index = asuint(Input.PositionHit.x), Birth = asuint(Input.PositionHit.y), Current = asuint(Input.NormalRoughness.x);
    for (uint Slot = 0; Slot < 9; ++Slot) OutputColour[Slot * Count + Pixel] = 0;
    FacetReservoirDescriptor Descriptor;
    Descriptor.FacetIndex = Index; Descriptor.Revision = Birth;
    Descriptor.U = Input.PositionHit.z; Descriptor.V = Input.PositionHit.w;
    Descriptor.Outgoing = Birth == Current ? float3(0) : float3(9, 8, 7); Descriptor.Weight = 1;
    if (Index < Extent.Sampling.z && Birth == Current)
    {
        SurfacePoint Surface = TracePoint(Index, Descriptor.U, Descriptor.V);
        bool Visible;
        Descriptor.Outgoing = TraceSuffix(Surface, Input.NormalRoughness.y, Visible);
        OutputColour[6 * Count + Pixel] = float4(Descriptor.Outgoing, Visible ? 1 : 0);
        OutputColour[8 * Count + Pixel].x = float(SuffixCalls);
    }
    OutputColour[4 * Count + Pixel] = float4(asfloat(Index), asfloat(Birth), Descriptor.U, Descriptor.V);
    OutputColour[5 * Count + Pixel] = float4(Descriptor.Outgoing, Descriptor.Weight);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    float4 First = OutputColour[4 * Count + Pixel], Second = OutputColour[5 * Count + Pixel];
    uint Index = asuint(First.x), Birth = asuint(First.y);
    SurfacePoint Input = Receivers[Pixel];
    if (Index >= Extent.Sampling.z || Birth != asuint(Input.NormalRoughness.x)) return;
    SurfacePoint Surface = TracePoint(Index, First.z, First.w);
    float3 Delta = Input.ViewMetallic.xyz - Surface.PositionHit.xyz;
    float Geometry = max(0, dot(Surface.NormalRoughness.xyz, Unit(Delta))) / max(dot(Delta, Delta), 1e-6);
    bool CachedVisible = TraceReconnect(Surface, Input.ViewMetallic.xyz);
    uint CachedCalls = ReconnectCalls;
    bool SuffixVisible;
    float3 InlineRgb = TraceSuffix(Surface, Input.NormalRoughness.y, SuffixVisible);
    bool InlineVisible = TraceReconnect(Surface, Input.ViewMetallic.xyz);
    OutputColour[Pixel] = float4(Second.xyz * Geometry * (CachedVisible ? 1 : 0), CachedVisible ? 1 : 0);
    OutputColour[Count + Pixel] = float4(InlineRgb * Geometry * (InlineVisible ? 1 : 0), InlineVisible ? 1 : 0);
    OutputColour[2 * Count + Pixel] = float4(Second.xyz * Geometry, Geometry);
    OutputColour[3 * Count + Pixel] = float4(Surface.PositionHit.xyz, 1);
    OutputColour[7 * Count + Pixel] = float4(Surface.NormalRoughness.xyz, SuffixVisible ? 1 : 0);
    OutputColour[8 * Count + Pixel].yzw = float3(SuffixCalls, CachedCalls, ReconnectCalls - CachedCalls);
}

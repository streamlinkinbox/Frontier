/*============================================================================================================================================
                                                        FACETCAUSALGPU.SLANG
============================================================================================================================================*/
// 🧩 Reconstruct fixed selected proposals and deformed causal rays through shared Slang visibility functions.
#include "GeometricAreaReservoirGpu.slang"

[shader("compute")][numthreads(64, 1, 1)] void GenerateCausalPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count)
        return;
    for (uint Slab = 0; Slab < 16; ++Slab)
        OutputColour[Slab * Count + Pixel] = 0;
    SurfacePoint Input = Receivers[Pixel];
    float3 Strike;
    uint PrimaryIndex = NearestFacet(Input.ViewMetallic.xyz, Input.NormalRoughness.xyz, .00001, 100, Strike);
    if (PrimaryIndex == 0xffffffffu)
        return;
    SurfacePoint Primary = FacetSurface(PrimaryIndex, Input.ViewMetallic.xyz, Input.NormalRoughness.xyz, Strike);
    ReceiverNormal = Primary.NormalRoughness.xyz;
    ReceiverAlbedo = Primary.AlbedoIor.xyz;
    AreaCandidate Candidate = (AreaCandidate)0;
    Candidate.Index = asuint(Input.PositionHit.x);
    Candidate.U = Input.AlbedoIor.x;
    Candidate.V = Input.AlbedoIor.y;
    SurfacePoint Source = ReservoirAreaPoint(Candidate);
    bool SuffixVisible;
    float3 Outgoing = TraceSuffix(Source, 1, SuffixVisible);
    bool ConnectionVisible = TraceGeometricAreaConnection(Source, Primary.PositionHit.xyz);
    float3 Geometry = ReservoirGeometry(Candidate, Primary.PositionHit.xyz);
    float3 Proxy = Source.AlbedoIor.xyz * float3(1, .7, .4) *
        max(0, dot(Source.NormalRoughness.xyz, float3(.8, 0, .6))) / 3.141592653589793 * Geometry;
    float Weight = Input.AlbedoIor.z;
    float3 OmitConnection = Outgoing * Geometry * Weight;
    float3 OmitSuffix = Proxy * (ConnectionVisible ? 1 : 0) * Weight;
    float3 Correct = OmitConnection * (ConnectionVisible ? 1 : 0);
    uint Fault = asuint(Input.PositionHit.y);
    if (Fault == 1)
        Correct = OmitConnection;
    if (Fault == 2)
        Correct = OmitSuffix;
    float3 Origin = Source.PositionHit.xyz + Source.NormalRoughness.xyz * .002;
    float3 Delta = Primary.PositionHit.xyz + ReceiverNormal * .002 - Origin;
    float Distance = length(Delta);
    OutputColour[Pixel] = float4(Correct, 0);
    OutputColour[Count + Pixel] = float4(OmitConnection, 0);
    OutputColour[2 * Count + Pixel] = float4(OmitSuffix, 0);
    OutputColour[3 * Count + Pixel] = float4(Primary.PositionHit.xyz, 0);
    OutputColour[4 * Count + Pixel] = float4(ReceiverNormal, 0);
    OutputColour[5 * Count + Pixel] = float4(ReceiverAlbedo, 0);
    OutputColour[6 * Count + Pixel] = float4(asfloat(PrimaryIndex), Strike);
    OutputColour[7 * Count + Pixel] = float4(Source.PositionHit.xyz, 0);
    OutputColour[8 * Count + Pixel] = float4(Source.NormalRoughness.xyz, 0);
    OutputColour[9 * Count + Pixel] = float4(Source.AlbedoIor.xyz, 0);
    OutputColour[10 * Count + Pixel] = float4(Origin, Distance - .00001);
    OutputColour[11 * Count + Pixel] = float4(Delta / Distance, 0);
    OutputColour[12 * Count + Pixel] = float4(ConnectionVisible ? 1 : 0, SuffixVisible ? 1 : 0, SuffixCalls, ReconnectCalls);
    OutputColour[13 * Count + Pixel] = float4(asfloat(Candidate.Index), Candidate.U, Candidate.V, Weight);
}

[shader("compute")][numthreads(64, 1, 1)] void ResolveCausalPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // 📝 Preserve the existing two-dispatch submission ABI;the first dispatch writes the complete diagnostic.
}

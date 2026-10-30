/*============================================================================================================================================
                                                  FACETAREAOCCLUSIONGPU.SLANG
============================================================================================================================================*/
// 🧩 Trace sensitive source-restricted occlusion fixtures with shared Slang geometry and visibility.
#include "GeometricAreaReservoirGpu.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                              SOURCE-RESTRICTED PROPOSALS
//------------------------------------------------------------------------------------------------------------------------

AreaCandidate ProposeOcclusionCandidate(
    uint   Seed,
    uint   Draw,
    uint2  SourceIndices,
    float3 Receiver)
{
    float Choice = float(ReservoirAreaHash(Seed ^ (3 * Draw + 1) * 0x9e3779b9u) >> 8) / 16777216.0;
    float First  = float(ReservoirAreaHash(Seed ^ (3 * Draw + 2) * 0x9e3779b9u) >> 8) / 16777216.0;
    float Second = float(ReservoirAreaHash(Seed ^ (3 * Draw + 3) * 0x9e3779b9u) >> 8) / 16777216.0;
    AreaCandidate Candidate;
    Candidate.Index      = SourceIndices[min(uint(Choice * 2), 1u)];
    float Root           = sqrt(First);
    Candidate.U          = 1 - Root;
    Candidate.V          = Root * Second;
    Candidate.Density    = .5 / ReservoirFacetArea(Candidate.Index);
    SurfacePoint Surface = ReservoirAreaPoint(Candidate);
    float3 Proxy         = Surface.AlbedoIor.xyz * float3(1, .7, .4) / 3.141592653589793 *
                   max(0, dot(Surface.NormalRoughness.xyz, float3(.8, 0, .6))) * ReservoirGeometry(Candidate, Receiver);
    Candidate.Score = 1e-6 + dot(Proxy, float3(1, 1, 1)) / 3;
    return Candidate;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FRESH TRACE AND ABLATIONS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")][numthreads(64, 1, 1)] void GenerateOcclusionPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count)
        return;
    for (uint Slab = 0; Slab < 40; ++Slab)
        OutputColour[Slab * Count + Pixel] = 0;
    SurfacePoint Input = Receivers[Pixel];
    float3 Strike;
    uint PrimaryIndex = NearestFacet(Input.ViewMetallic.xyz, Input.NormalRoughness.xyz, .00001, 100, Strike);
    if (PrimaryIndex == 0xffffffffu)
        return;
    SurfacePoint Primary = FacetSurface(PrimaryIndex, Input.ViewMetallic.xyz, Input.NormalRoughness.xyz, Strike);
    ReceiverNormal       = Primary.NormalRoughness.xyz;
    ReceiverAlbedo       = Primary.AlbedoIor.xyz;
    float3 Receiver      = Primary.PositionHit.xyz;
    OutputColour[4 * Count + Pixel] = float4(Receiver, 0);
    OutputColour[5 * Count + Pixel] = float4(ReceiverNormal, 0);
    OutputColour[6 * Count + Pixel] = float4(ReceiverAlbedo, 0);
    OutputColour[7 * Count + Pixel] = float4(asfloat(PrimaryIndex), Strike);
    uint Seed = asuint(Input.PositionHit.x), Method = asuint(Input.PositionHit.y), Slot = 0;
    uint2 SourceIndices = asuint(Input.AlbedoIor.xy);
    float3 Correct = 0, OmitConnection = 0, OmitSuffix = 0, OmitBoth = 0;
    for (uint Lane = 0; Lane < 4; ++Lane)
    {
        uint CandidateCount = Method == 1 ? 1 : 2;
        AreaCandidate Candidates[2];
        Candidates[0] = ProposeOcclusionCandidate(Seed, 2 * Lane, SourceIndices, Receiver);
        Candidates[1] = (AreaCandidate)0;
        if (CandidateCount == 2)
            Candidates[1] = ProposeOcclusionCandidate(Seed, 2 * Lane + 1, SourceIndices, Receiver);
        for (uint Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            AreaCandidate Candidate = Candidates[Ordinal];
            OutputColour[(8 + 2 * Lane + Ordinal) * Count + Pixel] =
                float4(asfloat(Candidate.Index), Candidate.U, Candidate.V, Candidate.Density);
        }
        uint Selected    = 0;
        float Normalizer = Method == 1 ? 1 / Candidates[0].Density : 0;
        if (Method == 0)
        {
            float FirstWeight  = Candidates[0].Score / Candidates[0].Density;
            float SecondWeight = Candidates[1].Score / Candidates[1].Density;
            float Uniform      = float(ReservoirAreaHash(Seed ^ (0x7f4a7c15u + Lane * 0x9e3779b9u)) >> 8) / 16777216.0;
            Selected           = Uniform < FirstWeight / (FirstWeight + SecondWeight) ? 0 : 1;
            Normalizer         = (FirstWeight + SecondWeight) / (2 * Candidates[Selected].Score);
        }
        OutputColour[(16 + Lane) * Count + Pixel] =
            float4(Candidates[0].Score, Candidates[1].Score, Normalizer, Selected);
        for (uint Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            if (Method == 0 && Ordinal != Selected)
                continue;
            AreaCandidate Candidate = Candidates[Ordinal];
            SurfacePoint Surface    = ReservoirAreaPoint(Candidate);
            bool SuffixVisible;
            float3 Outgoing        = TraceSuffix(Surface, 1, SuffixVisible);
            bool ConnectionVisible = TraceGeometricAreaConnection(Surface, Receiver);
            float3 Origin          = Surface.PositionHit.xyz + Surface.NormalRoughness.xyz * .002;
            float3 Delta           = Receiver + ReceiverNormal * .002 - Origin;
            float Distance         = length(Delta);
            uint Packed = Candidate.Index | (SuffixVisible ? 1u << 16 : 0) | (ConnectionVisible ? 1u << 17 : 0);
            OutputColour[(20 + Slot) * Count + Pixel] = float4(Origin, asfloat(Packed));
            OutputColour[(28 + Slot) * Count + Pixel] = float4(Delta / Distance, Distance - .00001);
            ++Slot;
            float3 Geometry = ReservoirGeometry(Candidate, Receiver);
            float3 Proxy    = Surface.AlbedoIor.xyz * float3(1, .7, .4) / 3.141592653589793 *
                           max(0, dot(Surface.NormalRoughness.xyz, float3(.8, 0, .6))) * Geometry;
            float Weight = (Method == 0 ? Normalizer : 1 / Candidate.Density) * (Method == 2 ? .5 : 1) / 4;
            Correct += Outgoing * Geometry * (ConnectionVisible ? 1 : 0) * Weight;
            OmitConnection += Outgoing * Geometry * Weight;
            OmitSuffix += Proxy * (ConnectionVisible ? 1 : 0) * Weight;
            OmitBoth += Proxy * Weight;
        }
    }
    OutputColour[Pixel]              = float4(Correct, 0);
    OutputColour[Count + Pixel]      = float4(OmitConnection, 0);
    OutputColour[2 * Count + Pixel]  = float4(OmitSuffix, 0);
    OutputColour[3 * Count + Pixel]  = float4(OmitBoth, 0);
    OutputColour[36 * Count + Pixel] = float4(SuffixCalls, ReconnectCalls, Method == 1 ? 4 : 8, Slot);
}

    [shader("compute")][numthreads(64, 1, 1)] void ResolveOcclusionPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // 📝 Fresh results are complete;retain the existing two-dispatch submission ABI.
}

/*============================================================================================================================================
                                                         FACETPACKETGPU.SLANG
============================================================================================================================================*/
// 🧩 Generate full deformed causal reservoir packets without native proposals,selection or normalization inputs.
#include "GeometricAreaReservoirGpu.slang"

uint PacketSourceIndex(uint Ordinal)
{
    return asuint(Receivers[1 + Ordinal / 4].PositionHit[Ordinal % 4]);
}

AreaCandidate ProposePacketCandidate(
    uint Seed,
    uint Draw,
    uint Measure,
    float3 Receiver)
{
    float Choice = float(ReservoirAreaHash(Seed ^ (3 * Draw + 1) * 0x9e3779b9u) >> 8) / 16777216.0;
    float First = float(ReservoirAreaHash(Seed ^ (3 * Draw + 2) * 0x9e3779b9u) >> 8) / 16777216.0;
    float Second = float(ReservoirAreaHash(Seed ^ (3 * Draw + 3) * 0x9e3779b9u) >> 8) / 16777216.0;
    float Total = 0;
    for (uint Ordinal = 0; Ordinal < 16; ++Ordinal)
        Total += ReservoirFacetArea(PacketSourceIndex(Ordinal));
    uint Chosen = min(uint(Choice * 16), 15u);
    if (Measure == 1)
    {
        Chosen = 15;
        float Cumulative = 0;
        for (uint Ordinal = 0; Ordinal < 16; ++Ordinal)
        {
            Cumulative += ReservoirFacetArea(PacketSourceIndex(Ordinal)) / Total;
            if (Choice < Cumulative)
            {
                Chosen = Ordinal;
                break;
            }
        }
    }
    AreaCandidate Candidate = (AreaCandidate)0;
    Candidate.Index = PacketSourceIndex(Chosen);
    float Root = sqrt(First);
    Candidate.U = 1 - Root;
    Candidate.V = Root * Second;
    Candidate.Density = Measure == 0 ? 1.0 / 16 / ReservoirFacetArea(Candidate.Index) : 1.0 / Total;
    SurfacePoint Source = ReservoirAreaPoint(Candidate);
    float3 Proxy = Source.AlbedoIor.xyz * float3(1, .7, .4) / 3.141592653589793 *
        max(0, dot(Source.NormalRoughness.xyz, float3(.8, 0, .6))) * ReservoirGeometry(Candidate, Receiver);
    Candidate.Score = 1e-6 + ((Proxy.x + Proxy.y) + Proxy.z) / 3;
    return Candidate;
}

[shader("compute")][numthreads(64, 1, 1)] void GeneratePacketPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // 📝 Four additional receiver entries carry only the original source profile's index permutation.
    if (Dispatch.x != 0)
        return;
    uint Count = Extent.Dimensions.x;
    for (uint Slab = 0; Slab < 80; ++Slab)
        OutputColour[Slab * Count] = 0;
    SurfacePoint Input = Receivers[0];
    uint Seed = asuint(Input.PositionHit.x), Method = asuint(Input.PositionHit.y), Fault = asuint(Input.PositionHit.z);
    float3 Strike;
    uint PrimaryIndex = NearestFacet(Input.ViewMetallic.xyz, Input.NormalRoughness.xyz, .00001, 100, Strike);
    if (PrimaryIndex == 0xffffffffu)
        return;
    SurfacePoint Primary = FacetSurface(PrimaryIndex, Input.ViewMetallic.xyz, Input.NormalRoughness.xyz, Strike);
    ReceiverNormal = Primary.NormalRoughness.xyz;
    ReceiverAlbedo = Primary.AlbedoIor.xyz;
    float3 Receiver = Primary.PositionHit.xyz;
    OutputColour[4 * Count] = float4(Receiver, 0);
    OutputColour[5 * Count] = float4(ReceiverNormal, 0);
    OutputColour[6 * Count] = float4(ReceiverAlbedo, 0);
    OutputColour[7 * Count] = float4(asfloat(PrimaryIndex), Strike);
    uint Slot = 0, Draws = 0;
    float3 Correct = 0, OmitConnection = 0, OmitSuffix = 0, OmitBoth = 0;
    for (uint Lane = 0; Lane < 4; ++Lane)
    {
        uint CandidateCount = Method == 2 || Method == 3 ? 1 : 2;
        AreaCandidate Candidates[2];
        Candidates[0] = ProposePacketCandidate(Seed, 2 * Lane, Method % 2, Receiver);
        Candidates[1] = (AreaCandidate)0;
        if (CandidateCount == 2)
            Candidates[1] = ProposePacketCandidate(Seed, 2 * Lane + 1, Method % 2, Receiver);
        for (uint Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            AreaCandidate Candidate = Candidates[Ordinal];
            OutputColour[(8 + 2 * Lane + Ordinal) * Count] = float4(asfloat(Candidate.Index), Candidate.U, Candidate.V, Candidate.Density);
            ++Draws;
        }
        uint Selected = 0;
        float Normalizer = 0;
        if (Method < 2)
        {
            float FirstWeight = Candidates[0].Score / Candidates[0].Density;
            float SecondWeight = Candidates[1].Score / Candidates[1].Density;
            float Uniform = float(ReservoirAreaHash(Seed ^ (0x7f4a7c15u + Lane * 0x9e3779b9u)) >> 8) / 16777216.0;
            Selected = Uniform < FirstWeight / (FirstWeight + SecondWeight) ? 0 : 1;
            Normalizer = (FirstWeight + SecondWeight) / (2 * Candidates[Selected].Score);
        }
        OutputColour[(16 + Lane) * Count] = float4(Candidates[0].Score, Candidates[1].Score, Normalizer, Selected);
        for (uint Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            if (Method < 2 && Ordinal != Selected)
                continue;
            AreaCandidate Candidate = Candidates[Ordinal];
            SurfacePoint Source = ReservoirAreaPoint(Candidate);
            bool SuffixVisible;
            float3 Outgoing = TraceSuffix(Source, 1, SuffixVisible);
            bool ConnectionVisible = TraceGeometricAreaConnection(Source, Receiver);
            float3 Geometry = ReservoirGeometry(Candidate, Receiver);
            float3 Proxy = Source.AlbedoIor.xyz * float3(1, .7, .4) / 3.141592653589793 *
                max(0, dot(Source.NormalRoughness.xyz, float3(.8, 0, .6))) * Geometry;
            float Weight = (Method < 2 ? Normalizer : 1 / Candidate.Density) * (Method >= 4 ? .5 : 1) / 4;
            float3 WithoutConnection = Outgoing * Geometry * Weight;
            float3 WithoutSuffix = Proxy * (ConnectionVisible ? 1 : 0) * Weight;
            float3 Contribution = WithoutConnection * (ConnectionVisible ? 1 : 0);
            if (Fault == 1)
                Contribution = WithoutConnection;
            if (Fault == 2)
                Contribution = WithoutSuffix;
            Correct += Contribution;
            OmitConnection += WithoutConnection;
            OmitSuffix += WithoutSuffix;
            OmitBoth += Proxy * Weight;
            float3 Origin = Source.PositionHit.xyz + Source.NormalRoughness.xyz * .002;
            float3 Delta = Receiver + ReceiverNormal * .002 - Origin;
            float Distance = length(Delta);
            uint Packed = Candidate.Index | (ConnectionVisible ? 1u << 17 : 0) | (SuffixVisible ? 1u << 16 : 0);
            OutputColour[(20 + Slot) * Count] = float4(Contribution, 0);
            OutputColour[(28 + Slot) * Count] = float4(WithoutConnection, 0);
            OutputColour[(36 + Slot) * Count] = float4(WithoutSuffix, 0);
            OutputColour[(44 + Slot) * Count] = float4(Origin, Distance - .00001);
            OutputColour[(52 + Slot) * Count] = float4(Delta / Distance, asfloat(Packed));
            OutputColour[(61 + Slot) * Count] = float4(Proxy * Weight, 0);
            OutputColour[(69 + Slot) * Count] = float4(Lane, Ordinal, Weight, 0);
            ++Slot;
        }
    }
    OutputColour[0] = float4(Correct, 0);
    OutputColour[Count] = float4(OmitConnection, 0);
    OutputColour[2 * Count] = float4(OmitSuffix, 0);
    OutputColour[3 * Count] = float4(OmitBoth, 0);
    OutputColour[60 * Count] = float4(Draws, Slot, SuffixCalls, ReconnectCalls);
}

[shader("compute")][numthreads(64, 1, 1)] void ResolvePacketPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // 📝 Preserve the existing two-dispatch submission ABI.
}

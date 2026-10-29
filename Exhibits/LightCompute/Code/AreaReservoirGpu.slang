/*============================================================================================================================================
                                                        AREARESERVOIRGPU.SLANG
============================================================================================================================================*/
// 🧩 Select fresh continuous-area reservoirs with explicit proposal PDFs and trace only selected suffix/connections.
#define GenerateVertexPayload ReservoirReferenceGenerate
#define EvaluateVertexPayload ReservoirReferenceEvaluate
#include "RgbTraceGpu.slang"
#undef GenerateVertexPayload
#undef EvaluateVertexPayload

struct AreaCandidate
{
    uint  Index;
    float U;
    float V;
    float Density;
    float Score;
};

uint ReservoirAreaHash(uint Bits)
{
    Bits ^= Bits >> 16; Bits *= 0x7feb352du;
    Bits ^= Bits >> 15; Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

float ReservoirFacetArea(uint Index)
{
    return .5 * length(cross(Facets[Index].B.xyz - Facets[Index].A.xyz,
                            Facets[Index].C.xyz - Facets[Index].A.xyz));
}

SurfacePoint ReservoirAreaPoint(AreaCandidate Candidate)
{
    FacetCorners Facet = Facets[Candidate.Index];
    float3 Point = Facet.A.xyz + Candidate.U * (Facet.B.xyz - Facet.A.xyz) +
        Candidate.V * (Facet.C.xyz - Facet.A.xyz);
    return FacetSurface(Candidate.Index, Point, -Facet.NormalA.xyz, float3(0, Candidate.U, Candidate.V));
}

float3 ReservoirGeometry(SurfacePoint Surface, float3 Receiver)
{
    float3 Delta = Receiver - Surface.PositionHit.xyz;
    float Squared = dot(Delta, Delta);
    float3 Direction = Delta / sqrt(Squared);
    return float3(.6, .5, .4) / 3.141592653589793 * max(0, Direction.z) *
        max(0, dot(Surface.NormalRoughness.xyz, Direction)) / Squared;
}

AreaCandidate ProposeAreaCandidate(
    uint   Seed,
    uint   Draw,
    uint   ProposalMeasure,
    float3 Receiver)
{
    uint Count = Extent.Sampling.z;
    float Choice = float(ReservoirAreaHash(Seed ^ (3 * Draw + 1) * 0x9e3779b9u) >> 8) / 16777216.0;
    float First = float(ReservoirAreaHash(Seed ^ (3 * Draw + 2) * 0x9e3779b9u) >> 8) / 16777216.0;
    float Second = float(ReservoirAreaHash(Seed ^ (3 * Draw + 3) * 0x9e3779b9u) >> 8) / 16777216.0;
    float Total = 0;
    for (uint Index = 0; Index < Count; ++Index) Total += ReservoirFacetArea(Index);
    AreaCandidate Candidate;
    Candidate.Index = min(uint(Choice * Count), Count - 1);
    if (ProposalMeasure == 1)
    {
        float Cumulative = 0;
        Candidate.Index = Count - 1;
        for (uint Index = 0; Index < Count; ++Index)
        {
            Cumulative += ReservoirFacetArea(Index) / Total;
            if (Choice < Cumulative) { Candidate.Index = Index; break; }
        }
    }
    float Root = sqrt(First);
    Candidate.U = 1 - Root; Candidate.V = Root * Second;
    Candidate.Density = ProposalMeasure == 0 ? 1.0 / Count / ReservoirFacetArea(Candidate.Index) : 1.0 / Total;
    SurfacePoint Surface = ReservoirAreaPoint(Candidate);
    float3 Unoccluded = Surface.AlbedoIor.xyz * float3(1, .7, .4) / 3.141592653589793 *
        max(0, dot(Surface.NormalRoughness.xyz, float3(.8, 0, .6))) * ReservoirGeometry(Surface, Receiver);
    Candidate.Score = 1e-6 + dot(Unoccluded, float3(1, 1, 1)) / 3;
    return Candidate;
}

float3 TraceAreaCandidate(AreaCandidate Candidate, float3 Receiver, out uint Packed)
{
    SurfacePoint Surface = ReservoirAreaPoint(Candidate);
    bool SuffixVisible;
    float3 Outgoing = TraceSuffix(Surface, 1, SuffixVisible);
    bool Visible = TraceReconnect(Surface, Receiver);
    Packed = Candidate.Index | (1u << 18) | (SuffixVisible ? 1u << 16 : 0) | (Visible ? 1u << 17 : 0);
    return Outgoing * ReservoirGeometry(Surface, Receiver) * (Visible ? 1 : 0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    SurfacePoint Input = Receivers[Pixel];
    uint Seed = asuint(Input.PositionHit.x), Method = asuint(Input.PositionHit.y);
    float3 Receiver = Input.ViewMetallic.xyz, Estimate = 0, Negative = 0;
    for (uint Lane = 0; Lane < 4; ++Lane)
    {
        AreaCandidate First = ProposeAreaCandidate(Seed, 2 * Lane, Method % 2, Receiver);
        AreaCandidate Second = (AreaCandidate)0;
        Second.Index = 0xffffffffu;
        if (Method < 2 || Method >= 4) Second = ProposeAreaCandidate(Seed, 2 * Lane + 1, Method % 2, Receiver);
        uint FirstPacked = First.Index, SecondPacked = Second.Index;
        float3 Contribution = 0, Wrong = 0;
        float Normalizer = 0, Uniform = 0;
        uint Selected = 0;
        if (Method < 2)
        {
            float FirstWeight = First.Score / First.Density, SecondWeight = Second.Score / Second.Density;
            Uniform = float(ReservoirAreaHash(Seed ^ (0x7f4a7c15u + Lane * 0x9e3779b9u)) >> 8) / 16777216.0;
            Selected = Uniform < FirstWeight / (FirstWeight + SecondWeight) ? 0 : 1;
            AreaCandidate Chosen = Selected == 0 ? First : Second;
            uint Packed;
            float3 Integrand = TraceAreaCandidate(Chosen, Receiver, Packed);
            if (Selected == 0) FirstPacked = Packed; else SecondPacked = Packed;
            Normalizer = (FirstWeight + SecondWeight) / (2 * Chosen.Score);
            Contribution = Integrand * Normalizer;
            Wrong = Integrand * (First.Score + Second.Score) / (2 * Chosen.Score);
        }
        else
        {
            float3 Integrand = TraceAreaCandidate(First, Receiver, FirstPacked);
            Normalizer = 1 / First.Density;
            Contribution = Integrand * Normalizer;
            Wrong = Integrand;
            if (Method >= 4)
            {
                float3 Other = TraceAreaCandidate(Second, Receiver, SecondPacked);
                Contribution = .5 * (Contribution + Other / Second.Density);
                Wrong = .5 * (Wrong + Other);
                Normalizer = 0;
            }
        }
        Estimate += Contribution / 4; Negative += Wrong / 4;
        OutputColour[(2 + 4 * Lane) * Count + Pixel] = float4(asfloat(FirstPacked), First.U, First.V, First.Density);
        OutputColour[(3 + 4 * Lane) * Count + Pixel] = float4(asfloat(SecondPacked), Second.U, Second.V, Second.Density);
        OutputColour[(4 + 4 * Lane) * Count + Pixel] = float4(Contribution, Normalizer);
        OutputColour[(5 + 4 * Lane) * Count + Pixel] = float4(First.Score, Second.Score, Selected, Uniform);
    }
    OutputColour[Pixel] = float4(Estimate, 1);
    OutputColour[Count + Pixel] = float4(Negative, 1);
    OutputColour[18 * Count + Pixel] = float4(SuffixCalls, ReconnectCalls, Method < 2 || Method >= 4 ? 8 : 4, 0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // 📝 Fresh estimator is complete;preserve the existing two-dispatch submission ABI.
}

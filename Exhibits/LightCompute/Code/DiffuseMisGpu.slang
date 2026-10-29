/*============================================================================================================================================
                                                        DIFFUSEMISGPU.SLANG
============================================================================================================================================*/
// 🧩 Evaluate fresh diffuse BSDF/area proposals,first-hit Jacobians and balance MIS with measured query counts.
#define GenerateVertexPayload ReservoirReferenceGenerate
#define EvaluateVertexPayload ReservoirReferenceEvaluate
#include "RgbTraceGpu.slang"
#undef GenerateVertexPayload
#undef EvaluateVertexPayload

struct AreaCandidate
{
    uint Index;
    float U;
    float V;
    float Density;
    float Score;
};

uint ReservoirAreaHash(uint Bits)
{
    Bits ^= Bits >> 16;
    Bits *= 0x7feb352du;
    Bits ^= Bits >> 15;
    Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

float ReservoirFacetArea(uint Index)
{
    return .5 * length(cross(Facets[Index].B.xyz - Facets[Index].A.xyz, Facets[Index].C.xyz - Facets[Index].A.xyz));
}

SurfacePoint ReservoirAreaPoint(AreaCandidate Candidate)
{
    FacetCorners Facet = Facets[Candidate.Index];
    float3 Point = Facet.A.xyz + Candidate.U * (Facet.B.xyz - Facet.A.xyz) + Candidate.V * (Facet.C.xyz - Facet.A.xyz);
    return FacetSurface(Candidate.Index, Point, -Facet.NormalA.xyz, float3(0, Candidate.U, Candidate.V));
}

static float3 ReceiverNormal;
static float3 ReceiverAlbedo;

// 📝 Error-free subtraction retains the low word before cancellation near a proposal surface.
float3 SubtractAreaCoordinates(float3 First, float3 Second, out float3 Remainder)
{
    float3 Difference = First - Second;
    float3 Virtual = First - Difference;
    Remainder = (First - (Difference + Virtual)) + (Virtual - Second);
    return Difference;
}

// 📝 Split products do not assume the device implements GLSL Fma with a single rounding.
float3 AreaProductRemainder(float3 First, float3 Second, float3 Product)
{
    float3 SplitFirst = 4097 * First;
    float3 FirstHigh = SplitFirst - (SplitFirst - First);
    float3 FirstLow = First - FirstHigh;
    float3 SplitSecond = 4097 * Second;
    float3 SecondHigh = SplitSecond - (SplitSecond - Second);
    float3 SecondLow = Second - SecondHigh;
    return ((FirstHigh * SecondHigh - Product) + FirstHigh * SecondLow + FirstLow * SecondHigh) + FirstLow * SecondLow;
}

float ReceiverAreaCosine(float3 Delta, float3 DeltaLow)
{
    float3 Product = -ReceiverNormal * Delta;
    float3 Remainder = AreaProductRemainder(-ReceiverNormal, Delta, Product);
    float Sum = Product.x + Product.y;
    float Virtual = Sum - Product.x;
    float Error = (Product.x - (Sum - Virtual)) + (Product.y - Virtual);
    float Total = Sum + Product.z;
    Virtual = Total - Sum;
    Error += (Sum - (Total - Virtual)) + (Product.z - Virtual);
    return max(0, Total + (Error + Remainder.x + Remainder.y + Remainder.z + dot(-ReceiverNormal, DeltaLow)));
}

float3 EvaluateMisDelta(AreaCandidate Candidate, float3 Receiver, out float3 DeltaLow)
{
    FacetCorners Facet = Facets[Candidate.Index];
    float3 OriginError, FirstError, SecondError, SumError, DeltaError;
    float3 Origin = SubtractAreaCoordinates(Receiver, Facet.A.xyz, OriginError);
    float3 FirstEdge = SubtractAreaCoordinates(Facet.B.xyz, Facet.A.xyz, FirstError);
    float3 SecondEdge = SubtractAreaCoordinates(Facet.C.xyz, Facet.A.xyz, SecondError);
    float3 First = Candidate.U * FirstEdge;
    float3 Second = Candidate.V * SecondEdge;
    float3 FirstResidual = AreaProductRemainder(float3(Candidate.U), FirstEdge, First) + Candidate.U * FirstError;
    float3 SecondResidual = AreaProductRemainder(float3(Candidate.V), SecondEdge, Second) + Candidate.V * SecondError;
    float3 Sum = SubtractAreaCoordinates(Origin, First, SumError);
    float3 Delta = SubtractAreaCoordinates(Sum, Second, DeltaError);
    DeltaLow = OriginError + SumError + DeltaError - FirstResidual - SecondResidual;
    return Delta;
}

float3 ReservoirGeometry(AreaCandidate Candidate, float3 Receiver)
{
    float3 DeltaLow;
    float3 Delta = EvaluateMisDelta(Candidate, Receiver, DeltaLow);
    float ReceiverCosine = ReceiverAreaCosine(Delta, DeltaLow);
    Delta += DeltaLow;
    float Squared = dot(Delta, Delta);
    SurfacePoint Surface = ReservoirAreaPoint(Candidate);
    float SourceCosine = max(0, dot(Surface.NormalRoughness.xyz, Delta));
    return ReceiverAlbedo / 3.141592653589793 * ReceiverCosine * SourceCosine / (Squared * Squared);
}

static uint MisProposalCalls = 0;

uint TraceMisProposal(float3 Origin, float3 Direction, out float3 Strike)
{
    ++MisProposalCalls;
    return NearestFacet(Origin, Direction, .00001, 100, Strike);
}

float MisRandom(uint Seed, uint Draw) { return float(ReservoirAreaHash(Seed ^ Draw * 0x9e3779b9u) >> 8) / 16777216.0; }

// 📝 Range reduction bounds the polynomial argument by pi/2;avoid device-dependent trigonometric approximations.
void EvaluateMisAzimuth(float Angle, out float Sine, out float Cosine)
{
    float Reduced = Angle > 3.141592653589793 ? Angle - 6.283185307179586 : Angle;
    float Sign = 1;
    if (Reduced > 1.5707963267948966)
    {
        Reduced = 3.141592653589793 - Reduced;
        Sign = -1;
    }
    else if (Reduced < -1.5707963267948966)
    {
        Reduced = -3.141592653589793 - Reduced;
        Sign = -1;
    }
    float Squared = Reduced * Reduced;
    Sine =
        Reduced *
        (1 + Squared *
                 (-1.0 / 6 +
                  Squared * (1.0 / 120 +
                             Squared * (-1.0 / 5040 + Squared * (1.0 / 362880 + Squared * (-1.0 / 39916800 +
                                                                                           Squared / 6227020800.0))))));
    Cosine =
        Sign *
        (1 + Squared * (-1.0 / 2 +
                        Squared * (1.0 / 24 + Squared * (-1.0 / 720 + Squared * (1.0 / 40320 +
                                                                                 Squared * (-1.0 / 3628800 +
                                                                                            Squared / 479001600.0))))));
}

[shader("compute")][numthreads(64, 1, 1)] void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    for (uint Slab = 0; Slab < 23; ++Slab)
        OutputColour[Slab * Count + Pixel] = 0;
    SurfacePoint Input = Receivers[Pixel];
    uint Seed = asuint(Input.PositionHit.x), Method = asuint(Input.PositionHit.y);
    float3 PrimaryStrike;
    uint PrimaryIndex = NearestFacet(Input.ViewMetallic.xyz, float3(0, 0, 1), .00001, 100, PrimaryStrike);
    if (PrimaryIndex == 0xffffffffu) return;
    SurfacePoint Primary = FacetSurface(PrimaryIndex, Input.ViewMetallic.xyz, float3(0, 0, 1), PrimaryStrike);
    ReceiverNormal = Primary.NormalRoughness.xyz;
    ReceiverAlbedo = Primary.AlbedoIor.xyz;
    OutputColour[19 * Count + Pixel] = float4(asfloat(PrimaryIndex), PrimaryStrike);
    OutputColour[20 * Count + Pixel] = float4(Primary.PositionHit.xyz, 1);
    OutputColour[21 * Count + Pixel] = float4(ReceiverNormal, 0);
    OutputColour[22 * Count + Pixel] = float4(ReceiverAlbedo, 0);
    float3 Origin = Primary.PositionHit.xyz + ReceiverNormal * .002;
    bool Bsdf = Method == 2 || (Method == 3 && MisRandom(Seed, 7) < .5);
    float Total = 0;
    for (uint Index = 0; Index < Extent.Sampling.z; ++Index)
        Total += ReservoirFacetArea(Index);
    AreaCandidate Candidate = (AreaCandidate)0;
    Candidate.Index = 0xffffffffu;
    float3 Direction, Strike;
    uint Nearest;
    bool Visible;
    bool ProposalQueried = true;
    if (Bsdf)
    {
        float First = MisRandom(Seed, 2), Second = MisRandom(Seed, 3);
        float3 Tangent =
            normalize(cross(abs(ReceiverNormal.z) > .999 ? float3(0, 1, 0) : float3(0, 0, 1), ReceiverNormal));
        float3 Lateral = cross(ReceiverNormal, Tangent);
        float Sine, Cosine;
        EvaluateMisAzimuth(6.283185307179586 * Second, Sine, Cosine);
        Direction = sqrt(First) * (Cosine * Tangent + Sine * Lateral) + sqrt(1 - First) * ReceiverNormal;
        Nearest = TraceMisProposal(Origin, Direction, Strike);
        Candidate.Index = Nearest;
        Candidate.U = Nearest == 0xffffffffu ? 0 : Strike.y;
        Candidate.V = Nearest == 0xffffffffu ? 0 : Strike.z;
        Visible = Nearest != 0xffffffffu;
    }
    else
    {
        float Choice = MisRandom(Seed, 1), Root = sqrt(MisRandom(Seed, 2));
        Candidate.Index = min(uint(Choice * Extent.Sampling.z), Extent.Sampling.z - 1);
        if (Method != 0)
        {
            float Cumulative = 0;
            Candidate.Index = Extent.Sampling.z - 1;
            for (uint Index = 0; Index < Extent.Sampling.z; ++Index)
            {
                Cumulative += ReservoirFacetArea(Index) / Total;
                if (Choice < Cumulative)
                {
                    Candidate.Index = Index;
                    break;
                }
            }
        }
        Candidate.U = 1 - Root;
        Candidate.V = Root * MisRandom(Seed, 3);
        SurfacePoint Source = ReservoirAreaPoint(Candidate);
        float3 DeltaLow;
        float3 Delta = -EvaluateMisDelta(Candidate, Origin, DeltaLow);
        Delta -= DeltaLow;
        float Distance = length(Delta);
        Direction = Delta / Distance;
        ProposalQueried = dot(ReceiverNormal, Direction) > 0;
        Strike = float3(100, 0, 0);
        Nearest = 0xffffffffu;
        if (ProposalQueried) Nearest = TraceMisProposal(Origin, Direction, Strike);
        // 📝 Outside the receiver hemisphere both the integrand and BSDF density are exactly zero.
        Visible = dot(ReceiverNormal, Direction) > 0 && Nearest == Candidate.Index &&
                  abs(Strike.x - Distance) <= 2e-5 * max(1, Distance);
    }
    float DensityA = 0, DensityB = 0, Density = 0, Omega = 0, WrongDensity = 0;
    float3 Estimate = 0, Negative = 0, SourceNormal = 0;
    bool SuffixVisible = false, Positive = false;
    float LightCosine = 0;
    if (Candidate.Index != 0xffffffffu)
    {
        SurfacePoint Source = ReservoirAreaPoint(Candidate);
        SourceNormal = Source.NormalRoughness.xyz;
        float3 DeltaLow;
        float3 Delta = EvaluateMisDelta(Candidate, Origin, DeltaLow);
        float ReceiverCosine = ReceiverAreaCosine(Delta, DeltaLow);
        Delta += DeltaLow;
        float Squared = dot(Delta, Delta);
        float3 Incoming = -Delta / sqrt(Squared);
        Omega = Visible ? ReceiverCosine / sqrt(Squared) / 3.141592653589793 : 0;
        DensityA = Method == 0 ? 1.0 / Extent.Sampling.z / ReservoirFacetArea(Candidate.Index) : 1.0 / Total;
        DensityB = Omega * abs(dot(SourceNormal, -Incoming)) / Squared;
        Density = Method < 2 ? DensityA : Method == 2 ? DensityB : .5 * (DensityA + DensityB);
        WrongDensity = Method < 2 ? DensityA : Method == 2 ? Omega : .5 * (DensityA + Omega);
        LightCosine = max(0, dot(SourceNormal, float3(.8, 0, .6)));
        Positive = Visible && Omega > 0 && dot(SourceNormal, -Incoming) > 0 && LightCosine > 0;
        if (Positive)
        {
            float3 Outgoing = TraceSuffix(Source, 1, SuffixVisible);
            float3 Integrand = Outgoing * ReservoirGeometry(Candidate, Origin);
            Estimate = Integrand / Density;
            Negative = Integrand / WrongDensity;
        }
    }
    OutputColour[Pixel] = float4(Estimate, 1);
    OutputColour[Count + Pixel] = float4(DensityA, DensityB, Density, Bsdf ? 1 : 0);
    OutputColour[2 * Count + Pixel] = float4(asfloat(Candidate.Index), Candidate.U, Candidate.V, Visible ? 1 : 0);
    OutputColour[3 * Count + Pixel] = float4(ProposalQueried ? Direction : float3(0), Strike.x);
    OutputColour[4 * Count + Pixel] = float4(SourceNormal, asfloat(Nearest));
    OutputColour[5 * Count + Pixel] = float4(Omega, WrongDensity, SuffixVisible ? 1 : 0, Positive ? 1 : 0);
    OutputColour[6 * Count + Pixel] = float4(Negative, 0);
    OutputColour[7 * Count + Pixel] = float4(Origin, LightCosine);
    OutputColour[18 * Count + Pixel] = float4(1, MisProposalCalls, SuffixCalls, 1 + MisProposalCalls + SuffixCalls);
}

    [shader("compute")][numthreads(64, 1, 1)] void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    // 📝 The fresh single-sample estimator is complete in the first dispatch.
}

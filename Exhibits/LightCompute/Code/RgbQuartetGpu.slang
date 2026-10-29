/*============================================================================================================================================
                                                         RGBQUARTETGPU.SLANG
============================================================================================================================================*/
// 🧩 Select four RGB facet reservoirs from eight proposals and reconnect shared descriptors at a new receiver.
#include "LightInfluence.slang"
#include "FacetReservoirDescriptor.h"

//------------------------------------------------------------------------------------------------------------------------
//                                              FINITE PROPOSALS AND RECONNECTION
//------------------------------------------------------------------------------------------------------------------------
uint QuartetHash(uint Bits)
{
    Bits ^= Bits >> 16; Bits *= 0x7feb352du;
    Bits ^= Bits >> 15; Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

float3 QuartetReceiver(uint Index)
{
    if (Index == 0) return float3(0, 0, 4);
    if (Index == 1) return float3(0, 0, -3);
    if (Index == 2) return float3(4, -2, 1);
    return float3(-3, 3, 2);
}

float3 QuartetRgb(uint FacetIndex)
{
    return float3(1 + FacetIndex % 7, 1 + FacetIndex % 5, 1 + FacetIndex % 3) / float3(8, 6, 4);
}

float QuartetGeometry(uint FacetIndex, float3 Receiver, float U = .2, float V = .3)
{
    FacetCorners Corners = Facets[FacetIndex];
    float3 Point = Corners.A.xyz * (1 - U - V) + Corners.B.xyz * U + Corners.C.xyz * V;
    float3 Normal = Unit(Corners.NormalA.xyz * (1 - U - V) + Corners.NormalB.xyz * U + Corners.NormalC.xyz * V);
    SurfacePoint Surface = FacetSurface(FacetIndex, Point, -Normal, float3(0, U, V));
    float3 Delta = Receiver - Surface.PositionHit.xyz;
    return max(0, dot(Surface.NormalRoughness.xyz, Unit(Delta))) / max(dot(Delta, Delta), 1e-6);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FOUR TWO-PROPOSAL RESERVOIRS
//------------------------------------------------------------------------------------------------------------------------
[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    SurfacePoint Input = Receivers[Pixel];
    uint Seed = asuint(Input.PositionHit.x), Scenario = asuint(Input.PositionHit.y);
    uint BirthIndex = asuint(Input.PositionHit.z), CurrentIndex = asuint(Input.PositionHit.w);
    uint BirthRevision = asuint(Input.NormalRoughness.x), CurrentRevision = asuint(Input.NormalRoughness.y);
    float3 Birth = QuartetReceiver(BirthIndex), Current = QuartetReceiver(CurrentIndex);
    float3 Estimate = 0, Raw = 0, ZeroSupport = 0, ValidDivisor = 0, CurrentDenominator = 0;
    uint Nulls = 0, Empty = 0;
    for (uint ReservoirIndex = 0; ReservoirIndex < 4; ++ReservoirIndex)
    {
        uint First = QuartetHash(Seed ^ (3 * ReservoirIndex + 1) * 0x9e3779b9u) % 9;
        uint Second = QuartetHash(Seed ^ (3 * ReservoirIndex + 2) * 0x9e3779b9u) % 9;
        if (Scenario == 18) { First = 8; Second = 8; }
        if (Scenario == 19) { First = ReservoirIndex == 0 ? 0 : 8; Second = 8; }
        float Uniform = float(QuartetHash(Seed ^ (3 * ReservoirIndex + 3) * 0x9e3779b9u) >> 8) / 16777216.0;
        float FirstGeometry = 0, SecondGeometry = 0;
        if (First < 8) FirstGeometry = QuartetGeometry(First, Birth);
        if (Second < 8) SecondGeometry = QuartetGeometry(Second, Birth);
        float FirstTarget = First < 8 ? 1.0 / 64 + FirstGeometry : 0;
        float SecondTarget = Second < 8 ? 1.0 / 64 + SecondGeometry : 0;
        float Sum = 9 * (FirstTarget + SecondTarget);
        uint Selected = Sum > 0 && Uniform < FirstTarget / (FirstTarget + SecondTarget) ? First : Second;
        FacetReservoirDescriptor Descriptor;
        Descriptor.FacetIndex = Sum > 0 ? Selected : 0xffffffffu;
        Descriptor.Revision = Sum > 0 ? BirthRevision : 0;
        Descriptor.U = Sum > 0 ? .2 : 0; Descriptor.V = Sum > 0 ? .3 : 0;
        Descriptor.Outgoing = Sum > 0 ? QuartetRgb(Selected) : float3(0);
        float SelectedTarget = Selected == First ? FirstTarget : SecondTarget;
        Descriptor.Weight = Sum > 0 ? Sum / (2 * SelectedTarget) : 0;
        OutputColour[(2 + 2 * ReservoirIndex) * Count + Pixel] =
            float4(asfloat(Descriptor.FacetIndex), asfloat(Descriptor.Revision), Descriptor.U, Descriptor.V);
        OutputColour[(3 + 2 * ReservoirIndex) * Count + Pixel] = float4(Descriptor.Outgoing, Descriptor.Weight);
        bool Valid = Sum > 0 && Descriptor.Revision == CurrentRevision;
        float3 Contribution = 0;
        float CurrentGeometry = 0;
        if (Valid)
        {
            CurrentGeometry = QuartetGeometry(Selected, Current);
            Contribution = Descriptor.Outgoing * Descriptor.Weight * CurrentGeometry;
        }
        Estimate += Contribution / 4;
        uint ValidCount = uint(First < 8) + uint(Second < 8);
        ValidDivisor += Contribution * (ValidCount > 0 ? 2.0 / ValidCount : 0) / 4;
        if (Valid) CurrentDenominator += Contribution * SelectedTarget / (1.0 / 64 + CurrentGeometry) / 4;
        float ZeroSum = FirstGeometry + SecondGeometry;
        uint ZeroSelected = ZeroSum > 0 && Uniform < FirstGeometry / ZeroSum ? First : Second;
        float ZeroTarget = ZeroSelected == First ? FirstGeometry : SecondGeometry;
        if (ZeroSum > 0 && BirthRevision == CurrentRevision)
            ZeroSupport += QuartetRgb(ZeroSelected) * QuartetGeometry(ZeroSelected, Current) * 9 * ZeroSum / (2 * ZeroTarget) / 4;
        if (BirthRevision == CurrentRevision)
        {
            if (First < 8) Raw += QuartetRgb(First) * QuartetGeometry(First, Current) * 9 / 8;
            if (Second < 8) Raw += QuartetRgb(Second) * QuartetGeometry(Second, Current) * 9 / 8;
        }
        Nulls += 2 - ValidCount; Empty += uint(Sum == 0);
    }
    OutputColour[Pixel] = float4(Estimate, 1);
    OutputColour[Count + Pixel] = float4(Raw, 1);
    OutputColour[10 * Count + Pixel] = float4(ZeroSupport, 1);
    OutputColour[11 * Count + Pixel] = float4(ValidDivisor, 1);
    OutputColour[12 * Count + Pixel] = float4(CurrentDenominator, 1);
    OutputColour[13 * Count + Pixel] = float4(Nulls, Empty, BirthRevision == CurrentRevision ? 1 : 0, Scenario);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    uint CurrentRevision = asuint(Receivers[Pixel].NormalRoughness.y);
    float3 Current = QuartetReceiver(asuint(Receivers[Pixel].PositionHit.w));
    float3 Estimate = 0;
    for (uint ReservoirIndex = 0; ReservoirIndex < 4; ++ReservoirIndex)
    {
        float4 First = OutputColour[(2 + 2 * ReservoirIndex) * Count + Pixel];
        float4 Second = OutputColour[(3 + 2 * ReservoirIndex) * Count + Pixel];
        FacetReservoirDescriptor Descriptor;
        Descriptor.FacetIndex = asuint(First.x); Descriptor.Revision = asuint(First.y);
        Descriptor.U = First.z; Descriptor.V = First.w;
        Descriptor.Outgoing = Second.xyz; Descriptor.Weight = Second.w;
        bool Valid = Descriptor.FacetIndex < 8 && Descriptor.Revision == CurrentRevision &&
            all(isfinite(float4(Descriptor.U, Descriptor.V, Descriptor.Weight, 0))) &&
            all(isfinite(Descriptor.Outgoing)) && all(Descriptor.Outgoing >= 0) &&
            Descriptor.U >= 0 && Descriptor.V >= 0 && Descriptor.U + Descriptor.V <= 1 && Descriptor.Weight > 0;
        if (Valid)
            Estimate += Descriptor.Outgoing * Descriptor.Weight *
                QuartetGeometry(Descriptor.FacetIndex, Current, Descriptor.U, Descriptor.V) / 4;
    }
    OutputColour[Pixel] = float4(Estimate, 1);
}

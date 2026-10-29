/*============================================================================================================================================
                                                       TRACEDQUARTETGPU.SLANG
============================================================================================================================================*/
// 🧩 Integrate selected ray-tested RGB suffixes,current visibility and persistent quartet refresh.
#ifndef QUARTET_QUERY_ELISION
#define QUARTET_QUERY_ELISION 0
#endif

#define GenerateVertexPayload TraceReferenceGenerate
#define EvaluateVertexPayload TraceReferenceEvaluate
#include "RgbTraceGpu.slang"
#undef GenerateVertexPayload
#undef EvaluateVertexPayload
uint QuartetHash(uint Bits)
{
    Bits ^= Bits >> 16; Bits *= 0x7feb352du;
    Bits ^= Bits >> 15; Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}


float QuartetGeometry(uint Index, float3 Receiver, float U = .2, float V = .3)
{
    SurfacePoint Surface = TracePoint(Index, U, V);
    float3 Delta = Receiver - Surface.PositionHit.xyz;
    return max(0, dot(Surface.NormalRoughness.xyz, Unit(Delta))) / max(dot(Delta, Delta), 1e-6);
}
#if QUARTET_QUERY_ELISION
// 📝 Invocation-private tables:never survive dispatches or epochs;all proposal points share exact UV.
static uint   SuffixMask     = 0;
static uint   ConnectionMask = 0;
static float3 SuffixRgb[8];
static float  ConnectionFactors[8];
#endif

float3 SelectedSuffix(uint Index, float Scale)
{
#if QUARTET_QUERY_ELISION
    if ((SuffixMask & (1u << Index)) != 0) return SuffixRgb[Index];
#endif
    bool Visible;
    float3 Outgoing = TraceSuffix(TracePoint(Index, .2, .3), Scale, Visible);
#if QUARTET_QUERY_ELISION
    SuffixMask |= 1u << Index;
    SuffixRgb[Index] = Outgoing;
#endif
    return Outgoing;
}
float ConnectedGeometry(uint Index, float3 Receiver, float U = .2, float V = .3)
{
#if QUARTET_QUERY_ELISION
    float Geometry = QuartetGeometry(Index, Receiver, U, V);
    if (Geometry == 0) return 0;
    bool Canonical = U == .2 && V == .3;
    if (Canonical && (ConnectionMask & (1u << Index)) != 0) return ConnectionFactors[Index];
#endif
    bool Visible = TraceReconnect(TracePoint(Index, U, V), Receiver);
    float Connected = QuartetGeometry(Index, Receiver, U, V) * (Visible ? 1 : 0);
#if QUARTET_QUERY_ELISION
    if (Canonical)
    {
        ConnectionMask |= 1u << Index;
        ConnectionFactors[Index] = Connected;
    }
#endif
    return Connected;
}
float3 RawContribution(uint Index, float Scale, float3 Receiver)
{
#if QUARTET_QUERY_ELISION
    if (QuartetGeometry(Index, Receiver) == 0) return 0;
    float3 Outgoing = SelectedSuffix(Index, Scale);
    if (all(Outgoing == 0)) return 0;
    return Outgoing * ConnectedGeometry(Index, Receiver);
#else
    return SelectedSuffix(Index, Scale) * ConnectedGeometry(Index, Receiver);
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PERSISTENT DESCRIPTOR WIRE
//------------------------------------------------------------------------------------------------------------------------
FacetReservoirDescriptor DecodeReuse(uint Pixel, uint Count, uint Lane)
{
    float4 First = OutputColour[(2 + 2 * Lane) * Count + Pixel];
    float4 Second = OutputColour[(3 + 2 * Lane) * Count + Pixel];
    FacetReservoirDescriptor Descriptor;
    Descriptor.FacetIndex = asuint(First.x); Descriptor.Revision = asuint(First.y);
    Descriptor.U = First.z; Descriptor.V = First.w;
    Descriptor.Outgoing = Second.xyz; Descriptor.Weight = Second.w;
    return Descriptor;
}

void EncodeReuse(uint Pixel, uint Count, uint Lane, FacetReservoirDescriptor Descriptor)
{
    OutputColour[(2 + 2 * Lane) * Count + Pixel] =
        float4(asfloat(Descriptor.FacetIndex), asfloat(Descriptor.Revision), Descriptor.U, Descriptor.V);
    OutputColour[(3 + 2 * Lane) * Count + Pixel] = float4(Descriptor.Outgoing, Descriptor.Weight);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    SurfacePoint Input = Receivers[Pixel];
    uint Seed = asuint(Input.PositionHit.x), Method = asuint(Input.PositionHit.y);
    uint Step = asuint(Input.PositionHit.z), Epoch = asuint(Input.NormalRoughness.x);
    bool Reset = asuint(Input.NormalRoughness.y) != 0;
    if (!Reset) Reset = asuint(OutputColour[18 * Count + Pixel].x) != Epoch;
    float3 Receiver = Input.ViewMetallic.xyz;
    float Scale = Input.NormalRoughness.z;
    float3 Raw = 0;
    uint Proposals = 0, Retained = 0, Evaluations = 0;
    for (uint Lane = 0; Lane < 4; ++Lane)
    {
        bool Refresh = Reset || Method == 1 || Lane / 2 == Step % 2;
        bool Generate = Method < 2 ? Refresh : Method == 3 || Refresh;
        if (Method == 0 && !Refresh)
        {
            ++Retained;
            continue;
        }
        FacetReservoirDescriptor Descriptor;
        Descriptor.FacetIndex = 0xffffffffu; Descriptor.Revision = 0;
        Descriptor.U = 0; Descriptor.V = 0; Descriptor.Outgoing = 0; Descriptor.Weight = 0;
        if (Generate)
        {
            uint First = QuartetHash(Seed ^ (3 * Lane + 1) * 0x9e3779b9u) % 9;
            uint Second = QuartetHash(Seed ^ (3 * Lane + 2) * 0x9e3779b9u) % 9;
            Proposals += 2;
            if (Method >= 2)
            {
                if (First < 8) { Raw += RawContribution(First, Scale, Receiver); ++Evaluations; }
                if (Second < 8) { Raw += RawContribution(Second, Scale, Receiver); ++Evaluations; }
            }
            else
            {
                float FirstTarget = 0, SecondTarget = 0;
                if (First < 8) FirstTarget = 1.0 / 64 + QuartetGeometry(First, Receiver);
                if (Second < 8) SecondTarget = 1.0 / 64 + QuartetGeometry(Second, Receiver);
                float Sum = FirstTarget + SecondTarget;
                if (Sum > 0)
                {
                    float Uniform = float(QuartetHash(Seed ^ (3 * Lane + 3) * 0x9e3779b9u) >> 8) / 16777216.0;
                    uint Selected = Uniform < FirstTarget / Sum ? First : Second;
                    float Target = Selected == First ? FirstTarget : SecondTarget;
                    Descriptor.FacetIndex = Selected; Descriptor.Revision = Epoch;
                    Descriptor.U = .2; Descriptor.V = .3;
                    Descriptor.Outgoing = SelectedSuffix(Selected, Scale);
                    Descriptor.Weight = 9 * Sum / (2 * Target);
                }
            }
        }
        EncodeReuse(Pixel, Count, Lane, Descriptor);
    }
    OutputColour[Pixel] = float4(Method >= 2 ? Raw * 9 / Proposals : float3(0), 1);
    OutputColour[Count + Pixel] = float4(Proposals, Retained, Evaluations, Reset ? 1 : 0);
    for (uint Slot = 10; Slot < 18; ++Slot) OutputColour[Slot * Count + Pixel] = 0;
    OutputColour[10 * Count + Pixel] = float4(SuffixCalls, ReconnectCalls, 0, 0);
    OutputColour[18 * Count + Pixel] = float4(asfloat(Epoch), asfloat(Step), Retained, Reset ? 1 : 0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;
    if (Pixel >= Count) return;
    SurfacePoint Input = Receivers[Pixel];
    if (asuint(Input.PositionHit.y) >= 2) return;
    uint Epoch = asuint(Input.NormalRoughness.x), Evaluations = 0;
    float3 Estimate = 0;
    for (uint Lane = 0; Lane < 4; ++Lane)
    {
        FacetReservoirDescriptor Descriptor = DecodeReuse(Pixel, Count, Lane);
        bool Valid = Descriptor.FacetIndex < 8 && Descriptor.Revision == Epoch && Descriptor.Weight > 0 &&
            all(isfinite(float4(Descriptor.U, Descriptor.V, Descriptor.Weight, 0))) &&
            all(isfinite(Descriptor.Outgoing)) && all(Descriptor.Outgoing >= 0) &&
            Descriptor.U >= 0 && Descriptor.V >= 0 && Descriptor.U + Descriptor.V <= 1;
        if (Valid)
        {
#if QUARTET_QUERY_ELISION
            if (any(Descriptor.Outgoing > 0))
#endif
            {
                Estimate += Descriptor.Outgoing * Descriptor.Weight *
                    ConnectedGeometry(Descriptor.FacetIndex, Input.ViewMetallic.xyz, Descriptor.U, Descriptor.V) / 4;
            }
            ++Evaluations;
        }
    }
    OutputColour[Pixel] = float4(Estimate, 1);
    OutputColour[Count + Pixel].z = float(Evaluations);
    OutputColour[10 * Count + Pixel].zw = float2(SuffixCalls, ReconnectCalls);
}

# 🧩 Retain eight raw source proposals with area densities; rotate candidates before receiver evaluation.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileCacheRefresh.py', str(Destination)], check=True)
Source = Destination.read_text()
Prefix = Source[:Source.index('uint VertexSlot(')]
Destination.write_text(Prefix + '''
uint BankSlot(uint Slot, uint Pixel) { return Extent.Dimensions.x * Slot + Pixel; }

float BankContribution(float4 Candidate, float ReceiverX)
{
    if (Candidate.w <= 0 || !SecondaryVisible(Candidate.xy, ReceiverX, 0)) return 0;
    return .6 * Candidate.z * SecondaryCosine(Candidate.xy, ReceiverX) *
           SecondaryMeasure(Candidate.xy, ReceiverX) / (3.141592653589793 * Candidate.w * 8);
}

// Independent nonpersistent eight-draw area-IS control; null draws still count in the divisor.
void InlineBankEstimate(uint Pixel)
{
    SurfacePoint R = Receivers[Pixel];
    CacheIntensity = R.ResidualSpecular.x;
    TriangleProposalOrigin = 0;
    uint Bits = Extent.Sampling.x ^ ((Pixel % 1024u) * 747796405u + 2891336453u);
    float Estimate = 0;
    for (uint Index = 0; Index < 8; ++Index)
    {
        bool CosineProposal;
        float2 Point = SecondaryProposal(Bits, .75, CosineProposal);
        if (!all(abs(Point) <= .5)) continue;
        if (CosineProposal && !SecondaryVisible(Point, 0, 0)) continue;
        float Density = .25 + (SecondaryVisible(Point, 0, 0)
            ? .75 * SecondaryCosine(Point, 0) * SecondaryMeasure(Point, 0) / 3.141592653589793 : 0);
        float Outgoing = TriangleOutgoing(Point);
        if (SecondaryVisible(Point, R.PositionHit.x, 0))
            Estimate += .6 * Outgoing * SecondaryCosine(Point, R.PositionHit.x) *
                        SecondaryMeasure(Point, R.PositionHit.x) / (3.141592653589793 * Density * 8);
    }
    OutputColour[Pixel] = float4(Estimate, float(TriangleQueryCount), 0, float(TriangleQueryCount));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    uint Method = uint(R.MotionPixel.z);
    if (Method == 0) { InlineBankEstimate(Pixel); return; }
    if (Method >= 4) { FreshComponent(Pixel, Method == 4 ? 6 : 8); return; }
    CacheIntensity = R.ResidualSpecular.x;
    TriangleProposalOrigin = 0;
    float4 Prior = OutputColour[BankSlot(9, Pixel)];
    bool Full = Method == 1 || Extent.History.w != 0 || Extent.Reconstruction.x == 0 ||
                Prior.z != R.MotionPositionToken.w || Prior.x != 8;
    uint Replace = Full ? 8 : Method == 2 ? 4 : 2;
    uint Start = (uint(R.MotionNormal.w) * Replace) % 8;
    uint Bits = Extent.Sampling.x ^ ((Pixel % 1024u) * 747796405u + 2891336453u);
    for (uint Index = 0; Index < 8; ++Index)
    {
        if (!Full && (Index + 8 - Start) % 8 >= Replace) continue;
        bool CosineProposal;
        float2 Point = SecondaryProposal(Bits, .75, CosineProposal);
        bool Valid = all(abs(Point) <= .5);
        if (Valid && CosineProposal && !SecondaryVisible(Point, 0, 0)) Valid = false;
        float4 Candidate = 0;
        if (Valid)
        {
            float Density = .25 + (SecondaryVisible(Point, 0, 0)
                ? .75 * SecondaryCosine(Point, 0) * SecondaryMeasure(Point, 0) / 3.141592653589793 : 0);
            Candidate = float4(Point, TriangleOutgoing(Point), Density);
        }
        OutputColour[BankSlot(1 + Index, Pixel)] = Candidate;
    }
    OutputColour[BankSlot(9, Pixel)] = float4(8, 0, R.MotionPositionToken.w, 1);
    OutputColour[BankSlot(10, Pixel)] = float4(float(TriangleQueryCount), float(8 - Replace), 0, 0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    uint Method = uint(R.MotionPixel.z);
    if (Method == 0 || Method >= 4) return;
    float Estimate = 0;
    for (uint Index = 0; Index < 8; ++Index)
        Estimate += BankContribution(OutputColour[BankSlot(1 + Index, Pixel)], R.PositionHit.x);
    float4 Telemetry = OutputColour[BankSlot(10, Pixel)];
    OutputColour[Pixel] = float4(Estimate, Telemetry.x + float(TriangleQueryCount), Telemetry.y, Telemetry.x);
}
''')

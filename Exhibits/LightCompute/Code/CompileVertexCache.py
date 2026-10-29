# 🧩 Split verified triangle transport into persistent source-payload generation and receiver evaluation.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileTriangleReconnection.py', str(Destination)], check=True)
Source = Destination.read_text()
Start = Source.index('[shader("compute")]')
Body = Source[Start:Source.index('// The host', Start)]
Body = Body[Body.index('    uint Pixel = Dispatch.x;'):Body.rindex('}')]
Body = Body.replace('    uint Pixel = Dispatch.x;\n', '')
Body = Body.replace('uint Method = uint(Receiver.MotionPixel.z);', 'uint Method = 1;\n    CacheIntensity = Receiver.ResidualSpecular.x;')
Body = Body.replace('float(Present), 8, float(TriangleQueryCount)', 'float(TriangleQueryCount), 0, float(TriangleQueryCount)')
Prefix = Source[:Start].replace('static float TriangleProposalOrigin = 0;', 'static float TriangleProposalOrigin = 0;\nstatic float CacheIntensity = 1;')
Prefix = Prefix.replace('return Visible ? Albedo * Cosine / .6 : 0;', 'return Visible ? CacheIntensity * Albedo * Cosine / .6 : 0;')
Destination.write_text(Prefix + 'void InlineVertexEstimate(uint Pixel)\n{\n' + Body + '}\n' + '''
uint VertexSlot(uint Slot, uint Pixel) { return Extent.Dimensions.x * Slot + Pixel; }

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    uint Method = uint(R.MotionPixel.z);
    if (Method == 0) { InlineVertexEstimate(Pixel); return; }
    CacheIntensity = R.ResidualSpecular.x;
    TriangleProposalOrigin = 0;
    float4 Prior = OutputColour[VertexSlot(2, Pixel)];
    bool Reset = Extent.History.w != 0;
    bool SameRevision = Prior.z == R.MotionPositionToken.w;
    if (Method == 2) SameRevision = true;  // Deliberate stale-light negative control.
    if (Method == 3) Reset = false;        // Deliberate stale-geometry/material negative control.
    bool Reuse = R.MotionNormal.w == 0 && Extent.Reconstruction.x != 0 &&
                 !Reset && SameRevision && Prior.x == 8;
    if (Reuse)
    {
        OutputColour[VertexSlot(3, Pixel)] = float4(0, 1, 0, 0);
        return;
    }
    uint Bits = Extent.Sampling.x ^ ((Pixel % 4096u) * 747796405u + 2891336453u);
    float Sum = 0;
    float2 Selected = 0;
    uint Present = 0;
    for (uint Index = 0; Index < 8; ++Index)
    {
        bool CosineProposal;
        float2 Point = SecondaryProposal(Bits, .75, CosineProposal);
        bool Valid = all(abs(Point) <= .5);
        if (CosineProposal && !SecondaryVisible(Point, 0, 0)) Valid = false;
        float Weight = 0;
        if (Valid)
        {
            ++Present;
            float Measure = SecondaryMeasure(Point, 0);
            float Density = .25 + (SecondaryVisible(Point, 0, 0)
                ? .75 * SecondaryCosine(Point, 0) * Measure / 3.141592653589793 : 0);
            Weight = (SecondaryAngular(Point, 0, 0) + .01) * Measure / Density;
        }
        Sum += Weight;
        if (RandomUnit(Bits) * Sum < Weight) Selected = Point;
    }
    float Outgoing = 0, Normalization = 0;
    if (Sum > 0)
    {
        Outgoing = TriangleOutgoing(Selected);
        float Target = .01 + (SecondaryVisible(Selected, 0, 0)
            ? .6 * Outgoing * SecondaryCosine(Selected, 0) / 3.141592653589793 : 0);
        Normalization = Sum / (8 * Target * SecondaryMeasure(Selected, 0));
    }
    OutputColour[VertexSlot(1, Pixel)] = float4(Selected, Outgoing, Normalization);
    OutputColour[VertexSlot(2, Pixel)] = float4(8, float(Present), R.MotionPositionToken.w, 1);
    OutputColour[VertexSlot(3, Pixel)] = float4(float(TriangleQueryCount), 0, 0, 0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateVertexPayload(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x || uint(Receivers[Pixel].MotionPixel.z) == 0) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Payload = OutputColour[VertexSlot(1, Pixel)];
    float4 Telemetry = OutputColour[VertexSlot(3, Pixel)];
    float Estimate = 0;
    if (Payload.w > 0 && SecondaryVisible(Payload.xy, R.PositionHit.x, 0))
        Estimate = .6 * Payload.z * SecondaryCosine(Payload.xy, R.PositionHit.x) /
                   3.141592653589793 * SecondaryMeasure(Payload.xy, R.PositionHit.x) * Payload.w;
    OutputColour[Pixel] = float4(Estimate, Telemetry.x + float(TriangleQueryCount), Telemetry.y, Telemetry.x);
}
''')

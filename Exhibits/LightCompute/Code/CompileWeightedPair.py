# 🧩 Compress four proposals into each of two positive-target weighted surface reservoirs.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileVisibilityReuse.py', str(Destination)], check=True)
Source = Destination.read_text()
Weighted = '''
float PairTarget(float2 Point, float ReceiverX)
{
    return SecondaryCosine(Point, ReceiverX) * SecondaryMeasure(Point, ReceiverX) / 3.141592653589793;
}

void GenerateWeightedPair(uint Pixel)
{
    SurfacePoint R = Receivers[Pixel];
    CacheIntensity = R.ResidualSpecular.x;
    TriangleProposalOrigin = R.PositionHit.x;
    float4 Prior = OutputColour[BankSlot(9, Pixel)];
    bool Full = uint(R.MotionPixel.z) == 12 || Extent.History.w != 0 || Extent.Reconstruction.x == 0 ||
                Prior.z != R.MotionPositionToken.w || Prior.x != 2;
    uint Bits = Extent.Sampling.x ^ ((Pixel % 1024u) * 747796405u + 2891336453u);
    uint SelectionBits = Bits ^ 0x91e10da5u;
    float2 Confirmed = 0;
    for (uint Lane = 0; Lane < 2; ++Lane)
    {
        if (!Full && Lane != uint(R.MotionNormal.w) % 2) continue;
        float Sum = 0, SelectedTarget = 0;
        float4 Selected = 0, Uniforms = 0, Weights = 0;
        uint SelectedIndex = 4;
        for (uint Draw = 0; Draw < 4; ++Draw)
        {
            bool CosineProposal;
            float2 Point = SecondaryProposal(Bits, .75, CosineProposal);
            bool Valid = all(abs(Point) <= .5);
            bool Visible = Valid ? SecondaryVisible(Point, R.PositionHit.x, 0) : false;
            if (CosineProposal && !Visible) Valid = false;
            float4 Candidate = 0;
            float Target = 0, Weight = 0;
            if (Valid)
            {
                Target = PairTarget(Point, R.PositionHit.x);
                float Density = .25 + (Visible ? .75 * Target : 0);
                Candidate = float4(Point, TriangleOutgoing(Point), Density);
                Weight = Target / Density;
            }
            float Uniform = RandomUnit(SelectionBits);
            Uniforms[Draw] = Uniform;
            Weights[Draw] = Weight;
            Sum += Weight;
            if (Weight > 0 && Uniform * Sum < Weight)
            {
                Selected = Candidate; SelectedTarget = Target; SelectedIndex = Draw;
            }
            // Audit only:these eight proposal slots are NOT persistent algorithmic history.
            OutputColour[BankSlot(17 + Lane * 4 + Draw, Pixel)] = Candidate;
        }
        Confirmed[Lane] = Selected.w > .25 ? 1 : 0;
        if (SelectedTarget > 0) Selected.w = Sum / (4 * SelectedTarget);
        OutputColour[BankSlot(1 + Lane, Pixel)] = Selected;
        OutputColour[BankSlot(25 + Lane, Pixel)] = float4(Sum, SelectedTarget, float(SelectedIndex), R.PositionHit.x);
        OutputColour[BankSlot(27 + Lane, Pixel)] = Uniforms;
        OutputColour[BankSlot(29 + Lane, Pixel)] = Weights;
    }
    OutputColour[BankSlot(9, Pixel)] = float4(2, 0, R.MotionPositionToken.w, 1);
    OutputColour[BankSlot(10, Pixel)] = float4(float(TriangleQueryCount), Full ? 0 : 1, Confirmed);
}

void EvaluateWeightedPair(uint Pixel)
{
    SurfacePoint R = Receivers[Pixel];
    float Estimate = 0;
    float4 Telemetry = OutputColour[BankSlot(10, Pixel)];
    for (uint Lane = 0; Lane < 2; ++Lane)
    {
        float4 C = OutputColour[BankSlot(1 + Lane, Pixel)];
        bool BornNow = Telemetry.y == 0 || Lane == uint(R.MotionNormal.w) % 2;
        bool Visible = C.w > 0 && (BornNow ? Telemetry[2 + Lane] > 0 : SecondaryVisible(C.xy, R.PositionHit.x, 0));
        if (Visible)
            Estimate += .6 * C.z * PairTarget(C.xy, R.PositionHit.x) * C.w / 2;
    }
    OutputColour[Pixel] = float4(Estimate, Telemetry.x + float(TriangleQueryCount), Telemetry.y, Telemetry.x);
}

'''
At = Source.index('[shader("compute")]')
Source = Source[:At] + Weighted + Source[At:]
for Function, Call in (('GenerateVertexPayload', 'GenerateWeightedPair'), ('EvaluateVertexPayload', 'EvaluateWeightedPair')):
    At = Source.index('    SurfacePoint R = Receivers[Pixel];', Source.index('void ' + Function))
    Source = Source[:At] + f'    if (uint(Receivers[Pixel].MotionPixel.z) >= 12) {{ {Call}(Pixel); return; }}\n' + Source[At:]
Destination.write_text(Source)

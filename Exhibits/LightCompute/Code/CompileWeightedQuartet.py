# 🧩 Retain four selected surface reservoirs from two proposals each with alternating two-reservoir injection.
from pathlib import Path
import subprocess
import sys

Destination=Path(sys.argv[1])
subprocess.run([sys.executable,'Exhibits/LightCompute/Code/CompileWeightedPair.py',str(Destination)],check=True)
Source=Destination.read_text()
Quartet='''
void GenerateWeightedQuartet(uint Pixel)
{
    SurfacePoint R = Receivers[Pixel];
    CacheIntensity = R.ResidualSpecular.x;
    TriangleProposalOrigin = R.PositionHit.x;
    float4 Prior = OutputColour[BankSlot(9, Pixel)];
    bool Full = uint(R.MotionPixel.z) == 14 || Extent.History.w != 0 || Extent.Reconstruction.x == 0 ||
                Prior.z != R.MotionPositionToken.w || Prior.x != 4;
    uint Bits = Extent.Sampling.x ^ ((Pixel % 1024u) * 747796405u + 2891336453u);
    uint SelectionBits = Bits ^ 0x91e10da5u;
    uint Start = (uint(R.MotionNormal.w) * 2) % 4, Confirmed = 0;
    for (uint Lane = 0; Lane < 4; ++Lane)
    {
        if (!Full && (Lane + 4 - Start) % 4 >= 2) continue;
        float Sum = 0, SelectedTarget = 0;
        float4 Selected = 0;
        float2 Uniforms = 0, Weights = 0;
        uint SelectedIndex = 2;
        for (uint Draw = 0; Draw < 2; ++Draw)
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
            Uniforms[Draw] = Uniform; Weights[Draw] = Weight;
            Sum += Weight;
            if (Weight > 0 && Uniform * Sum < Weight)
            {
                Selected = Candidate; SelectedTarget = Target; SelectedIndex = Draw;
            }
            OutputColour[BankSlot(17 + Lane * 2 + Draw, Pixel)] = Candidate;
        }
        if (Selected.w > .25) Confirmed |= 1u << Lane;
        if (SelectedTarget > 0) Selected.w = Sum / (2 * SelectedTarget);
        OutputColour[BankSlot(1 + Lane, Pixel)] = Selected;
        OutputColour[BankSlot(11 + Lane, Pixel)] = float4(Sum, SelectedTarget, float(SelectedIndex), R.PositionHit.x);
        float4 U = OutputColour[BankSlot(15 + Lane / 2, Pixel)];
        float4 W = OutputColour[BankSlot(25 + Lane / 2, Pixel)];
        U[(Lane % 2) * 2] = Uniforms.x; U[(Lane % 2) * 2 + 1] = Uniforms.y;
        W[(Lane % 2) * 2] = Weights.x; W[(Lane % 2) * 2 + 1] = Weights.y;
        OutputColour[BankSlot(15 + Lane / 2, Pixel)] = U;
        OutputColour[BankSlot(25 + Lane / 2, Pixel)] = W;
    }
    OutputColour[BankSlot(9, Pixel)] = float4(4, 0, R.MotionPositionToken.w, 1);
    OutputColour[BankSlot(10, Pixel)] = float4(float(TriangleQueryCount), Full ? 0 : 2, float(Confirmed), 0);
}

void EvaluateWeightedQuartet(uint Pixel)
{
    SurfacePoint R = Receivers[Pixel];
    float Estimate = 0;
    float4 Telemetry = OutputColour[BankSlot(10, Pixel)];
    uint Start = (uint(R.MotionNormal.w) * 2) % 4;
    for (uint Lane = 0; Lane < 4; ++Lane)
    {
        float4 C = OutputColour[BankSlot(1 + Lane, Pixel)];
        bool BornNow = Telemetry.y == 0 || (Lane + 4 - Start) % 4 < 2;
        bool Visible = C.w > 0 && (BornNow ? (uint(Telemetry.z) & (1u << Lane)) != 0 :
                                             SecondaryVisible(C.xy, R.PositionHit.x, 0));
        if (Visible) Estimate += .6 * C.z * PairTarget(C.xy, R.PositionHit.x) * C.w / 4;
    }
    OutputColour[Pixel] = float4(Estimate, Telemetry.x + float(TriangleQueryCount), Telemetry.y, Telemetry.x);
}

'''
At=Source.index('[shader("compute")]')
Source=Source[:At]+Quartet+Source[At:]
for Function,Call in (('GenerateVertexPayload','GenerateWeightedQuartet'),('EvaluateVertexPayload','EvaluateWeightedQuartet')):
    At=Source.index('    if (uint(Receivers[Pixel].MotionPixel.z) >= 12)',Source.index('void '+Function))
    Source=Source[:At]+f'    if (uint(Receivers[Pixel].MotionPixel.z) >= 14) {{ {Call}(Pixel); return; }}\n'+Source[At:]
Destination.write_text(Source)

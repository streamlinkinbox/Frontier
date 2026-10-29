# 🧩 Pair inherited and query-elided transport with identical proposals, RNG advances and stored birth densities.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReceiverBank.py', str(Destination)], check=True)
Source = Destination.read_text()
# The first six arms remain unchanged; the next six opt in via their receiver method index.
Start = Source.index('void FreshComponent(')
End = Source.index('uint BankSlot(', Start)
Body = Source[Start:End]
Old = 'if (CosineProposal && !SecondaryVisible(Point, Method == 6 ? ReceiverX : 0, Blocker)) Valid = false;'
assert Body.count(Old) == 1
Body = Body.replace(Old, 'if (uint(Receiver.MotionPixel.z) < 6 && CosineProposal && !SecondaryVisible(Point, Method == 6 ? ReceiverX : 0, Blocker)) Valid = false;')
Source = Source[:Start] + Body + Source[End:]
Start = Source.index('void InlineBankEstimate(')
Body = Source[Start:]
assert Body.count('uint Method = uint(R.MotionPixel.z);') == 2
Body = Body.replace('uint Method = uint(R.MotionPixel.z);', 'uint Method = uint(R.MotionPixel.z) % 6;')
Old = 'if (CosineProposal && !SecondaryVisible(Point, R.PositionHit.x, 0)) continue;'
assert Body.count(Old) == 1
Body = Body.replace(Old, 'if (uint(R.MotionPixel.z) < 6 && CosineProposal && !SecondaryVisible(Point, R.PositionHit.x, 0)) continue;')
Old = 'if (Valid && CosineProposal && !SecondaryVisible(Point, R.PositionHit.x, 0)) Valid = false;'
assert Body.count(Old) == 1
Body = Body.replace(Old, 'if (uint(R.MotionPixel.z) < 6 && Valid && CosineProposal && !SecondaryVisible(Point, R.PositionHit.x, 0)) Valid = false;')
Old = 'float Density = .25 + (SecondaryVisible(Point, R.PositionHit.x, 0)'
assert Body.count(Old) == 2
Body = Body.replace(Old, 'float Density = .25 + (((uint(R.MotionPixel.z) >= 6 && CosineProposal) || SecondaryVisible(Point, R.PositionHit.x, 0))')
Old = 'if (SecondaryVisible(Point, R.PositionHit.x, 0))'
assert Body.count(Old) == 1
Body = Body.replace(Old, 'if (uint(R.MotionPixel.z) >= 6 ? Density > .25 : SecondaryVisible(Point, R.PositionHit.x, 0))')
Old = '''    float Estimate = 0;
    for (uint Index = 0; Index < 8; ++Index)
        Estimate += BankContribution(OutputColour[BankSlot(1 + Index, Pixel)], R.PositionHit.x);
    float4 Telemetry = OutputColour[BankSlot(10, Pixel)];'''
assert Body.count(Old) == 1
Body = Body.replace(Old, '''    float Estimate = 0;
    float4 Telemetry = OutputColour[BankSlot(10, Pixel)];
    uint Replace = 8 - uint(Telemetry.y), Start = (uint(R.MotionNormal.w) * Replace) % 8;
    for (uint Index = 0; Index < 8; ++Index)
    {
        float4 C = OutputColour[BankSlot(1 + Index, Pixel)];
        bool BornNow = (Index + 8 - Start) % 8 < Replace;
        if (uint(R.MotionPixel.z) >= 6 && BornNow)
        {
            // Only newly generated q encodes visibility at THIS receiver; never infer it from historical q.
            if (C.w > .25)
                Estimate += .6 * C.z * SecondaryCosine(C.xy, R.PositionHit.x) *
                            SecondaryMeasure(C.xy, R.PositionHit.x) / (3.141592653589793 * C.w * 8);
        }
        else Estimate += BankContribution(C, R.PositionHit.x);
    }''')
Source = Source[:Start] + Body
Probe = '''void AuditBudgetEdge(uint Pixel)
{
    SurfacePoint R = Receivers[Pixel];
    float3 Direction = normalize(float3(R.MotionPixel.xy - float2(R.PositionHit.x, 0), 1));
    float3 Strike;
    uint Hit = TriangleQuery(float3(R.PositionHit.x, 0, 0), Direction, 100000, 14, Strike);
    float2 Point = float2(R.PositionHit.x, 0) + Direction.xy * Strike.x;
    bool First = Hit != 0xffffffffu && Facets[Hit].A.z == 1 && all(abs(Point) <= .5);
    bool Recheck = First ? SecondaryVisible(Point, R.PositionHit.x, 0) : false;
    OutputColour[Pixel] = float4(float(First), float(Recheck), float(TriangleQueryCount), 0);
}

'''
Position = Source.index('[shader("compute")]', Source.index('void InlineBankEstimate('))
Source = Source[:Position] + Probe + Source[Position:]
Needle = '    SurfacePoint R = Receivers[Pixel];\n    uint Method = uint(R.MotionPixel.z) % 6;'
assert Source.count(Needle) == 2
Source = Source.replace(Needle, '    SurfacePoint R = Receivers[Pixel];\n    if (R.ResidualSpecular.y != 0) { AuditBudgetEdge(Pixel); return; }\n    uint Method = uint(R.MotionPixel.z) % 6;', 1)
Position = Source.index('void EvaluateVertexPayload(')
Source = Source[:Position] + Source[Position:].replace(Needle, '    SurfacePoint R = Receivers[Pixel];\n    if (R.ResidualSpecular.y != 0) return;\n    uint Method = uint(R.MotionPixel.z) % 6;', 1)
Destination.write_text(Source)

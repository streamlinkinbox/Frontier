# 🧩 Reuse confirmed reconstructed-point visibility; never substitute a primary first-hit classification.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReceiverBank.py', str(Destination)], check=True)
Source = Destination.read_text()
Start = Source.index('void FreshComponent(')
End = Source.index('uint BankSlot(', Start)
Body = Source[Start:End]
Old = 'if (CosineProposal && !SecondaryVisible(Point, Method == 6 ? ReceiverX : 0, Blocker)) Valid = false;'
assert Body.count(Old) == 1
Body = Body.replace(Old, 'if ((uint(Receiver.MotionPixel.z) < 6 || Valid) && CosineProposal && !SecondaryVisible(Point, Method == 6 ? ReceiverX : 0, Blocker)) Valid = false;')
Body = Body.replace('float Sum = 0, Fresh = 0;', 'float Sum = 0, Fresh = 0;\n    uint Misses = 0;')
Body = Body.replace('bool Valid = all(abs(Point) <= .5);', 'bool Valid = all(abs(Point) <= .5);\n        if (!Valid) ++Misses;')
Body = Body.replace('    OutputColour[Pixel] = float4(Estimate, float(TriangleQueryCount), 0, float(TriangleQueryCount));',
                    '    OutputColour[Pixel] = float4(Estimate, float(TriangleQueryCount), 0, float(TriangleQueryCount));\n    OutputColour[Extent.Dimensions.x * 16 + Pixel] = float4(float(Budget), float(Misses), 0, 0);')
Source = Source[:Start] + Body + Source[End:]
Start = Source.index('void InlineBankEstimate(')
Body = Source[Start:]
assert Body.count('uint Method = uint(R.MotionPixel.z);') == 2
Body = Body.replace('uint Method = uint(R.MotionPixel.z);', 'uint Method = uint(R.MotionPixel.z) % 6;')
Old = '''        if (CosineProposal && !SecondaryVisible(Point, R.PositionHit.x, 0)) continue;
        float Density = .25 + (SecondaryVisible(Point, R.PositionHit.x, 0)'''
New = '''        bool Reuse = uint(R.MotionPixel.z) >= 6;
        bool Visible = Reuse ? SecondaryVisible(Point, R.PositionHit.x, 0) : false;
        if (CosineProposal && !(Reuse ? Visible : SecondaryVisible(Point, R.PositionHit.x, 0))) continue;
        float Density = .25 + ((Reuse ? Visible : SecondaryVisible(Point, R.PositionHit.x, 0))'''
assert Body.count(Old) == 1
Body = Body.replace(Old, New)
Body = Body.replace('if (SecondaryVisible(Point, R.PositionHit.x, 0))', 'if (Reuse ? Visible : SecondaryVisible(Point, R.PositionHit.x, 0))')
Old = '''        if (Valid && CosineProposal && !SecondaryVisible(Point, R.PositionHit.x, 0)) Valid = false;
        float4 Candidate = 0;
        if (Valid)
        {
            float Density = .25 + (SecondaryVisible(Point, R.PositionHit.x, 0)'''
New = '''        bool Reuse = uint(R.MotionPixel.z) >= 6;
        bool Visible = Reuse && Valid ? SecondaryVisible(Point, R.PositionHit.x, 0) : false;
        if (Valid && CosineProposal && !(Reuse ? Visible : SecondaryVisible(Point, R.PositionHit.x, 0))) Valid = false;
        float4 Candidate = 0;
        if (Valid)
        {
            float Density = .25 + ((Reuse ? Visible : SecondaryVisible(Point, R.PositionHit.x, 0))'''
assert Body.count(Old) == 1
Body = Body.replace(Old, New)
Old = '''    float Estimate = 0;
    for (uint Index = 0; Index < 8; ++Index)
        Estimate += BankContribution(OutputColour[BankSlot(1 + Index, Pixel)], R.PositionHit.x);
    float4 Telemetry = OutputColour[BankSlot(10, Pixel)];'''
New = '''    float Estimate = 0;
    float4 Telemetry = OutputColour[BankSlot(10, Pixel)];
    uint Replace = 8 - uint(Telemetry.y), Start = (uint(R.MotionNormal.w) * Replace) % 8;
    for (uint Index = 0; Index < 8; ++Index)
    {
        float4 C = OutputColour[BankSlot(1 + Index, Pixel)];
        bool BornNow = (Index + 8 - Start) % 8 < Replace;
        if (uint(R.MotionPixel.z) >= 6 && BornNow)
        {
            // New q encodes the CONFIRMED reconstructed-point query, not the primary hit result.
            if (C.w > .25)
                Estimate += .6 * C.z * SecondaryCosine(C.xy, R.PositionHit.x) *
                            SecondaryMeasure(C.xy, R.PositionHit.x) / (3.141592653589793 * C.w * 8);
        }
        else Estimate += BankContribution(C, R.PositionHit.x);
    }'''
assert Body.count(Old) == 1
Body = Body.replace(Old, New)
Source = Source[:Start] + Body
Probe = '''void AuditVisibilityEdge(uint Pixel)
{
    SurfacePoint R = Receivers[Pixel];
    float3 Origin = float3(R.PositionHit.x, 0, 0);
    float3 Direction = normalize(float3(R.MotionPixel.xy - Origin.xy, 1));
    float3 Strike = 0;
    uint Hit = TriangleQuery(Origin, Direction, 100000, 14, Strike);
    float2 Point = Origin.xy + Direction.xy * Strike.x;
    bool First = Hit != 0xffffffffu && Facets[Hit].A.z == 1 && all(abs(Point) <= .5);
    float3 Delta = float3(Point, 1) - Origin;
    float Distance = length(Delta);
    float3 Reconstructed = Delta / Distance;
    float3 OriginalStrike = 0, ReconstructedStrike = 0;
    uint OriginalHit = 0xffffffffu, ReconstructedHit = 0xffffffffu;
    bool Recheck = false, Confirmed = false;
    if (First)
    {
        OriginalHit = TriangleQuery(Origin, Direction, Distance - .0001, 14, OriginalStrike);
        ReconstructedHit = TriangleQuery(Origin, Reconstructed, Distance - .0001, 15, ReconstructedStrike);
        Recheck = SecondaryVisible(Point, R.PositionHit.x, 0);
        Confirmed = SecondaryVisible(Point, R.PositionHit.x, 0);
    }
    OutputColour[BankSlot(11, Pixel)] = float4(Direction, Strike.x);
    OutputColour[BankSlot(12, Pixel)] = float4(Point, Distance, Distance - .0001);
    OutputColour[BankSlot(13, Pixel)] = float4(Reconstructed, 0);
    OutputColour[BankSlot(14, Pixel)] = float4(Hit == 0xffffffffu ? -1 : float(Hit),
        OriginalHit == 0xffffffffu ? -1 : float(OriginalHit),
        ReconstructedHit == 0xffffffffu ? -1 : float(ReconstructedHit), float(First));
    OutputColour[BankSlot(15, Pixel)] = float4(OriginalStrike, ReconstructedStrike.x);
    OutputColour[Pixel] = float4(float(First), float(Recheck), float(Confirmed), float(TriangleQueryCount));
}

'''
Position = Source.index('[shader("compute")]', Source.index('void InlineBankEstimate('))
Source = Source[:Position] + Probe + Source[Position:]
Needle = '    SurfacePoint R = Receivers[Pixel];\n    uint Method = uint(R.MotionPixel.z) % 6;'
assert Source.count(Needle) == 2
Source = Source.replace(Needle, '    SurfacePoint R = Receivers[Pixel];\n    if (R.ResidualSpecular.y != 0) { AuditVisibilityEdge(Pixel); return; }\n    uint Method = uint(R.MotionPixel.z) % 6;', 1)
Position = Source.index('void EvaluateVertexPayload(')
Source = Source[:Position] + Source[Position:].replace(Needle, '    SurfacePoint R = Receivers[Pixel];\n    if (R.ResidualSpecular.y != 0) return;\n    uint Method = uint(R.MotionPixel.z) % 6;', 1)
Destination.write_text(Source)

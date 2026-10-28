#============================================================================================================================================
#                                                  COMPILEREPLAYINDIRECTSPLIT.PY
#============================================================================================================================================
# 🧩 Separate first-bounce fresh lighting from deeper reservoir transport without changing its random sequence.

from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
Destination.parent.mkdir(parents=True, exist_ok=True)
Whole = Destination.parent / 'whole'
Whole.mkdir(exist_ok=True)
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReplayResetBudget.py',
                str(Whole / 'ReplayResetBudget.slang')], check=True)
Source = (Whole / 'ReplayResetBudget.slang').read_text()

def Once(Text, Previous, Replacement):
    if Text.count(Previous) != 1:
        raise RuntimeError('Review indirect split substitution: ' + Previous)
    return Text.replace(Previous, Replacement)

Start = Source.index('float3 EvaluateReplayPath(')
End = Source.index('float ReplayTarget(', Start)
PathBody = Source[Start:End]
Segment = Source[Source.index('bool ReplaySegmentOccluded('):Source.index('// 📝 A half-uniform mixture')]
Facet = Source[Source.index('float3 ReplayFacetRadiance('):Start]
DirectSegment = Segment.replace('ReplaySegmentOccluded(', 'ReplayDirectSegmentOccluded(').replace('Counters[2]', 'Counters[14]')
DirectFacet = Facet.replace('ReplayFacetRadiance(', 'ReplayDirectFacetRadiance(').replace('ReplaySegmentOccluded(', 'ReplayDirectSegmentOccluded(')
DirectPath = PathBody.replace('EvaluateReplayPath(', 'EvaluateReplayDirect(').replace('Counters[16]', 'Counters[12]').replace('Counters[17]', 'Counters[13]')
DirectPath = Once(DirectPath, 'Bounce < Extent.Primary.z', 'Bounce < min(1u, Extent.Primary.z)')
DirectPath = DirectPath.replace('ReplayFacetRadiance(', 'ReplayDirectFacetRadiance(')
WholePath = PathBody.replace('EvaluateReplayPath(', 'EvaluateReplayWhole(')
IndirectPath = Once(PathBody, 'ReplayFacetRadiance(Current, Origin, RoughOpaque, Bits)',
                    'ReplayFacetRadiance(Current, Origin, RoughOpaque, Bits, Bounce != 0)')
IndirectPath = Once(IndirectPath, 'Radiance += Throughput * max(Extent.Minimum.xyz, 0);',
                    'if (Bounce != 0) Radiance += Throughput * max(Extent.Minimum.xyz, 0);')
IndirectPath = Once(IndirectPath, 'if (!SampleFacets || (Extent.Primary.w & 256u) != 0)',
                    'if (Bounce != 0 && (!SampleFacets || (Extent.Primary.w & 256u) != 0))')
IndirectPath = Once(IndirectPath, 'else if ((Extent.Primary.w & 1024u) != 0 && any(F.Emissive.xyz > 0)',
                    'else if (Bounce != 0 && (Extent.Primary.w & 1024u) != 0 && any(F.Emissive.xyz > 0)')
IndirectFacet = Once(Facet, 'bool RoughOpaque, inout uint Bits)',
                     'bool RoughOpaque, inout uint Bits, bool TraceVisibility = true)')
IndirectFacet = Once(IndirectFacet, '    float3 Delta = Point - Origin;',
                     '    if (!TraceVisibility) return 0;\n    float3 Delta = Point - Origin;')
Source = Once(Source, Facet + PathBody, DirectSegment + DirectFacet + DirectPath + IndirectFacet + WholePath + IndirectPath)
Combine = '''float3 ReplaySplitOutput(float3 Indirect, float3 Direct, float3 Residual)
{
#ifdef FRONTIER_SPLIT_DIRECT_ONLY
    return Direct;
#else
    return (Indirect + Direct) + Residual;
#endif
}
'''
Source = Once(Source, '[shader("compute")][numthreads(64,1,1)] void GenerateReplayReservoirs',
               Combine + '[shader("compute")][numthreads(64,1,1)] void GenerateReplayReservoirs')
DirectMean = '''    // Current output RGB is temporary direct-light scratch between Generate and Resolve.
    float3 Direct = 0;
    if (R.PositionHit.w != 0 && R.MotionPositionToken.w > 0 && R.ViewMetallic.w == 0)
    {
        uint DirectBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
        uint DirectSeed = NextRandom(DirectBits);
        Direct = EvaluateReplayDirect(R, DirectSeed);
        for (uint Index = 1; Index < 8; ++Index)
            Direct += EvaluateReplayDirect(R, NextRandom(DirectBits));
        Direct /= 8.f;
    }
    OutputColour[Pixel] = float4(Direct, 1);
'''
Source = Once(Source, '    float4 Packed = 0, SelectedRadiance = 0;\n',
               '    float4 Packed = 0, SelectedRadiance = 0;\n' + DirectMean)
Source = Once(Source, 'float4(OutputColour[ReplaySlot(3, Pixel)].xyz + R.ResidualSpecular.xyz, 1)',
               'float4(ReplaySplitOutput(OutputColour[ReplaySlot(3, Pixel)].xyz, OutputColour[Pixel].xyz, R.ResidualSpecular.xyz), 1)')
Source = Once(Source, 'float4(Indirect + R.ResidualSpecular.xyz, 1)',
               'float4(ReplaySplitOutput(Indirect, OutputColour[Pixel].xyz, R.ResidualSpecular.xyz), 1)')
Source += '''
[shader("compute")][numthreads(64,1,1)] void GenerateSplitIdentity(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float Error = 0, IndirectMaximum = 0, WholeMaximum = 0;
    if (R.PositionHit.w != 0 && R.MotionPositionToken.w > 0 && R.ViewMetallic.w == 0)
    {
        uint Bits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
        for (uint Index = 0; Index < 8; ++Index)
        {
            uint Seed = NextRandom(Bits);
            float3 Direct = EvaluateReplayDirect(R, Seed);
            float3 Indirect = EvaluateReplayPath(R, Seed);
            float3 Whole = EvaluateReplayWhole(R, Seed);
            float3 Delta = abs((Direct + Indirect) - Whole);
            Error = max(Error, max(Delta.x, max(Delta.y, Delta.z)));
            IndirectMaximum = max(IndirectMaximum, max(Indirect.x, max(Indirect.y, Indirect.z)));
            WholeMaximum = max(WholeMaximum, max(Whole.x, max(Whole.y, Whole.z)));
        }
    }
    OutputColour[Pixel] = float4(Error, IndirectMaximum, WholeMaximum, 1);
    OutputColour[ReplaySlot(2, Pixel)] = 0;
}
[shader("compute")][numthreads(64,1,1)] void ResolveSplitIdentity(uint3 Dispatch : SV_DispatchThreadID) {}
'''
Destination.write_text(Source)
print('Derived fresh-direct/deeper-indirect split with unchanged first-bounce random consumption')

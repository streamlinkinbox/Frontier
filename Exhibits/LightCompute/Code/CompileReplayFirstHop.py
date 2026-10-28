#============================================================================================================================================
#                                                     COMPILEREPLAYFIRSTHOP.PY
#============================================================================================================================================
# 🧩 Cache current first-hop hits from direct samples for exactly matching indirect seeds; retain an unfused control.

from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
Destination.parent.mkdir(parents=True, exist_ok=True)
Unfused = Destination.parent / 'unfused'
Unfused.mkdir(exist_ok=True)
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReplayIndirectSplit.py',
                str(Unfused / 'ReplayIndirectSplit.slang')], check=True)
Source = (Unfused / 'ReplayIndirectSplit.slang').read_text()

def Once(Text, Previous, Replacement):
    if Text.count(Previous) != 1:
        raise RuntimeError('Review first-hop cache substitution: ' + Previous)
    return Text.replace(Previous, Replacement)

Start = Source.index('float3 EvaluateReplayDirect(')
End = Source.index('float3 ReplayFacetRadiance(', Start)
Direct = Source[Start:End]
CachedDirect = Once(Direct, 'float3 EvaluateReplayDirect(SurfacePoint Receiver, uint Seed)',
                    'float3 EvaluateReplayDirectCached(SurfacePoint Receiver, uint Seed, out uint HitIndex, out float3 HitStrike)')
CachedDirect = Once(CachedDirect, '{\n#ifdef FRONTIER_REPLAY_PATH_BUDGET',
                    '{\n    HitIndex = 0xffffffffu; HitStrike = 0;\n#ifdef FRONTIER_REPLAY_PATH_BUDGET')
CachedDirect = Once(CachedDirect, '        uint Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);',
                    '        uint Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);\n'
                    '        HitIndex = Index;\n        if (Index != 0xffffffffu) HitStrike = Strike;')
DirectWrapper = '''float3 EvaluateReplayDirect(SurfacePoint Receiver, uint Seed)
{
    uint Index; float3 Strike;
    return EvaluateReplayDirectCached(Receiver, Seed, Index, Strike);
}
'''
Source = Once(Source, Direct, CachedDirect + DirectWrapper)
Start = Source.index('float3 EvaluateReplayPath(')
End = Source.index('float ReplayTarget(', Start)
Indirect = Source[Start:End]
CachedIndirect = Once(Indirect, 'float3 EvaluateReplayPath(SurfacePoint Receiver, uint Seed)',
                      'float3 EvaluateReplayIndirectCached(SurfacePoint Receiver, uint Seed, bool Cached, uint HitIndex, float3 HitStrike)')
Previous = '''#ifdef FRONTIER_REPLAY_PATH_BUDGET
        InterlockedAdd(Counters[17], 1);
#endif
        uint Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);'''
Replacement = '''        uint Index;
        if (Bounce == 0 && Cached)
        {
            Index = HitIndex;
            Strike = HitStrike;
        }
        else
        {
#ifdef FRONTIER_REPLAY_PATH_BUDGET
            InterlockedAdd(Counters[17], 1);
#endif
            Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);
        }'''
CachedIndirect = Once(CachedIndirect, Previous, Replacement)
IndirectWrappers = '''float3 EvaluateReplayPath(SurfacePoint Receiver, uint Seed)
{
    return EvaluateReplayIndirectCached(Receiver, Seed, false, 0xffffffffu, float3(0));
}
float3 EvaluateReplayFirstHop(SurfacePoint Receiver, uint Seed, uint Seeds[8], uint Hits[8], float3 Strikes[8], uint Available)
{
    bool Found = false;
    uint Hit = 0xffffffffu;
    float3 Strike = 0;
    for (uint Index = 0; Index < Available; ++Index)
        if (Seeds[Index] == Seed)
        {
            Found = true; Hit = Hits[Index]; Strike = Strikes[Index];
            break;
        }
    return EvaluateReplayIndirectCached(Receiver, Seed, Found, Hit, Strike);
}
'''
Source = Once(Source, Indirect, CachedIndirect + IndirectWrappers)
Source = Once(Source, '    float3 Direct = 0;\n    if (R.PositionHit.w',
               '''    uint DirectSeeds[8], DirectHits[8]; float3 DirectStrikes[8];
    uint AvailableDirect = 0;
    for (uint Index = 0; Index < 8; ++Index)
    {
        DirectSeeds[Index] = 0; DirectHits[Index] = 0xffffffffu; DirectStrikes[Index] = 0;
    }
'''
               '    float3 Direct = 0;\n    if (R.PositionHit.w')
Source = Once(Source, '        Direct = EvaluateReplayDirect(R, DirectSeed);',
               '        AvailableDirect = 8; DirectSeeds[0] = DirectSeed;\n'
               '        Direct = EvaluateReplayDirectCached(R, DirectSeed, DirectHits[0], DirectStrikes[0]);')
Source = Once(Source, '        for (uint Index = 1; Index < 8; ++Index)\n            Direct += EvaluateReplayDirect(R, NextRandom(DirectBits));',
               '''        for (uint Index = 1; Index < 8; ++Index)
        {
            DirectSeeds[Index] = NextRandom(DirectBits);
            Direct += EvaluateReplayDirectCached(R, DirectSeeds[Index], DirectHits[Index], DirectStrikes[Index]);
        }''')
Start = Source.index('void GenerateReplayReservoirs(')
End = Source.index('[shader("compute")][numthreads(64,1,1)] void ResolveReplayReservoirs', Start)
Generation = Source[Start:End]
for Seed in ('FirstSeed', 'NextRandom(SeedBits)', 'Selected', 'Candidate'):
    Previous = 'EvaluateReplayPath(R, ' + Seed + ')'
    if Previous not in Generation:
        raise RuntimeError('Missing generation evaluation: ' + Previous)
    Generation = Generation.replace(Previous, 'EvaluateReplayFirstHop(R, ' + Seed + ', DirectSeeds, DirectHits, DirectStrikes, AvailableDirect)')
Source = Source[:Start] + Generation + Source[End:]
Destination.write_text(Source)
print('Derived current-generation first-hop cache; seeds, radiance evaluation and reservoir arithmetic unchanged')

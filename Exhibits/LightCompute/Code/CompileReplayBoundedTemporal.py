#============================================================================================================================================
#                                                   COMPILEREPLAYBOUNDEDTEMPORAL.PY
#============================================================================================================================================
# 🧩 Derive an isolated bounded-temporal validation shader from the shared engine shader, with fail-closed substitutions.

from pathlib import Path
import sys

Source = Path('Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang').read_text()
Replacements = (
    ('if (Extent.Reconstruction.x != 0 && Extent.History.z != 0 && Extent.History.w == 0 &&',
     'if ((Extent.Primary.w & 1048576u) != 0 && (Extent.Primary.w & 524288u) == 0 &&\n'
     '            Extent.History.z != 0 && Extent.History.w == 0 &&'),
    ('Compatible = Compatible && ReplayHistorySourceValid(Source);',
     'Compatible = Compatible && ReplayHistorySourceValid(Source) && Source.w <= 8;'),
    ('float Retained = min(Source.w, float(max(1u, Extent.Reconstruction.w)));',
     'float Retained = min(Source.w, min(Count, float(max(1u, Extent.Reconstruction.w))));'),
    ('            int2 P = Centre + Offsets[I];',
     '            if (I != 0 && I == 1u + (Pixel & 3u)) continue;\n'
     '            int2 P = Centre + Offsets[I];'),
)
for Previous, Replacement in Replacements:
    if Source.count(Previous) != 1:
        raise RuntimeError('Shared shader changed; review bounded-temporal derivation: ' + Previous)
    Source = Source.replace(Previous, Replacement)
Path(sys.argv[1]).with_name('ReplayBoundedTemporalUncached.slang').write_text(Source)
Caching = (
    ('        float3 Radiance = EvaluateReplayPath(R, Selected);',
     '        uint CurrentSeeds[4]; float3 CurrentRadiances[4];\n'
     '        float3 Radiance = EvaluateReplayPath(R, Selected);\n'
     '        CurrentSeeds[0] = Selected; CurrentRadiances[0] = Radiance;'),
    ('                uint Candidate = DecodeReplaySeed(Encoded);\n'
     '                float3 CandidateRadiance = EvaluateReplayPath(R, Candidate);',
     '                uint Candidate = DecodeReplaySeed(Encoded);\n'
     '                float3 CandidateRadiance = EvaluateReplayPath(R, Candidate);\n'
     '                CurrentSeeds[CandidateIndex + 1] = Candidate;\n'
     '                CurrentRadiances[CandidateIndex + 1] = CandidateRadiance;'),
    ('                    float3 Replayed = EvaluateReplayPath(R, Candidate);',
     '                    float3 Replayed = 0; bool Cached = false;\n'
     '                    for (uint Index = 0; Index < uint(Count); ++Index)\n'
     '                        if (CurrentSeeds[Index] == Candidate)\n'
     '                        { Replayed = CurrentRadiances[Index]; Cached = true; break; }\n'
     '                    if (!Cached) Replayed = EvaluateReplayPath(R, Candidate);\n'
     '                    else InterlockedAdd(Counters[15], 1);'),
)
for Previous, Replacement in Caching:
    if Source.count(Previous) != 1:
        raise RuntimeError('Review current-surface evaluation cache: ' + Previous)
    Source = Source.replace(Previous, Replacement)
Path(sys.argv[1]).write_text(Source)
print('Derived bounded and cached validation shaders; production source and contracts unchanged')

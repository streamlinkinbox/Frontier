"""Generate a test-only fresh-neighbor replay shader without modifying the Engine."""
from pathlib import Path
import sys

Root = Path(__file__).resolve().parents[3]
Source = Root / 'Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang'
Before = '''            float3 Radiance = I == 0 ? OutputColour[ReplaySlot(3, Pixel)].xyz :
                                       EvaluateReplayPath(R, DecodeReplaySeed(Source.xy));
            float Target = ReplayTarget(Radiance), Weight = Target * Source.z * Source.w;
            if (!isfinite(Weight) || !isfinite(Sum + Weight)) continue;
            Sum += Weight; Count += Source.w;
'''
After = '''            // Test-only: spatial donors contribute their first fresh generator seed with unit weight/count.
            uint CandidateSeed = DecodeReplaySeed(Source.xy);
            float SourceNormalization = Source.z, Represented = Source.w;
            if (I != 0)
            {
                uint FreshBits = Extent.Sampling.x ^ (Donor * 747796405u + 2891336453u);
                CandidateSeed = NextRandom(FreshBits);
                SourceNormalization = 1.0;
                Represented = 1.0;
            }
            float3 Radiance = I == 0 ? OutputColour[ReplaySlot(3, Pixel)].xyz :
                                       EvaluateReplayPath(R, CandidateSeed);
            float Target = ReplayTarget(Radiance), Weight = Target * SourceNormalization * Represented;
            if (!isfinite(Weight) || !isfinite(Sum + Weight)) continue;
            Sum += Weight; Count += Represented;
'''
if len(sys.argv) != 2:
    raise SystemExit('Usage: ReplayFreshDonorCandidate.py OUTPUT_PATH')
Destination = Path(sys.argv[1]).resolve()
if Destination == Source.resolve() or Root / 'Frontier' in Destination.parents:
    raise SystemExit('Refusing to modify Engine source')
Text = Source.read_text()
if Text.count(Before) != 1:
    raise SystemExit('Expected exactly one Engine Resolve donor block')
Destination.parent.mkdir(parents=True, exist_ok=True)
Destination.write_text(Text.replace(Before, After, 1))
print('GENERATED test-only fresh-neighbor candidate:', Destination)

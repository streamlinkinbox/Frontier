"""Generate diagnostic-only and raw-history test shaders from the current Engine source."""
from pathlib import Path
import sys

Root = Path(__file__).resolve().parents[3]
Source = Root / 'Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang'
if len(sys.argv) != 3:
    raise SystemExit('Usage: ReplayTemporalFreshHistoryCandidate.py ORIGINAL_TEST_PATH RAW_TEST_PATH')
Original, Raw = (Path(Name).resolve() for Name in sys.argv[1:])
if Original == Raw or any(PathName == Source.resolve() or Root / 'Frontier' in PathName.parents
                          for PathName in (Original, Raw)):
    raise SystemExit('Refusing to overwrite Engine source or overlap destinations')
Text = Source.read_text()
ClearBefore = '''    SurfacePoint R = Receivers[Pixel];
    float4 Packed = 0, SelectedRadiance = 0;
'''
ClearAfter = '''    SurfacePoint R = Receivers[Pixel];
    // Test-only diagnostic slab; this study excludes compact output and other consumers of slot 5.
    OutputColour[ReplaySlot(5, Pixel)] = 0;
    float4 Packed = 0, SelectedRadiance = 0;
'''
AcceptBefore = '''                        TemporalReplayed = true;
                        TemporalSeed = Candidate;
'''
AcceptAfter = '''                        // Preserve the already-computed incoming history contribution for inspection.
                        OutputColour[ReplaySlot(5, Pixel)] = float4(Replayed.x, Source.z, Source.w, ReplayedTarget);
                        TemporalReplayed = true;
                        TemporalSeed = Candidate;
'''
HistoryBefore = '''    OutputColour[ReplaySlot(0, Pixel)] = Local;
'''
HistoryAfter = '''    // Test-only: history is the first unselected fresh seed, not the selected local reservoir.
    float4 FreshHistory = 0;
    if (ReplayReservoirHistoryValid(Local))
    {
        uint FreshBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
        FreshHistory = float4(EncodeReplaySeed(NextRandom(FreshBits)), 1.0, 1.0);
    }
    OutputColour[ReplaySlot(0, Pixel)] = FreshHistory;
'''
for Before in (ClearBefore, AcceptBefore, HistoryBefore):
    if Text.count(Before) != 1:
        raise SystemExit('Expected exactly one Engine source anchor')
Diagnostic = Text.replace(ClearBefore, ClearAfter, 1).replace(AcceptBefore, AcceptAfter, 1)
Original.parent.mkdir(parents=True, exist_ok=True)
Raw.parent.mkdir(parents=True, exist_ok=True)
Original.write_text(Diagnostic)
Raw.write_text(Diagnostic.replace(HistoryBefore, HistoryAfter, 1))
print('GENERATED diagnostic original and raw-history test-only shaders')

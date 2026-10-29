"""Generate test-only baseline and decorrelated path-proposal stream shaders."""
from pathlib import Path
import sys

Root = Path(__file__).resolve().parents[3]
Source = Root / 'Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang'
if len(sys.argv) != 3:
    raise SystemExit('Usage: ReplayTemporalPathStreamCandidate.py BASELINE_PATH DECORRELATED_PATH')
Baseline, Decorrelated = (Path(Name).resolve() for Name in sys.argv[1:])
if Baseline == Decorrelated or any(Root / 'Frontier' in PathName.parents
                                   for PathName in (Baseline, Decorrelated)):
    raise SystemExit('Refusing to overwrite Engine source or overlapping destinations')
Text = Source.read_text()
Receiver = '''    SurfacePoint R = Receivers[Pixel];
    float4 Packed = 0, SelectedRadiance = 0;
'''
ReceiverDiagnostic = '''    SurfacePoint R = Receivers[Pixel];
    // Test-only diagnostics occupy unused noncompact slabs 5 and 6.
    OutputColour[ReplaySlot(5, Pixel)] = 0;
    OutputColour[ReplaySlot(6, Pixel)] = 0;
    float4 Packed = 0, SelectedRadiance = 0;
'''
Fresh = '''        uint Selected = NextRandom(SeedBits);
        float3 Radiance = EvaluateReplayPath(R, Selected);
'''
FreshDiagnostic = '''        uint Selected = NextRandom(SeedBits);
        OutputColour[ReplaySlot(5, Pixel)] = float4(EncodeReplaySeed(Selected), 0, 0);
        float3 Radiance = EvaluateReplayPath(R, Selected);
'''
OriginalPathStream = '''        uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
'''
OriginalSelectionStream = '''        uint SelectionBits = (Extent.Sampling.x * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);
'''
SelectionCapture = OriginalSelectionStream + '''        OutputColour[ReplaySlot(6, Pixel)] = float4(EncodeReplaySeed(SelectionBits), 0, 0);
'''
DecorrelatedPathStream = '''        uint SeedBits = Extent.Sampling.x ^ (Pixel * 0x9e3779b9u) ^ 0x6a09e667u;
        SeedBits ^= SeedBits >> 16u;
        SeedBits *= 0x7feb352du;
        SeedBits ^= SeedBits >> 15u;
        SeedBits *= 0x846ca68bu;
        SeedBits ^= SeedBits >> 16u;
'''
for Anchor in (Receiver, Fresh, OriginalPathStream, OriginalSelectionStream):
    if Text.count(Anchor) != 1:
        raise SystemExit('Expected exactly one Engine source anchor')
BaselineText = Text.replace(Receiver, ReceiverDiagnostic, 1).replace(Fresh, FreshDiagnostic, 1)
BaselineText = BaselineText.replace(OriginalSelectionStream, SelectionCapture, 1)
DecorrelatedText = BaselineText.replace(OriginalPathStream, DecorrelatedPathStream, 1)
if DecorrelatedText.replace(DecorrelatedPathStream, OriginalPathStream, 1) != BaselineText:
    raise SystemExit('Path-stream shader diff escaped its single initialization block')
if BaselineText.count(OriginalSelectionStream) != 1:
    raise SystemExit('Selection-stream source did not remain unchanged')
for PathName, Content in ((Baseline, BaselineText), (Decorrelated, DecorrelatedText)):
    PathName.parent.mkdir(parents=True, exist_ok=True)
    PathName.write_text(Content)
print('GENERATED path-stream baseline/candidate with fixed selection stream and readbacks')

"""Generate instrumented baseline and decorrelated-selection test shaders from Engine source."""
from pathlib import Path
import sys

Root = Path(__file__).resolve().parents[3]
Source = Root / 'Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang'
if len(sys.argv) != 3:
    raise SystemExit('Usage: ReplayTemporalSelectionStreamCandidate.py BASELINE_PATH DECORRELATED_PATH')
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
OriginalStream = '''        uint SelectionBits = (Extent.Sampling.x * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);
'''
OriginalCapture = OriginalStream + '''        OutputColour[ReplaySlot(6, Pixel)] = float4(EncodeReplaySeed(SelectionBits), 0, 0);
'''
DecorrelatedStream = '''        uint SelectionBits = Extent.Sampling.x ^ (Pixel * 0x9e3779b9u) ^ 0x6a09e667u;
        SelectionBits ^= SelectionBits >> 16u;
        SelectionBits *= 0x7feb352du;
        SelectionBits ^= SelectionBits >> 15u;
        SelectionBits *= 0x846ca68bu;
        SelectionBits ^= SelectionBits >> 16u;
        OutputColour[ReplaySlot(6, Pixel)] = float4(EncodeReplaySeed(SelectionBits), 0, 0);
'''
for Anchor in (Receiver, Fresh, OriginalStream):
    if Text.count(Anchor) != 1:
        raise SystemExit('Expected exactly one Engine source anchor')
BaselineText = Text.replace(Receiver, ReceiverDiagnostic, 1).replace(Fresh, FreshDiagnostic, 1)
BaselineText = BaselineText.replace(OriginalStream, OriginalCapture, 1)
DecorrelatedText = BaselineText.replace(OriginalCapture, DecorrelatedStream, 1)
if DecorrelatedText.replace(DecorrelatedStream, OriginalCapture, 1) != BaselineText:
    raise SystemExit('Selection-stream shader diff escaped its single initialization block')
if BaselineText.count('uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);') != 1:
    raise SystemExit('Expected path-proposal seed stream was not preserved')
for PathName, Content in ((Baseline, BaselineText), (Decorrelated, DecorrelatedText)):
    PathName.parent.mkdir(parents=True, exist_ok=True)
    PathName.write_text(Content)
print('GENERATED baseline/decorrelated test shaders with path and preselection readbacks')

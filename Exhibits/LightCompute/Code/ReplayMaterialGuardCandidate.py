"""Generate the single-change test-only shader; never overwrite the Engine source."""
from pathlib import Path
import sys

Root = Path(__file__).resolve().parents[3]
Source = Root / 'Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang'
Anchor = ('    if (AuthoredMaterials && MaterialTransport(Receiver) != MaterialTransport(DonorReceiver))\n'
          '        return false;\n')
Addition = ('    // Test-only conservative material boundary; not an Engine default.\n'
            '    if (AuthoredMaterials && MaterialAddress(Receiver) != MaterialAddress(DonorReceiver))\n'
            '        return false;\n')
if len(sys.argv) != 2:
    raise SystemExit('Usage: ReplayMaterialGuardCandidate.py OUTPUT_PATH')
Destination = Path(sys.argv[1]).resolve()
if Destination == Source.resolve() or Root / 'Frontier' in Destination.parents:
    raise SystemExit('Refusing to modify Engine source')
Text = Source.read_text()
if Text.count(Anchor) != 1:
    raise SystemExit('Expected exactly one Engine compatibility anchor')
Destination.parent.mkdir(parents=True, exist_ok=True)
Destination.write_text(Text.replace(Anchor, Anchor + Addition, 1))
print('GENERATED test-only material-boundary candidate:', Destination)

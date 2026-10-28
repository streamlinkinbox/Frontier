#============================================================================================================================================
#                                                 REPLAYTEMPORALCONTRACTPARITY.PY
#============================================================================================================================================
# 🧩 Verify final temporal result/capture parity and the retained pre-repair shader negative control.

from pathlib import Path
import hashlib
import re
from PIL import Image

Logs = [Path('Exhibits/LightCompute/Results/ReplayTemporalContract-' + Mode + '.txt').read_text()
        for Mode in ('release', 'sanitized')]
Rows = []
Checks = []
for Mode, Text in zip(('release', 'sanitized'), Logs):
    assert 'PASS temporal GPU geometry' in Text and 'TEMPORAL_MAX_COUNT' in Text
    assert '100% tests passed out of 5' in Text and 'PASS temporal finite-measure identities' in Text
    Rows.append([Line for Line in Text.splitlines() if Line.startswith('TEMPORAL_')])
    Digests = [Line.split('  ', 1) for Line in Text.splitlines() if re.match(r'^[0-9a-f]{64}  ', Line)]
    for Digest, Name in Digests:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Checks.append(f'{Mode}: {len(Digests)} logged source/tool/shader digests match final files')
assert Rows[0] == Rows[1]
Checks.append(f'{len(Rows[0])} TEMPORAL result rows exact between release and host sanitizer')
Prior = Path('Exhibits/LightCompute/Results/ReplayCompactStorage-release.txt').read_text()
PriorHashes = {Path(Name).name: Digest for Digest, Name in
               (Line.split('  ', 1) for Line in Prior.splitlines() if re.match(r'^[0-9a-f]{64}  ', Line))}
for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory', 'ReplayFinalizeSampleHistory'):
    Current = Path('_ClaudeScratch/build/replay-temporal-contract-release/unchecked') / (Stem + '.spv')
    assert hashlib.sha256(Current.read_bytes()).hexdigest() == PriorHashes[Stem + '.spv']
    Checks.append(Stem + ': unchecked-control SHA256 matches pre-repair compact checkpoint')
for Name in ('TemporalCurrent', 'TemporalSourceOff'):
    Captures = []
    for Mode in ('release', 'sanitized'):
        with Image.open(Path('_ClaudeScratch/replay-temporal-contract-capture-' + Mode + '/png') / (Name + '.png')) as Capture:
            Captures.append(Capture.convert('RGB').tobytes())
    assert Captures[0] == Captures[1]
    if Name == 'TemporalSourceOff':
        assert not any(Captures[0])
    Checks.append(Name + ': decoded RGB exact ' + hashlib.sha256(Captures[0]).hexdigest())
Checks.append('PASS temporal result, final source, unchecked negative-control and actual capture parity')
Path('Exhibits/LightCompute/Results/ReplayTemporalContract-parity.txt').write_text('\n'.join(Checks) + '\n')
print('\n'.join(Checks))

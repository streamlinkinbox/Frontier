#============================================================================================================================================
#                                                  REPLAYBOUNDEDTEMPORALPARITY.PY
#============================================================================================================================================
# 🧩 Verify bounded temporal quality parity, evaluation-cache invariance and unchanged production shader digests.

from pathlib import Path
import hashlib
import re
from PIL import Image

Directory = Path('Exhibits/LightCompute/Results')
Logs = [(Directory / ('ReplayBoundedTemporal-' + Mode + '.txt')).read_text() for Mode in ('release', 'sanitized')]
Checks = []
Rows = lambda Text: [Line for Line in Text.splitlines() if Line.startswith('BOUNDED_')]
assert Rows(Logs[0]) == Rows(Logs[1])
for Mode, Text in zip(('release', 'sanitized'), Logs):
    assert 'PASS bounded eight-current-path comparison' in Text and '100% tests passed out of 5' in Text
    assert 'BOUNDED_CACHE_PARITY frames=18 output_and_nine_slabs_exact=1' in Text
    Entries = [Line.split('  ', 1) for Line in Text.splitlines() if re.match(r'^[0-9a-f]{64}  ', Line)]
    for Digest, Name in Entries:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Checks.append(f'{Mode}: all {len(Entries)} logged digests match final files')
Checks.append(f'{len(Rows(Logs[0]))} bounded result rows exact across configurations')
Prefixes = ('BOUNDED_REFERENCE ', 'BOUNDED_RESULT ', 'BOUNDED_PAIRED ', 'BOUNDED_OVERLAP ')
for Mode, Text in zip(('release', 'sanitized'), Logs):
    Prior = (Directory / ('ReplayBoundedTemporal-uncached-' + Mode + '.txt')).read_text()
    Numeric = lambda Content: [Line for Line in Content.splitlines() if Line.startswith(Prefixes)]
    assert Numeric(Prior) == Numeric(Text)
    Checks.append(f'{Mode}: all {len(Numeric(Text))} reference/quality/paired/overlap rows exact to uncached run')
    for Stem in ('ReplayGenerate', 'ReplayResolve'):
        Expected = next(Line.split('  ', 1)[0] for Line in Prior.splitlines()
                        if re.match(r'^[0-9a-f]{64}  ', Line) and Line.endswith('/' + Stem + '.spv') and '/plain/' not in Line)
        File = Path('_ClaudeScratch/build/replay-bounded-temporal-' + Mode + '/uncached') / (Stem + '.spv')
        assert hashlib.sha256(File.read_bytes()).hexdigest() == Expected
    Checks.append(f'{Mode}: retained uncached shader digests equal original uncached experiment')
Cold = [(Directory / ('ReplayBoundedTemporal-cold-' + Mode + '.txt')).read_text() for Mode in ('release', 'sanitized')]
assert Rows(Cold[0]) == Rows(Cold[1])
Checks.append('Three-warm-up control result rows exact across configurations; earlier harness hashes remain historical')
Prior = (Directory / 'ReplayTemporalContract-release.txt').read_text()
PriorHashes = {Path(Name).name: Digest for Digest, Name in
               (Line.split('  ', 1) for Line in Prior.splitlines() if re.match(r'^[0-9a-f]{64}  ', Line))
               if '/unchecked/' not in Name}
for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory', 'ReplayFinalizeSampleHistory'):
    File = Path('_ClaudeScratch/build/replay-bounded-temporal-release/plain') / (Stem + '.spv')
    assert hashlib.sha256(File.read_bytes()).hexdigest() == PriorHashes[Stem + '.spv']
Checks.append('All five unmodified production shader digests match the temporal-contract checkpoint')
for Name in ('BoundedFreshCosine','BoundedReuseCosine','BoundedMaskCosine','BoundedFreshFacet',
             'BoundedReuseFacet','BoundedMaskFacet','BoundedOffCosine','BoundedOffFacet'):
    Captures = []
    for Mode in ('release', 'sanitized'):
        with Image.open(Path('_ClaudeScratch/replay-bounded-temporal-capture-' + Mode + '/png') / (Name + '.png')) as Capture:
            Captures.append(Capture.convert('RGB').tobytes())
    assert Captures[0] == Captures[1]
    with Image.open(Path('Exhibits/LightCompute/Images') / (Name + '.png')) as Retained:
        assert Retained.convert('RGB').tobytes() == Captures[0]
    for Text in Logs:
        assert Name + '.png 64x64 RGB_SHA256=' + hashlib.sha256(Captures[0]).hexdigest() in Text
    if 'Off' in Name:
        assert not any(Captures[0])
    Checks.append(Name + ': decoded RGB exact ' + hashlib.sha256(Captures[0]).hexdigest())
import subprocess
import tempfile
with tempfile.TemporaryDirectory(dir='_ClaudeScratch') as Temporary:
    Derived = Path(Temporary) / 'ReplayBoundedTemporal.slang'
    subprocess.run(['python3', 'Exhibits/LightCompute/Code/CompileReplayBoundedTemporal.py', str(Derived)], check=True)
    for Mode in ('release','sanitized'):
        for Name in ('ReplayBoundedTemporal.slang','ReplayBoundedTemporalUncached.slang'):
            assert (Derived.parent / Name).read_bytes() == (Path('_ClaudeScratch/build/replay-bounded-temporal-' + Mode) / Name).read_bytes()
Checks.append('Both derived shader sources reproduce exactly from final authoring script in both builds')
for Name in ('CompileReplayBoundedTemporal.py','ReplayBoundedTemporalMeasure.cpp',
             'ReplayBoundedTemporalImages.py','ReplayBoundedTemporalParity.py'):
    File=Path('Exhibits/LightCompute/Code') / Name
    Checks.append('Post-run authoring/presenter/audit source ' + hashlib.sha256(File.read_bytes()).hexdigest() + '  ' + str(File))
Checks.append('PASS bounded temporal cache, quality, source and production-inactive parity')
(Directory / 'ReplayBoundedTemporal-parity.txt').write_text('\n'.join(Checks) + '\n')
print('\n'.join(Checks))

#============================================================================================================================================
#                                                   REPLAYTEMPORALHOLDOUTPARITY.PY
#============================================================================================================================================
# 🧩 Audit holdout source snapshots, independent result parity, unchanged shaders and all retained first-batch captures.

from pathlib import Path
import hashlib
import re
from PIL import Image

Results = Path('Exhibits/LightCompute/Results')
Logs = [(Results / ('ReplayTemporalHoldout-' + Mode + '.txt')).read_text() for Mode in ('release', 'sanitized')]
Checks = []
Semantic = lambda Text: [Line for Line in Text.splitlines() if Line.startswith(('HOLDOUT_', 'BOUNDED_', 'BUDGET_'))]
assert Semantic(Logs[0]) == Semantic(Logs[1])
Checks.append(f'{len(Semantic(Logs[0]))} semantic rows exact across release and host sanitizers')
for Mode, Text in zip(('release', 'sanitized'), Logs):
    assert '100% tests passed out of 5' in Text
    assert 'BOUNDED_CACHE_PARITY frames=18 output_and_nine_slabs_exact=1' in Text
    assert 'HOLDOUT_DECISION correctness=1 ' in Text
    Entries = [Line.split('  ', 1) for Line in Text.splitlines() if re.match(r'^[0-9a-f]{64}  ', Line)]
    for Digest, Name in Entries:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Checks.append(f'{Mode}: all {len(Entries)} logged source/tool/derived-shader/binary digests match final files')
Prior = (Results / 'ReplayBoundedTemporal-release.txt').read_text()
PriorShaders = {Path(Name).name + ('_plain' if '/plain/' in Name else ''): Digest
                for Digest, Name in (Line.split('  ', 1) for Line in Prior.splitlines()
                                     if re.match(r'^[0-9a-f]{64}  ', Line)) if Name.endswith('.spv')}
Uncached = (Results / 'ReplayBoundedTemporal-uncached-release.txt').read_text()
for Mode in ('release', 'sanitized'):
    Build = Path('_ClaudeScratch/build/replay-temporal-holdout-' + Mode)
    for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory',
                 'ReplayFinalizeSampleHistory', 'ReplayFreshBudget'):
        assert hashlib.sha256((Build / (Stem + '.spv')).read_bytes()).hexdigest() == PriorShaders[Stem + '.spv']
    for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory', 'ReplayFinalizeSampleHistory'):
        assert hashlib.sha256((Build / 'plain' / (Stem + '.spv')).read_bytes()).hexdigest() == PriorShaders[Stem + '.spv_plain']
    for Stem in ('ReplayGenerate', 'ReplayResolve'):
        Expected = next(Line.split('  ', 1)[0] for Line in Uncached.splitlines()
                        if re.match(r'^[0-9a-f]{64}  ', Line) and Line.endswith('/' + Stem + '.spv') and '/plain/' not in Line)
        assert hashlib.sha256((Build / 'uncached' / (Stem + '.spv')).read_bytes()).hexdigest() == Expected
    Checks.append(Mode + ': all 13 prototype/control/default shader digests unchanged from preceding phase')
for Condition in range(4):
    for Suffix in ('Fresh', 'Temporal', 'Mask'):
        Name = f'Holdout{Condition}{Suffix}'
        Captures = []
        for Mode in ('release', 'sanitized'):
            with Image.open(Path('_ClaudeScratch/replay-temporal-holdout-capture-' + Mode) / (Name + '.ppm')) as Capture:
                assert Capture.size == (32, 32)
                Captures.append(Capture.convert('RGB').tobytes())
        with Image.open(Path('Exhibits/LightCompute/Images') / (Name + '.png')) as Retained:
            assert Retained.convert('RGB').tobytes() == Captures[0] == Captures[1]
        Checks.append(Name + ': decoded RGB exact ' + hashlib.sha256(Captures[0]).hexdigest())
for Name in ('ReplayTemporalHoldoutImages.py', 'ReplayTemporalHoldoutParity.py'):
    Source = Path('Exhibits/LightCompute/Code') / Name
    Checks.append('Post-run presentation/audit source ' + hashlib.sha256(Source.read_bytes()).hexdigest() + '  ' + str(Source))
Checks.append('Previous bounded-phase main.cpp digest is historical after adding this holdout CLI; shader digests remain unchanged')
Checks.append('PASS emitter-enabled holdout correctness, source, shader and capture parity; see HOLDOUT_DECISION for quality')
(Results / 'ReplayTemporalHoldout-parity.txt').write_text('\n'.join(Checks) + '\n')
print('\n'.join(Checks))

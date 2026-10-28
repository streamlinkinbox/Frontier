#============================================================================================================================================
#                                                    REPLAYRESETBUDGETPARITY.PY
#============================================================================================================================================
# 🧩 Audit reset parity, retained captures, reproduced shader derivation and unchanged preceding/default shaders.

from pathlib import Path
import hashlib
import re
import subprocess
import tempfile
from PIL import Image

Directory = Path('Exhibits/LightCompute/Results')
Logs = [(Directory / ('ReplayResetBudget-' + Mode + '.txt')).read_text() for Mode in ('release', 'sanitized')]
Rows = lambda Text: [Line for Line in Text.splitlines() if Line.startswith('RESET_')]
assert Rows(Logs[0]) == Rows(Logs[1])
Checks = [f'{len(Rows(Logs[0]))} reset semantic rows exact across release and host sanitizers']
ExpectedParity = ('RESET_PARITY fresh8_exact=64 extra_reset_exact=4 nonreset_output_and_nine_slabs_exact=225 '
                  'persistent_eight_slabs_exact_all=1')
for Mode, Text in zip(('release', 'sanitized'), Logs):
    assert '100% tests passed out of 5' in Text
    assert 'PASS metadata-only fresh reset budget' in Text and ExpectedParity in Text
    Entries = [Line.split('  ', 1) for Line in Text.splitlines() if re.match(r'^[0-9a-f]{64}  ', Line)]
    for Digest, Name in Entries:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Checks.append(f'{Mode}: all {len(Entries)} logged source/tool/derived-source/shader/binary digests match final files')
Work = dict(re.findall(r'(\w+)=([^ ]+)', next(Line for Line in Logs[0].splitlines() if Line.startswith('RESET_WORK '))))
assert int(Work['fixed_current']) == 262144 and int(Work['previous_current']) == 128512
assert int(Work['fixed_warm']) - int(Work['previous_warm']) == 133632
Checks.append('Restored current budget costs 133632 additional path calls; initial warm-up resets also cost 133632 extra')
Prior = (Directory / 'ReplayTemporalHoldout-release.txt').read_text()
PriorShaders = {Path(Name).name + ('_plain' if '/plain/' in Name else ''): Digest
                for Digest, Name in (Line.split('  ', 1) for Line in Prior.splitlines()
                                     if re.match(r'^[0-9a-f]{64}  ', Line)) if Name.endswith('.spv') and '/uncached/' not in Name}
for Mode in ('release', 'sanitized'):
    Build = Path('_ClaudeScratch/build/replay-reset-budget-' + Mode)
    for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory',
                 'ReplayFinalizeSampleHistory', 'ReplayFreshBudget'):
        assert hashlib.sha256((Build / 'previous' / (Stem + '.spv')).read_bytes()).hexdigest() == PriorShaders[Stem + '.spv']
    for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory', 'ReplayFinalizeSampleHistory'):
        assert hashlib.sha256((Build / 'plain' / (Stem + '.spv')).read_bytes()).hexdigest() == PriorShaders[Stem + '.spv_plain']
    for Stem in ('Primary', 'ReplayGatherSampleHistory', 'ReplayFinalizeSampleHistory', 'ReplayFreshBudget'):
        assert (Build / (Stem + '.spv')).read_bytes() == (Build / 'previous' / (Stem + '.spv')).read_bytes()
    for Stem in ('ReplayGenerate', 'ReplayResolve'):
        assert (Build / (Stem + '.spv')).read_bytes() != (Build / 'previous' / (Stem + '.spv')).read_bytes()
    Checks.append(Mode + ': six previous/control and five production-default shaders unchanged; only prototype Generate/Resolve differ')
with tempfile.TemporaryDirectory(dir='_ClaudeScratch') as Temporary:
    Derived = Path(Temporary) / 'ReplayResetBudget.slang'
    subprocess.run(['python3', 'Exhibits/LightCompute/Code/CompileReplayResetBudget.py', str(Derived)], check=True)
    for Mode in ('release', 'sanitized'):
        assert Derived.read_bytes() == (Path('_ClaudeScratch/build/replay-reset-budget-' + Mode) / Derived.name).read_bytes()
Checks.append('Final reset shader reproduces exactly from fail-closed authoring scripts in both builds')
for Name in ('ResetPrevious', 'ResetFixed', 'ResetFresh', 'ResetMask'):
    Captures = []
    for Mode in ('release', 'sanitized'):
        with Image.open(Path('_ClaudeScratch/replay-reset-budget-capture-' + Mode) / (Name + '.ppm')) as Capture:
            assert Capture.size == (32, 32)
            Captures.append(Capture.convert('RGB').tobytes())
    with Image.open(Path('Exhibits/LightCompute/Images') / (Name + '.png')) as Retained:
        assert Retained.convert('RGB').tobytes() == Captures[0] == Captures[1]
    Checks.append(Name + ': decoded RGB exact ' + hashlib.sha256(Captures[0]).hexdigest())
for Name in ('ReplayResetBudgetImages.py', 'ReplayResetBudgetParity.py'):
    Source = Path('Exhibits/LightCompute/Code') / Name
    Checks.append('Post-run presentation/audit source ' + hashlib.sha256(Source.read_bytes()).hexdigest() + '  ' + str(Source))
Checks.append('Prior main.cpp source digest is historical after adding the reset CLI; earlier numerical evidence is retained')
Checks.append('PASS fresh reset budget, unchanged continuation, source, shader and capture parity')
(Directory / 'ReplayResetBudget-parity.txt').write_text('\n'.join(Checks) + '\n')
print('\n'.join(Checks))

#============================================================================================================================================
#                                                  REPLAYINDIRECTSPLITPARITY.PY
#============================================================================================================================================
# 🧩 Audit decomposition, direct isolation, separately counted work, default shaders and retained split captures.

from pathlib import Path
import hashlib
import re
import subprocess
import tempfile
from PIL import Image

Results = Path('Exhibits/LightCompute/Results')
Logs = [(Results / ('ReplayIndirectSplit-' + Mode + '.txt')).read_text() for Mode in ('release', 'sanitized')]
Semantic = lambda Text: [Line for Line in Text.splitlines() if Line.startswith('SPLIT_')]
assert Semantic(Logs[0]) == Semantic(Logs[1])
Checks = [f'{len(Semantic(Logs[0]))} split semantic rows exact across release and host ASan/UBSan']
for Mode, Text in zip(('release', 'sanitized'), Logs):
    assert '100% tests passed out of 5' in Text and 'SPLIT_DECISION correctness=1 ' in Text
    for Condition in range(2):
        assert f'SPLIT_DIRECT_PARITY condition={Condition} all_pixels_exact=1 indirect_nine_slabs_exact=1' in Text
        assert f'SPLIT_RESET condition={Condition} source_off_black=1' in Text
    Entries = [Line.split('  ', 1) for Line in Text.splitlines() if re.match(r'^[0-9a-f]{64}  ', Line)]
    for Digest, Name in Entries:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Checks.append(f'{Mode}: all {len(Entries)} logged source/tool/derived-source/shader/binary hashes match final files')
Work = {}
for Line in Logs[0].splitlines():
    if Line.startswith('SPLIT_WORK '):
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        Work[(int(Fields['condition']), Fields['method'])] = Fields
for Condition in range(2):
    Whole = Work[(Condition, 'whole_fresh')]
    Fresh = Work[(Condition, 'split_fresh')]
    Reused = Work[(Condition, 'split_reuse')]
    for Row in (Whole, Fresh, Reused):
        assert int(Row['transport_queries']) == sum(int(Row[Name]) for Name in
                                                   ('direct_nearest', 'direct_shadow', 'replay_nearest', 'replay_shadow'))
    assert Fresh['indirect_or_whole_calls'] == Whole['indirect_or_whole_calls']
    assert Fresh['direct_calls'] == Reused['direct_calls'] == Whole['indirect_or_whole_calls']
    assert Fresh['replay_nearest'] == Whole['replay_nearest']
    assert int(Fresh['direct_shadow']) + int(Fresh['replay_shadow']) == int(Whole['replay_shadow'])
    assert int(Fresh['transport_queries']) - int(Whole['transport_queries']) == int(Fresh['direct_nearest'])
    Checks.append(f'Condition {Condition}: query sums exact; matched split adds exactly its separately counted direct closest-hit queries')
Prior = (Results / 'ReplayResetBudget-release.txt').read_text()
PriorShaders = {Path(Name).name + ('_plain' if '/plain/' in Name else ''): Digest
                for Digest, Name in (Line.split('  ', 1) for Line in Prior.splitlines()
                                     if re.match(r'^[0-9a-f]{64}  ', Line))
                if Name.endswith('.spv') and '/previous/' not in Name and '/uncached/' not in Name}
for Mode in ('release', 'sanitized'):
    Build = Path('_ClaudeScratch/build/replay-indirect-split-' + Mode)
    for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory',
                 'ReplayFinalizeSampleHistory', 'ReplayFreshBudget'):
        assert hashlib.sha256((Build / 'whole' / (Stem + '.spv')).read_bytes()).hexdigest() == PriorShaders[Stem + '.spv']
    for Stem in ('Primary', 'ReplayGenerate', 'ReplayResolve', 'ReplayGatherSampleHistory', 'ReplayFinalizeSampleHistory'):
        assert hashlib.sha256((Build / 'plain' / (Stem + '.spv')).read_bytes()).hexdigest() == PriorShaders[Stem + '.spv_plain']
    for Stem in ('Primary', 'ReplayGatherSampleHistory', 'ReplayFinalizeSampleHistory', 'ReplayFreshBudget'):
        assert (Build / (Stem + '.spv')).read_bytes() == (Build / 'whole' / (Stem + '.spv')).read_bytes()
    Checks.append(Mode + ': six whole-control and five production-default shaders unchanged; primary/gather/finalizer/fresh control identical')
with tempfile.TemporaryDirectory(dir='_ClaudeScratch') as Temporary:
    Derived = Path(Temporary) / 'ReplayIndirectSplit.slang'
    subprocess.run(['python3', 'Exhibits/LightCompute/Code/CompileReplayIndirectSplit.py', str(Derived)], check=True)
    for Mode in ('release', 'sanitized'):
        assert Derived.read_bytes() == (Path('_ClaudeScratch/build/replay-indirect-split-' + Mode) / Derived.name).read_bytes()
Checks.append('Final split shader reproduces exactly from the fail-closed authoring chain')
for Condition in range(2):
    for Suffix in ('Whole', 'Fresh', 'Reuse', 'Mask'):
        Name = f'Split{Condition}{Suffix}'
        Captures = []
        for Mode in ('release', 'sanitized'):
            with Image.open(Path('_ClaudeScratch/replay-indirect-split-capture-' + Mode) / (Name + '.ppm')) as Capture:
                assert Capture.size == ((32, 32) if Condition == 0 else (24, 24))
                Captures.append(Capture.convert('RGB').tobytes())
        with Image.open(Path('Exhibits/LightCompute/Images') / (Name + '.png')) as Retained:
            assert Retained.convert('RGB').tobytes() == Captures[0] == Captures[1]
        Checks.append(Name + ': decoded RGB exact ' + hashlib.sha256(Captures[0]).hexdigest())
for Name in ('ReplayIndirectSplitImages.py', 'ReplayIndirectSplitParity.py'):
    Source = Path('Exhibits/LightCompute/Code') / Name
    Checks.append('Post-run presentation/audit source ' + hashlib.sha256(Source.read_bytes()).hexdigest() + '  ' + str(Source))
Checks.append('Previous main.cpp source digest is historical after adding the split CLI; old numerical evidence is retained')
Checks.append('PASS direct/indirect split source, control, work and capture parity; see SPLIT_DECISION for quality')
(Results / 'ReplayIndirectSplit-parity.txt').write_text('\n'.join(Checks) + '\n')
print('\n'.join(Checks))

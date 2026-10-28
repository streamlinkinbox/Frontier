# 🧩 Verify power-reuse shader rederivation, work accounting, logged artifacts and cross-build evidence.
from pathlib import Path
import hashlib
import re
import subprocess
import sys
import tempfile

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Rows, Counts = [], []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'ReplayPowerReuse-{Mode}.txt').read_text()
    Semantic = [Line for Line in Text.splitlines() if Line.startswith('POWER_REUSE_')]
    assert Semantic[-1] == 'POWER_REUSE_DECISION correctness=0 promote=0 physical_gpu_timing=0'
    assert '100% tests passed out of 5' in Text
    assert 'Indirect split failed its fixed mean gates' in Text
    Matches = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Matches:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Rows.append(Semantic)
    Counts.append(len(Matches))
    Work = {}
    FailedMeans = []
    for Line in Semantic:
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('POWER_REUSE_EQUIVALENCE '):
            assert int(Fields['submissions']) == 36 and int(Fields['saved_closest_queries']) > 0
        if Line.startswith('POWER_REUSE_RESULT ') and Fields['mean_gate'] == '0':
            FailedMeans.append((Fields['condition'], Fields['method']))
        if Line.startswith('POWER_REUSE_WORK '):
            Total = sum(int(Fields[Name]) for Name in ('direct_nearest', 'direct_shadow', 'replay_nearest', 'replay_shadow'))
            assert Total == int(Fields['transport_queries'])
            Work[int(Fields['condition']), Fields['method']] = Total
    assert FailedMeans == [('1', 'whole_no_mis')]
    assert len(Work) == 8
    for Condition in range(2):
        assert Work[Condition, 'whole_power'] == Work[Condition, 'fused_fresh']
    Build = Root / f'_ClaudeScratch/build/replay-power-reuse-{Mode}'
    with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
        subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileReplayPowerReuse.py'),
                        str(Path(Temporary) / 'ReplayPowerReuse.slang')], cwd=Root, check=True,
                       stdout=subprocess.DEVNULL)
        for Name in ('ReplayPowerReuse.slang', 'unfused/ReplayIndirectSplit.slang',
                     'unfused/whole/ReplayResetBudget.slang', 'ReplayFreshPower.slang'):
            assert (Path(Temporary) / Name).read_bytes() == (Build / Name).read_bytes(), Name
assert Rows[0] == Rows[1], 'Host semantic results differ'
for Condition in range(2):
    for Suffix in ('NoMIS', 'Whole', 'Fresh', 'Reuse', 'Mask'):
        Name = f'Split{Condition}{Suffix}.ppm'
        A, B = (Root / f'_ClaudeScratch/replay-power-reuse-capture-{Mode}' / Name for Mode in ('release', 'sanitized'))
        assert A.read_bytes() == B.read_bytes(), Name
Message = (f'PASS rejection-evidence parity (benchmark correctness=0, promote=0): {len(Rows[0])} identical semantic rows, logged hashes {Counts}, ten exact RGB captures; '
           'both 5 CTests passed; four shader sources rederived per build; matched fresh work and component sums exact.\n')
(Results / 'ReplayPowerReuse-parity.txt').write_text(Message)
print(Message, end='')

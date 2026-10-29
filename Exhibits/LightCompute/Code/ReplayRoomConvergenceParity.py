# 🧩 Audit cohort arithmetic, unchanged historical prefixes, pinned artifacts and host-build agreement.
from pathlib import Path
import hashlib
import math
import re

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Rows, HashCounts = [], []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'ReplayRoomConvergence-{Mode}.txt').read_text()
    Semantic = [Line for Line in Text.splitlines() if Line.startswith('CONVERGENCE_')]
    assert Semantic[-1] == 'CONVERGENCE_DECISION agreement=1 prior_rejection_preserved=1 promote=0 physical_gpu_timing=0'
    assert '100% tests passed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Matches = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Matches:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Matches))
    Cpu, Gpu, Summary = {}, {}, {}
    for Line in Semantic:
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        Tag = Line.split()[0]
        if Tag == 'CONVERGENCE_AGREEMENT':
            assert Fields['gate'] == '1'
        if Tag in ('CONVERGENCE_CPU_BATCH', 'CONVERGENCE_GPU_BATCH'):
            Key = (int(Fields['bounces']), Fields['method'])
            (Cpu if Tag == 'CONVERGENCE_CPU_BATCH' else Gpu).setdefault(Key, []).append(float(Fields['mean']))
        if Tag == 'CONVERGENCE_GPU':
            Summary[int(Fields['bounces']), Fields['method'], int(Fields['batches'])] = float(Fields['mean'])
        if Tag in ('CONVERGENCE_CPU', 'CONVERGENCE_GPU'):
            Cohorts = (Cpu if Tag == 'CONVERGENCE_CPU' else Gpu)[int(Fields['bounces']), Fields['method']]
            if Tag == 'CONVERGENCE_CPU':
                assert len(Cohorts) == 64 and Fields['paths'] == '2162688'
            else:
                assert len(Cohorts) == int(Fields['batches'])
            Mean = sum(Cohorts) / len(Cohorts)
            SE = math.sqrt(sum((Number - Mean) ** 2 for Number in Cohorts) / (len(Cohorts) * (len(Cohorts) - 1)))
            assert abs(Mean - float(Fields['mean'])) < 6e-10
            assert abs(SE - float(Fields['nominal_se'])) < 6e-11
    assert len(Cpu) == 6 and len(Gpu) == 4
    assert all(len(Cohorts) == 256 for Cohorts in Gpu.values())
    Prior = (Results / 'ReplayPowerReuse-release.txt').read_text()
    for Method, Name in (('nee', 'whole_no_mis'), ('power', 'whole_power')):
        Pattern = r'^POWER_REUSE_RESULT condition=1 method=' + Name + r' .*?mean=([^ ]+)'
        Expected = float(re.search(Pattern, Prior, re.M).group(1))
        assert Summary[4, Method, 32] == Expected, 'Historical 32-batch prefix changed'
    Rows.append(Semantic)
assert Rows[0] == Rows[1], 'Release/sanitizer diagnostic rows differ'
for Depth in (1, 4):
    for Method in ('Nee', 'Power'):
        Name = f'Room{Depth}{Method}.ppm'
        A, B = (Root / f'_ClaudeScratch/replay-room-convergence-capture-{Mode}' / Name for Mode in ('release', 'sanitized'))
        assert A.read_bytes() == B.read_bytes(), Name
Message = (f'PASS room convergence evidence: {len(Rows[0])} identical semantic rows; logged artifact hashes {HashCounts}; '
           'all cohort means/SE rederived; 32-batch historical means exact; four captures exact; both five-CTest sets passed. '
           'Prior rejection preserved, promote=0.\n')
(Results / 'ReplayRoomConvergence-parity.txt').write_text(Message)
print(Message, end='')

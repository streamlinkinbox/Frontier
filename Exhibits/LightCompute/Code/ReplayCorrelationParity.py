# 🧩 Verify actual Fresh6 budgets, covariance identities, control activation and cross-build artifact/readback parity.
from pathlib import Path
import hashlib
import math
import re
import subprocess
import sys
import tempfile
from PIL import Image

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Rows, HashCounts = [], []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'ReplayCorrelation-{Mode}.txt').read_text()
    Semantic = [Line for Line in Text.splitlines() if Line.startswith('CORRELATION_')]
    assert Semantic[-1] == 'CORRELATION_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    assert re.search(r'100% tests passed(?:, 0 tests failed)? out of 5', Text)
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Matches = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Matches:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Matches))
    Measurements, Components = {}, []
    for Line in Semantic:
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('CORRELATION_RESULT '):
            assert Fields['mean_gate'] == '1'
            assert int(Fields['queries']) == int(Fields['nearest']) + int(Fields['shadow'])
            Measurements[int(Fields['condition']), Fields['method']] = Fields
        if Line.startswith('CORRELATION_COMPONENTS '):
            Components.append(Fields)
            Mse = float(Fields['direct_variance']) + float(Fields['indirect_variance']) + 2 * float(Fields['covariance']) + float(Fields['mean_error_squared'])
            assert abs(Mse - float(Fields['rederived_mse'])) < 2e-9
        if Line.startswith('CORRELATION_CHECKS '):
            assert Fields['warm_submissions'] == '256' and int(Fields['temporal_accepted']) > 0
            assert Fields['spatial_links'] == '0' and float(Fields['maximum_independent_difference']) > 1e-7
    assert len(Measurements) == 10 and len(Components) == 4
    for Condition in (0, 1):
        W, C, I, S = (Measurements[Condition, Name] for Name in ('whole8', 'coupled8', 'independent8', 'whole6'))
        assert tuple(W[Name] for Name in ('queries', 'nearest', 'shadow')) == tuple(C[Name] for Name in ('queries', 'nearest', 'shadow'))
        assert int(S['calls']) * 8 == int(W['calls']) * 6
        assert int(C['calls']) == int(I['calls']) == 2 * int(W['calls'])
    for Fields in Components:
        Name = 'coupled8' if Fields['arm'] == 'coupled' else 'independent8'
        Rmse = float(Measurements[int(Fields['condition']), Name]['rmse'])
        assert abs(Rmse * Rmse - float(Fields['rederived_mse'])) < 2e-9
    Build = Root / f'_ClaudeScratch/build/replay-correlation-{Mode}'
    with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
        subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileReplayCorrelation.py'),
                        str(Path(Temporary) / 'ReplayPowerReuse.slang')], cwd=Root, check=True, stdout=subprocess.DEVNULL)
        for Name in ('ReplayPowerReuse.slang', 'ReplayFreshPower.slang', 'ReplayFreshSix.slang'):
            assert (Path(Temporary) / Name).read_bytes() == (Build / Name).read_bytes(), Name
    Rows.append(Semantic)
assert Rows[0] == Rows[1], 'Host semantic rows differ'
for Condition in (0, 1):
    for Method in range(5):
        Name = f'Correlation{Condition}{Method}.ppm'
        A, B = (Root / f'_ClaudeScratch/replay-correlation-capture-{Mode}' / Name for Mode in ('release', 'sanitized'))
        assert A.read_bytes() == B.read_bytes(), Name
        with Image.open(A) as Original:
            with Image.open(Root / f'Exhibits/LightCompute/Images/Correlation{Condition}{Method}.png') as Retained:
                assert Original.convert('RGB').tobytes() == Retained.convert('RGB').tobytes()
Message = (f'PASS correlation controls: {len(Rows[0])} identical semantic rows; logged hashes {HashCounts}; '
           'ten exact readbacks/PNG pixels; covariance MSE identities, Fresh6 calls, source rederivation and work sums pass; '
           'both five-CTest sets passed. promote=0.\n')
(Results / 'ReplayCorrelation-parity.txt').write_text(Message)
print(Message, end='')

# 🧩 Audit ablation feature isolation, cold equivalence, work sums and cross-build evidence.
from pathlib import Path
import hashlib
import re
from PIL import Image

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Rows, HashCounts = [], []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'ReplayReuseAblation-{Mode}.txt').read_text()
    Semantic = [Line for Line in Text.splitlines() if Line.startswith('ABLATION_')]
    assert Semantic[-1].startswith('ABLATION_DECISION correctness=1 ')
    assert re.search(r'100% tests passed(?:, 0 tests failed)? out of 5', Text)
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Matches = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Matches:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Matches))
    assert sum(Line.startswith('ABLATION_COLD ') for Line in Semantic) == 2
    assert sum(Line.startswith('ABLATION_PAIRED ') for Line in Semantic) == 6
    assert sum(Line.startswith('ABLATION_WARM ') for Line in Semantic) == 4
    Work = {}
    for Line in Semantic:
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('ABLATION_PAIRED '):
            Expected = float(Fields['mse_difference']) + 3 * float(Fields['nominal_se']) < 0
            assert int(Fields['improvement_gate']) == int(Expected)
        if Line.startswith('ABLATION_RESULT '):
            assert Fields['mean_gate'] == '1'
            assert int(Fields['queries']) == int(Fields['nearest']) + int(Fields['shadow'])
            Work[int(Fields['condition']), Fields['method']] = tuple(int(Fields[Name]) for Name in ('queries', 'nearest', 'shadow'))
            if Fields['method'] == 'temporal':
                assert int(Fields['accepted']) > 0 and Fields['spatial_links'] == '0'
            if Fields['method'] == 'spatial':
                assert int(Fields['accepted']) > 0 and int(Fields['spatial_links']) > 0
        if Line.startswith('ABLATION_WARM '):
            assert Fields['submissions'] == '256'
            if Fields['method'] == 'temporal':
                assert Fields['spatial_links'] == '0'
            else:
                assert int(Fields['spatial_links']) > 0
    assert len(Work) == 8
    for Condition in (0, 1):
        assert Work[Condition, 'whole'] == Work[Condition, 'fresh']
    Rows.append(Semantic)
assert Rows[0] == Rows[1], 'Host numerical/feature rows differ'
for Condition in (0, 1):
    for Method in ('Whole', 'Fresh', 'Temporal', 'Spatial'):
        Name = f'Ablation{Condition}{Method}.ppm'
        A, B = (Root / f'_ClaudeScratch/replay-reuse-ablation-capture-{Mode}' / Name for Mode in ('release', 'sanitized'))
        assert A.read_bytes() == B.read_bytes(), Name
        with Image.open(A) as Original:
            with Image.open(Root / f'Exhibits/LightCompute/Images/ReuseAblation{Condition}{Method}.png') as Retained:
                assert Original.convert('RGB').tobytes() == Retained.convert('RGB').tobytes()
Message = (f'PASS reuse ablation evidence: {len(Rows[0])} identical semantic rows; logged hashes {HashCounts}; '
           'eight exact captures and retained PNG pixels; feature isolation and work sums exact; both five-CTest sets passed.\n')
(Results / 'ReplayReuseAblation-parity.txt').write_text(Message)
print(Message, end='')

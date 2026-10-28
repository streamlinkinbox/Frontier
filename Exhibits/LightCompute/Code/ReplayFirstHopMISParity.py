# 🧩 Audit logged artifacts, paired semantic results and all original captures across host build modes.
from pathlib import Path
import hashlib
import re

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Rows = []
Counts = []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'ReplayFirstHopMIS-{Mode}.txt').read_text()
    Semantic = [Line for Line in Text.splitlines() if Line.startswith('HOP_')]
    assert Semantic[-1] == 'HOP_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    assert '100% tests passed out of 5' in Text
    Matches = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Matches:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Rows.append(Semantic)
    Counts.append(len(Matches))
assert Rows[0] == Rows[1], 'Semantic rows differ between host builds'
for Condition in range(2):
    for Suffix in ('NoMIS', 'Whole', 'Fresh', 'Reuse', 'Mask'):
        Name = f'Split{Condition}{Suffix}.ppm'
        A, B = (Root / f'_ClaudeScratch/replay-first-hop-mis-capture-{Mode}' / Name for Mode in ('release', 'sanitized'))
        assert A.read_bytes() == B.read_bytes(), Name
Message = f'PASS first-hop MIS: {len(Rows[0])} identical semantic rows, logged hashes {Counts}, ten exact RGB captures; both 5 CTests passed.\n'
(Results / 'ReplayFirstHopMIS-parity.txt').write_text(Message)
print(Message, end='')

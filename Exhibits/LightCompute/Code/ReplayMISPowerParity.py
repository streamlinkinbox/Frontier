# 🧩 Verify active/disabled controls, all logged artifact hashes, weight derivation and host-build parity.
from pathlib import Path
import hashlib
import math
import re
import subprocess
import sys
import tempfile

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Rows, Counts = [], []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'ReplayMISPower-{Mode}.txt').read_text()
    Semantic = [Line for Line in Text.splitlines() if Line.startswith('POWER_')]
    assert Semantic[-1].startswith('POWER_DECISION correctness=1 ')
    assert '100% tests passed out of 5' in Text
    assert sum(Line.startswith('POWER_ACTIVE ') for Line in Semantic) == 2
    Matches = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Matches:
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest() == Digest, Name
    Counts.append(len(Matches))
    Rows.append(Semantic)
    Build = Root / f'_ClaudeScratch/build/replay-mis-power-{Mode}'
    assert (Build / 'power/ReplayFreshBudget.spv').read_bytes() != (Build / 'balance/ReplayFreshBudget.spv').read_bytes()
    with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
        subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileReplayMISPower.py'),
                        Temporary], cwd=Root, check=True, stdout=subprocess.DEVNULL)
        for Name in ('balance/ReplayResetBudget.slang', 'power/ReplayMISPower.slang', 'power/ReplayFreshPower.slang'):
            assert (Path(Temporary) / Name).read_bytes() == (Build / Name).read_bytes(), Name
assert Rows[0] == Rows[1], 'Host-build semantic results differ'
for Condition in range(2):
    for Suffix in ('Off', 'Balance', 'Power'):
        Name = f'Power{Condition}{Suffix}.ppm'
        A, B = (Root / f'_ClaudeScratch/replay-mis-power-capture-{Mode}' / Name for Mode in ('release', 'sanitized'))
        assert A.read_bytes() == B.read_bytes(), Name
# Independent arithmetic sanity check, not a claim of extreme-ratio GPU coverage.
for Ratio in (0.,) + tuple(10. ** Exponent for Exponent in range(-30, 31)):
    Small = Ratio if Ratio <= 1 else 1 / Ratio
    Low, High = Small * Small / (1 + Small * Small), 1 / (1 + Small * Small)
    Bsdf, Facet = (Low, High) if Ratio <= 1 else (High, Low)
    assert math.isfinite(Bsdf + Facet) and abs(Bsdf + Facet - 1) < 1e-15
    assert abs(Bsdf - Ratio * Ratio / (1 + Ratio * Ratio)) < 1e-15
Message = (f'PASS power MIS: {len(Rows[0])} identical semantic rows, logged hashes {Counts}, six exact RGB captures; '
           'both 5 CTests passed; active/disabled controls and shader rederivation passed; 62 CPU weight pairs passed.\n')
(Results / 'ReplayMISPower-parity.txt').write_text(Message)
print(Message, end='')

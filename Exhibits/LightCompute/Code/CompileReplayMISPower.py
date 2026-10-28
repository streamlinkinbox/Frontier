# 🧩 Derive a fixed power-heuristic control without changing random draws, storage or production sources.
from pathlib import Path
import subprocess
import sys

Root = Path(sys.argv[1])
Root.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReplayResetBudget.py',
                str(Root / 'balance' / 'ReplayResetBudget.slang')], check=True)
Source = (Root / 'balance/ReplayResetBudget.slang').read_text()
Helper = '''// Power heuristic, beta=2. Branching avoids squaring a large density ratio.
float ReplayPowerWeight(float Ratio, bool Bsdf)
{
    float Small = Ratio <= 1 ? Ratio : 1 / Ratio;
    float Squared = Small * Small;
    float Low = Squared / (1 + Squared);
    float High = 1 / (1 + Squared);
    return Bsdf ? (Ratio <= 1 ? Low : High) : (Ratio <= 1 ? High : Low);
}
'''
for Before, After in (
    ('float3 ReplayFacetRadiance(', Helper + 'float3 ReplayFacetRadiance('),
    ('Balance = 1 / (1 + Ratio);', 'Balance = ReplayPowerWeight(Ratio, false);'),
    ('(Ratio / (1 + Ratio))', 'ReplayPowerWeight(Ratio, true)'),
):
    if Source.count(Before) != 1:
        raise RuntimeError('Review power MIS substitution: ' + Before)
    Source = Source.replace(Before, After)
(Root / 'power').mkdir(exist_ok=True)
(Root / 'power/ReplayMISPower.slang').write_text(Source)
print('Derived fixed beta=2 MIS; complementary weights only, draws and query branches unchanged')

Fresh = Path('Exhibits/LightCompute/Code/ReplayFreshBudget.slang').read_text()
Include = '#include "PathReplayReservoir.slang"'
if Fresh.count(Include) != 1:
    raise RuntimeError('Review Fresh8 evaluator include')
(Root / 'power/ReplayFreshPower.slang').write_text(Fresh.replace(Include, '#include "ReplayMISPower.slang"'))

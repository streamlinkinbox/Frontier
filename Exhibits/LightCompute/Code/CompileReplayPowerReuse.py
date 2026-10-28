# 🧩 Apply fixed power MIS consistently to the split, unfused and actual Fresh8 controls.
import ast
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReplayFirstHop.py', str(Destination)], check=True)
Syntax = ast.parse(Path('Exhibits/LightCompute/Code/CompileReplayMISPower.py').read_text())
Weights = [ast.literal_eval(Assignment.value) for Assignment in Syntax.body
           if isinstance(Assignment, ast.Assign) and any(isinstance(Token, ast.Name) and Token.id == 'Helper'
                                                       for Token in Assignment.targets)]
if len(Weights) != 1:
    raise RuntimeError('Review fixed power-weight function extraction')
WeightFunction = Weights[0]
Root = Destination.parent
for File, FacetName, NeeCount, HitCount in (
    (Destination, 'ReplayDirectFacetRadiance', 2, 3),
    (Root / 'unfused/ReplayIndirectSplit.slang', 'ReplayDirectFacetRadiance', 2, 3),
    (Root / 'unfused/whole/ReplayResetBudget.slang', 'ReplayFacetRadiance', 1, 1),
):
    Source = File.read_text()
    for Before, After, Expected in (
        ('float3 ' + FacetName + '(', WeightFunction + 'float3 ' + FacetName + '(', 1),
        ('Balance = 1 / (1 + Ratio);', 'Balance = ReplayPowerWeight(Ratio, false);', NeeCount),
        ('(Ratio / (1 + Ratio))', 'ReplayPowerWeight(Ratio, true)', HitCount),
    ):
        if Source.count(Before) != Expected:
            raise RuntimeError('Review power reuse substitution: ' + str(File) + ': ' + Before)
        Source = Source.replace(Before, After)
    File.write_text(Source)
Fresh = Path('Exhibits/LightCompute/Code/ReplayFreshBudget.slang').read_text()
Include = '#include "PathReplayReservoir.slang"'
if Fresh.count(Include) != 1:
    raise RuntimeError('Review Fresh8 include')
(Root / 'ReplayFreshPower.slang').write_text(Fresh.replace(Include, '#include "unfused/whole/ReplayResetBudget.slang"'))
print('Derived power reuse: both NEE copies, all BSDF hit terms and actual Fresh8 evaluator use fixed beta=2')

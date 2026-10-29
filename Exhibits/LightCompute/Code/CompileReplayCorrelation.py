# 🧩 Derive independent fresh indirect frame seeds and a six-path whole control without changing transport weights.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReplayPowerReuse.py', str(Destination)], check=True)
Source = Destination.read_text()
Before = '            uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);'
After = '''            uint IndirectFrame = asuint(Extent.Maximum.z);
            uint SeedBits = IndirectFrame ^ (Pixel * 747796405u + 2891336453u);'''
if Source.count(Before) != 1:
    raise RuntimeError('Review fresh-reset indirect stream substitution')
Destination.write_text(Source.replace(Before, After))
Fresh = (Destination.parent / 'ReplayFreshPower.slang').read_text()
for Before, After in (('Index < 8;', 'Index < 6;'), ('RadianceSum / 8.f', 'RadianceSum / 6.f')):
    if Fresh.count(Before) != 1:
        raise RuntimeError('Review whole Fresh6 substitution: ' + Before)
    Fresh = Fresh.replace(Before, After)
(Destination.parent / 'ReplayFreshSix.slang').write_text(Fresh)
print('Derived fresh-only indirect seed override and Whole6; direct stream/history estimator unchanged')

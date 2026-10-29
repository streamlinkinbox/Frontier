# 🧩 Move newly drawn bank proposals to the current receiver; retain each older draw's actual area density.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileCandidateBank.py', str(Destination)], check=True)
Source = Destination.read_text()
Start = Source.index('void InlineBankEstimate(')
Prefix, Body = Source[:Start], Source[Start:]
assert Body.count('TriangleProposalOrigin = 0;') == 2
assert Body.count('SecondaryVisible(Point, 0, 0)') == 4
assert Body.count('SecondaryCosine(Point, 0)') == 2
assert Body.count('SecondaryMeasure(Point, 0)') == 2
Body = Body.replace('TriangleProposalOrigin = 0;', 'TriangleProposalOrigin = R.PositionHit.x;')
Body = Body.replace('SecondaryVisible(Point, 0, 0)', 'SecondaryVisible(Point, R.PositionHit.x, 0)')
Body = Body.replace('SecondaryCosine(Point, 0)', 'SecondaryCosine(Point, R.PositionHit.x)')
Body = Body.replace('SecondaryMeasure(Point, 0)', 'SecondaryMeasure(Point, R.PositionHit.x)')
Destination.write_text(Prefix + Body)

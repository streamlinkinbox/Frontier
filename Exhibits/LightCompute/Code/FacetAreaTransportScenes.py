# 🧩 Reuse exact retained fixture/camera constructors without importing Vulkan or GPU capture inputs.
from pathlib import Path
import subprocess
import sys
Code=Path(__file__).resolve().parent
Output=Path(sys.argv[1])
subprocess.run([sys.executable,str(Code/'FacetAreaProposalScenes.py'),str(Output)],check=True)
Text=Output.read_text()
for Name,Function in (('PerspectiveAreaReservoirValidation.h','ConstructPerspectiveAreaRay'),
                      ('GeometricAreaReservoirValidation.h','ConstructGeometricAreaRay')):
    Source=(Code/Name).read_text()
    Begin=Source.rfind('\n',0,Source.index(Function+'('))+1
    Opening=Source.index('{',Begin);Depth=1;End=Opening+1
    while Depth:
        Depth+=(Source[End]=='{')-(Source[End]=='}');End+=1
    Text+=Source[Begin:End]+'\n'
Output.write_text(Text)

# 🧩 Extract exact retained fixture constructors without importing Vulkan submission or duplicating geometry.
from pathlib import Path
import hashlib
import sys
Root=Path(__file__).resolve().parents[3]
Code=Root/'Exhibits/LightCompute/Code'
Selections={
    'RgbTraceValidation.h':('TraceCross','TraceDot','TraceNormalize','RgbTraceScene'),
    'TriangleReconnectionValidation.h':('TriangleReconnectionScene',),
    'GeometricAreaReservoirValidation.h':('ConstructGeometricAreaScene',),
    'DeformedAreaReservoirValidation.h':('ConstructDeformedAreaScene',),
}
Functions={}
for Name,Names in Selections.items():
    Text=(Code/Name).read_text()
    for Function in Names:
        Begin=Text.rfind('\n',0,Text.index(Function+'('))+1
        Opening=Text.index('{',Begin)
        Depth=1;End=Opening+1
        while Depth:
            Depth+=(Text[End]=='{')-(Text[End]=='}');End+=1
        Functions[Function]=Text[Begin:End]
Header='#pragma once\n// Source-extracted exact constructors;generated scratch,not an independent fixture copy.\n'
Header+='#include "LightComputeFormat.h"\n#include <cmath>\nusing namespace Frontier;\n'
Header+='using TraceVector=std::array<double,3>;\n'
Header+='void Require(bool Accepted,const char* Message) { if (!Accepted) throw std::runtime_error(Message); }\n'
for Function in ('TraceCross','TraceDot','TraceNormalize','TriangleReconnectionScene','RgbTraceScene',
                 'ConstructGeometricAreaScene','ConstructDeformedAreaScene'):
    Header+=Functions[Function]+'\n'
Path(sys.argv[1]).write_text(Header)
print('FIXTURE_CONSTRUCTORS exact=7 generated_sha256='+hashlib.sha256(Header.encode()).hexdigest())

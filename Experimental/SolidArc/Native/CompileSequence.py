"""Compile the existing SolidArc sources for the browser; invoke with an activated Emscripten SDK."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess
import os

Root = Path(__file__).resolve().parents[4]
Source = Root / 'Frontier/Editor/AuthoringTools/Modelling/SolidArc'
Destination = Path(__file__).resolve().parents[1] / 'Runtime'
Scratch = Root / '_AgentScratch/build/SolidArcWeb'
Scratch.mkdir(parents=True, exist_ok=True)
Destination.mkdir(parents=True, exist_ok=True)
Compiler = os.environ.get('EMXX', 'em++')
Files = list((Source / 'Kernel').glob('*.cpp'))
Files += list((Source / 'Document').glob('*.cpp'))
Files += list((Source / 'Interaction').glob('*.cpp'))
Files += [Source / 'Presentation' / Name for Name in ['ScenePresentation.cpp', 'SoftwareRaster.cpp']]
Files += [Source / 'Console' / Name for Name in ['CommandCodec.cpp', 'ConsoleHost.cpp', 'ConsoleInteraction.cpp', 'ConsoleSelection.cpp']]
Files += [Path(__file__).with_name('GeometryExchange.cpp')]
Options = ['-std=c++20', '-O2', '-fexceptions', '-I' + str(Source), '-DSOLIDARC_PROOF_FOLDER="/"']
def Compile(Path):
    Target = Scratch / (Path.stem + '.o')
    subprocess.run([Compiler, *Options, '-c', str(Path), '-o', str(Target)], check=True)
    return str(Target)
with ThreadPoolExecutor(max_workers=2) as Pool:
    Compiled = list(Pool.map(Compile, Files))
subprocess.run([Compiler, *Options, *Compiled, '-o', str(Destination / 'GeometryModule.js'),
    '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,worker,node', '-sALLOW_MEMORY_GROWTH=1',
    '-sSTACK_SIZE=4194304', '-sMAXIMUM_MEMORY=2147483648', '-sFORCE_FILESYSTEM=1',
    '-sEXPORTED_RUNTIME_METHODS=ccall,FS,UTF8ToString,HEAPU8', '-sDISABLE_EXCEPTION_CATCHING=0'], check=True)
print('Compiled native SolidArc for WebAssembly:', Destination)

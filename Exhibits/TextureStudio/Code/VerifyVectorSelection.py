#============================================================================================================================================
#                                                       VERIFYVECTORSELECTION.PY
#============================================================================================================================================
# 🧩 Compile and execute shared selection geometry assertions with address/undefined sanitizers and source provenance.

import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

Root = Path(__file__).resolve().parents[3]
Build = Root / '_ClaudeScratch/build/VectorSelection'
Build.mkdir(parents=True, exist_ok=True)
Sources = ['Engine/ContentInterchange/' + Caption + '.cpp' for Caption in
           ['VectorAuthoringDocument', 'VectorAuthoringEditing', 'VectorAuthoringArrayPlacement',
            'VectorAuthoringProperties', 'VectorAuthoringSelectionEditing']]
Sources += ['Exhibits/TextureStudio/Code/VectorSelectionValidation.cpp']
Flags = ['-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror', '-I.', '-IExternalPackages/tomlpp/include']
Report = {'platform': platform.platform(), 'windows_executed': False, 'uncommitted_sources': True,
          'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=Root, text=True).strip(),
          'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0], 'commands': []}
for Caption, Additional in [('strict', []), ('sanitized', ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
    Executable = Build / Caption
    Command = ['g++'] + Flags + Additional + Sources + ['-o', str(Executable)]
    Report['commands'].append(Command)
    subprocess.run(Command, cwd=Root, check=True)
    Command = [str(Executable)]
    Report['commands'].append(Command)
    Execution = subprocess.run(Command, cwd=Root, text=True, capture_output=True,
                               env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1'))
    print(Execution.stdout, end='', flush=True)
    print(Execution.stderr, end='', flush=True)
    if Execution.returncode:
        raise SystemExit(Execution.returncode)
    Report[Caption] = json.loads(Execution.stdout)
Report['source_sha256'] = {Source: hashlib.sha256((Root / Source).read_bytes()).hexdigest() for Source in Sources}
Report['header_sha256'] = {str(Source.relative_to(Root)): hashlib.sha256(Source.read_bytes()).hexdigest()
                           for Source in sorted((Root / 'Engine/ContentInterchange').glob('Vector*.h'))}
Report['result'] = 'passed'
Destination = Root / 'Exhibits/TextureStudio/Results/VectorSelectionChecks.json'
Destination.write_text(json.dumps(Report, indent=2) + '\n')
print(Destination, flush=True)

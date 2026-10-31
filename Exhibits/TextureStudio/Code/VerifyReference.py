#============================================================================================================================================
#                                                           VERIFYREFERENCE.PY
#============================================================================================================================================
# 🧩 Record the updated reference build and Node-only paint checks; optional prerequisites must already be provisioned.

from pathlib import Path
import hashlib
import json
import subprocess

Root = Path(__file__).resolve().parents[3]
Studio = Root / 'Experimental/TextureStudio'
Commands = [['npm', 'run', 'build']] + [['node', 'scripts/' + Name + '.mjs'] for Name in
    ['verify-gpu-packing', 'verify-stroke-incremental', 'verify-mask-gpu', 'verify-glsl']]
Executions = []
for Command in Commands:
    Completed = subprocess.run(Command, cwd=Studio, capture_output=True, text=True, timeout=240)
    print(Completed.stdout, end='', flush=True)
    print(Completed.stderr, end='', flush=True)
    Executions.append({'command': Command, 'exit_code': Completed.returncode,
                       'stdout': Completed.stdout, 'stderr': Completed.stderr})
    if Completed.returncode:
        raise RuntimeError('Reference check failed: ' + ' '.join(Command))
Sources = [Source for Directory in ['src', 'scripts'] for Source in (Studio / Directory).rglob('*') if Source.is_file()]
Sources += [Studio / 'package.json', Studio / 'package-lock.json', Studio / 'vite.config.js', Path(__file__).resolve()]
Report = {'result': 'passed', 'scope': 'Updated React/Three reference only; no native application claim',
          'optional_prerequisites': ['@napi-rs/canvas', '@shaderfrog/glsl-parser'],
          'limitations': ['GLSL parse warnings about built-in symbols are not GPU compiler results',
                          'Node checks do not execute browser painting or native Vulkan'],
          'executions': Executions,
          'sources': [{'file': Source.relative_to(Root).as_posix(),
                       'sha256': hashlib.sha256(Source.read_bytes()).hexdigest()} for Source in sorted(Sources)]}
(Root / 'Exhibits/TextureStudio/Results/ReferenceChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS: updated reference build and four Node paint checks')

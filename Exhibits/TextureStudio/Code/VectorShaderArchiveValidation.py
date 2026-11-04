#============================================================================================================================================
#                                                   VECTORSHADERARCHIVEVALIDATION.PY
#============================================================================================================================================
# 🧩 Verify current vector shader archives and negative packaging cases without producing an executable package.

from pathlib import Path
import hashlib
import json
import shutil
import sys
import tempfile

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / 'Automation'))
from VectorShaderArchives import VerifyVectorShaderArchives

Directory = Path(sys.argv[1]).resolve()
Original = json.loads((Directory / 'VectorCompilation.json').read_text())
Archives = VerifyVectorShaderArchives(Root, Directory)
Checks = 1
with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp', prefix='vector-archive-refusal-') as Temporary:
    Target = Path(Temporary)
    for Mutation in ['missing-selection', 'corrupt-binary', 'stale-source', 'duplicate-entry', 'wrong-entry']:
        for Filename in Archives:
            shutil.copy2(Directory / Filename, Target / Filename)
        Manifest = json.loads(json.dumps(Original))
        if Mutation == 'missing-selection':
            (Target / 'VectorSelectionCompute.spv').unlink()
        elif Mutation == 'corrupt-binary':
            (Target / 'VectorSelectionCompute.spv').write_bytes(b'not a shader')
        elif Mutation == 'stale-source':
            Manifest['sources'][0]['sha256'] = '0' * 64
        elif Mutation == 'duplicate-entry':
            Manifest['entries'][1] = Manifest['entries'][0]
        else:
            Manifest['entries'][1]['entry'] = '../Unexpected'
        (Target / 'VectorCompilation.json').write_text(json.dumps(Manifest))
        try:
            VerifyVectorShaderArchives(Root, Target)
        except (RuntimeError, FileNotFoundError):
            Checks += 1
        else:
            raise AssertionError('Invalid shader set accepted: ' + Mutation)
Report = {'result': 'passed', 'checks': Checks, 'packages_created': False,
          'archives': {Filename: hashlib.sha256((Directory / Filename).read_bytes()).hexdigest() for Filename in Archives},
          'source_sha256': {Filename: hashlib.sha256((Root / Filename).read_bytes()).hexdigest() for Filename in
              ['Automation/VectorShaderArchives.py', 'Automation/BuildTextureStudioWindows.py',
               'Automation/PackageWorkspacesWindows.py', 'Automation/CompileVectorCoverage.mjs']}}
(Root / 'Exhibits/TextureStudio/Results/VectorShaderArchiveChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
print(json.dumps(Report, indent=2))

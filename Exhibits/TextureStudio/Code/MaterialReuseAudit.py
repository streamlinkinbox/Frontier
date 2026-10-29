"""Compare existing material sources/content to the user's pinned upstream branch without importing code."""
from pathlib import Path
import hashlib
import json
import subprocess

Root = Path(__file__).resolve().parents[3]
Revision = 'a76dbd34d12d5e0dbfafe24dd5aa9d4d5b414c73'
Payload = subprocess.check_output(['gh', 'api', f'repos/SultanAladin/Frontier/git/trees/{Revision}?recursive=1'], cwd=Root)
Tree = json.loads(Payload)
assert not Tree.get('truncated'), 'Incomplete upstream tree cannot establish reuse identity'
Rows = []
for Entry in Tree['tree']:
    Name = Entry['path']
    Selected = (Name.startswith('Engine/Render/') or Name.startswith('EngineContent/MaterialArchives/') or
                Name.startswith('Engine/ContentInterchange/Material'))
    if Entry['type'] != 'blob' or not Selected:
        continue
    Pathname = Root / Name
    Blob = None
    if Pathname.is_file():
        Content = Pathname.read_bytes()
        Blob = hashlib.sha1(b'blob ' + str(len(Content)).encode() + b'\0' + Content).hexdigest()
    Rows.append({'path': Name, 'upstream_git_blob': Entry['sha'], 'local_git_blob': Blob,
                 'identical': Blob == Entry['sha']})
assert Rows
Report = {
    'repository': 'SultanAladin/Frontier', 'branch': 'arena/5e581235-frontier', 'revision': Revision,
    'tree_complete': True, 'files_compared': len(Rows),
    'identical': sum(R['identical'] for R in Rows), 'different_or_absent': sum(not R['identical'] for R in Rows),
    'scope': 'all Engine/Render files, EngineContent/MaterialArchives and Material-prefixed ContentInterchange sources',
    'difference_notes': 'Three retained vector-coverage ABI differences; explicit compute-footprint AutomotivePaint extension; intentional energy allocation changes in ScatteringSequence.h and ScatteringEvaluation.slang; reciprocal coating factor grouping in MaterialSurface.slang. No material archive or partial vector ABI import.',
    'archive_identical': sum(R['identical'] for R in Rows if R['path'].startswith('EngineContent/MaterialArchives/')),
    'archive_compared': sum(R['path'].startswith('EngineContent/MaterialArchives/') for R in Rows),
    'meaning': 'Git blob identity only; not new material physics, scene integration or hardware validation',
    'glints': 'existing cellular selected for automotive paint; Glinty remains separate research',
    'research': 'Documentation/Plans/Ongoing/MaterialGlintResearch.md',
    'executed_comparison': 'Exhibits/TextureStudio/GlintComparison.md',
    'files': Rows,
}
Destination = Root / 'Exhibits/TextureStudio/Results/MaterialReuse.json'
Destination.write_text(json.dumps(Report, indent=2) + '\n')
print(f"Material reuse: {Report['identical']}/{len(Rows)} identical; {Report['different_or_absent']} different or absent")

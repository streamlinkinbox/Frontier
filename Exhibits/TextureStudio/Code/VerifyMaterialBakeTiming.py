#============================================================================================================================================
#                                                      VERIFYMATERIALBAKETIMING.PY
#============================================================================================================================================
# 🧩 Restore the existing pinned image-writer dependency and execute a native procedural-versus-baked sampling measurement.

from pathlib import Path
import hashlib
import io
import json
import subprocess
import tarfile
import urllib.request

Root = Path(__file__).resolve().parents[3]
Dependency = next(P for P in json.loads((Root / 'Automation/UIDependencies.json').read_text())['packages'] if P['name'] == 'stb')
Header = Root / 'ExternalPackages/stb/stb_image_write.h'
if not Header.exists():
    Raw = urllib.request.urlopen(Dependency['url'], timeout=60).read()
    assert hashlib.sha256(Raw).hexdigest() == Dependency['sha256']
    with tarfile.open(fileobj=io.BytesIO(Raw)) as Archive:
        Entry = next(E for E in Archive.getmembers() if E.name.endswith('/stb_image_write.h'))
        Header.parent.mkdir(parents=True, exist_ok=True)
        Header.write_bytes(Archive.extractfile(Entry).read())
Build = Root / '_ClaudeScratch/build'
Build.mkdir(parents=True, exist_ok=True)
Implementation = Build / 'MaterialTimingImageWriter.cpp'
Implementation.write_text('#define STB_IMAGE_WRITE_IMPLEMENTATION\n#include <stb_image_write.h>\n')
Sources = ['Exhibits/TextureStudio/Code/MaterialBakeTiming.cpp', 'Engine/Render/MaterialProceduralBaking.cpp']
subprocess.run(['c++', '-std=c++20', '-O2', '-I.', '-IExternalPackages/stb', *Sources, str(Implementation), '-o', str(Build / 'MaterialBakeTiming')], cwd=Root, check=True)
Reply = subprocess.check_output([str(Build / 'MaterialBakeTiming')], cwd=Root, text=True)
(Root / 'Exhibits/TextureStudio/Results/MaterialBakeTiming.txt').write_text(Reply)
Report = {'cpu_only': True, 'physical_gpu_measured': False, 'scope': 'retained denim field versus four-channel float-map bilinear sampling; not whole-frame or GPU texture-unit cost',
          'source_sha256': {N: hashlib.sha256((Root / N).read_bytes()).hexdigest() for N in Sources + ['Engine/Render/MaterialProceduralBaking.h', 'EngineContent/MaterialArchives/TextureStudio/indigo-denim.material']},
          'raw_result': Reply.strip(), 'dependency_revision': Dependency['revision']}
(Root / 'Exhibits/TextureStudio/Results/MaterialBakeTiming.json').write_text(json.dumps(Report, indent=2) + '\n')
print(Reply, end='')

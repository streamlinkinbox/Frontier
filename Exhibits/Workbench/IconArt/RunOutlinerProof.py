#!/usr/bin/env python3
"""Apply the native integration in an isolated copy; never modify the before-reference tree."""
import hashlib, json, os, shutil, subprocess, tarfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / '.cache/cpp-target'
TARGET = ROOT / '.cache/cpp-outliner'
OUT = ROOT / 'Exhibits/Gallery/NativeOutliner'
OUT.mkdir(parents=True, exist_ok=True)
assert (BASE/'.frontier-proof-pin').read_text().strip() == 'f6c99702c769ef9fd44c39ac8819e0ebeae35b9b'
with tarfile.open(ROOT/'.cache/cpp-target.tar.gz') as archive:
    count = 0
    for entry in archive:
        name = entry.name.split('/',1)[-1]
        if entry.isfile() and name.startswith(('Engine/', 'Exhibits/Workbench/Editor/', 'Projects/Project-Zero/Source/')):
            assert archive.extractfile(entry).read() == (BASE/name).read_bytes(), 'Modified baseline: '+name
            count += 1
(OUT/'BaselineVerification.txt').write_text(f'{count} pinned baseline sources unchanged. Integration uses a separate copy.\n')
# Fresh overlay every run, including target's already-pinned/patched ImGui dependencies.
if TARGET.exists(): shutil.rmtree(TARGET)
shutil.copytree(BASE, TARGET)
env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(TARGET))
subprocess.run(['git', 'apply', str(ROOT/'Tools/Build/Patches/NativeOutliner.patch')], cwd=TARGET, env=env, check=True)
for name in ['IconArt.h', 'IconArt.cpp', 'BakedIconArt.h', 'IconSymbols.inc', 'IconPresentation.h', 'IconPresentation.cpp']:
    shutil.copy2(ROOT/'Engine/DisplayPresentation'/name, TARGET/'Engine/DisplayPresentation'/name)
shutil.copytree(ROOT/'EngineContent/Icons', TARGET/'EngineContent/Icons', dirs_exist_ok=True)
shutil.copytree(ROOT/'Exhibits/Workbench/IconArt', TARGET/'Exhibits/Workbench/IconArt', dirs_exist_ok=True)
script = (TARGET/'Exhibits/Workbench/Editor/CheckEditorProof.sh').read_text()
command = script[script.index('g++ -std=c++20'):script.index(' 2>/tmp/EditorProof.build; then')]
command = command.replace('\\\n','').replace('"$Binary"', str(ROOT/'.cache/NativeOutlinerProof'))
command = command.replace(' -o ', ' -DTVG_STATIC -IExternalPackages/thorvg/inc Engine/DisplayPresentation/IconArt.cpp Engine/DisplayPresentation/IconPresentation.cpp '+str(ROOT/'.cache/icon-art/release/libthorvg.a')+' -o ')
(OUT/'Command.txt').write_text(command+'\n')
with (OUT/'Build.log').open('w') as log:
    subprocess.run(command, shell=True, cwd=TARGET, stdout=log, stderr=subprocess.STDOUT, check=True)
for folder in [OUT, TARGET/'Exhibits/Gallery/Editor']:
    for image in folder.glob('EditorProof*.png'): image.unlink()
with (OUT/'Proof.log').open('w') as log:
    result = subprocess.run([str(ROOT/'.cache/NativeOutlinerProof')], cwd=TARGET, env=env, stdout=log, stderr=subprocess.STDOUT)
(OUT/'ExitCode.txt').write_text(str(result.returncode)+'\n')
# Keep the original lexical/no-allocation/chrome source gates as well.
a = script.index('# Banned vocabulary'); b = script.index('echo "[EditorProof] the knobs')
with (OUT/'SourceGates.log').open('w') as log:
    subprocess.run(['bash','-c','Fail=0\n'+script[a:b]+'\nexit "$Fail"'],cwd=TARGET,stdout=log,stderr=subprocess.STDOUT,check=True)
with (OUT/'SourceGates.log').open('a') as log: log.write('PASS source gates\n')
with (OUT/'ProjectSyntax.log').open('w') as log:
    syntax = ['g++','-std=c++20','-DFRONTIER_DEVELOPMENT','-IEngine','-IEditor/AuthoringTools/Modelling/SolidArc','-IExternalPackages/imgui','-IExternalPackages/tomlpp/include','-IEngine/DisplayPresentation','-fsyntax-only','Projects/Project-Zero/Source/CelestialSequence.cpp','Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcEditorHost.cpp']
    log.write(' '.join(syntax)+'\n'); log.flush()
    subprocess.run(syntax,cwd=TARGET,stdout=log,stderr=subprocess.STDOUT,check=True)
    log.write('PASS (syntax only; not full application linkage)\n')
for image in (TARGET/'Exhibits/Gallery/Editor').glob('EditorProof*.png'): shutil.copy2(image, OUT/image.name)
files = ['Tools/Build/Patches/NativeOutliner.patch','Engine/DisplayPresentation/IconPresentation.cpp','Engine/DisplayPresentation/IconPresentation.h','Exhibits/Workbench/IconArt/CpuDraw.h']
(OUT/'Provenance.json').write_text(json.dumps({'target':'f6c99702c769ef9fd44c39ac8819e0ebeae35b9b','mode':'native C++ CPU, no GPU execution','exitCode':result.returncode,'sha256':{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in files},'images':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.glob('*.png'))}},indent=2)+'\n')
print((OUT/'Proof.log').read_text())
raise SystemExit(result.returncode)

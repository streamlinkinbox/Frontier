#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,os,hashlib
R=Path(__file__).resolve().parents[3];B=R/'.cache/celestial-bake-proof';B.mkdir(exist_ok=True);O=R/'Exhibits/Gallery/MainEditorNative/Editor';reports=[]
for mode,opt in [('Release',['-O2']),('Debug',['-O0','-g']),('Sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 cmd=['g++','-std=c++20','-Wall','-Wextra','-Werror','-fstack-usage',*opt,'-DTVG_STATIC','-I'+str(R/'Engine/DisplayPresentation'),'-I'+str(R/'.cache/cpp-target/ExternalPackages/thorvg/inc'),str(R/'Engine/DisplayPresentation/IconArt.cpp'),str(R/'Exhibits/Workbench/IconArt/CelestialBakeProof.cpp'),str(R/'.cache/icon-art/release/libthorvg.a'),'-pthread','-o',str(B/mode)]
 subprocess.run(cmd,cwd=R,check=True)
 run=subprocess.run([str(B/mode)],cwd=R,text=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 print(mode,run.stdout,run.stderr);reports.append({'mode':mode,'command':cmd,'exit':run.returncode,'output':run.stdout+run.stderr});assert run.returncode==0
inputs=[R/'Engine/DisplayPresentation/IconArt.cpp',R/'Engine/DisplayPresentation/BakedIconArt.h',Path(__file__),R/'Exhibits/Workbench/IconArt/CelestialBakeProof.cpp',R/'Exhibits/Workbench/IconArt/BakeCelestialIcons.mjs',*list((R/'EngineContent/Icons/Baked').glob('*'))]
(O/'CelestialIconProof.json').write_text(json.dumps({'tests':reports,'limitation':'ThorVG static dependency is release-only; no GPU/Windows claim.','sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}},indent=2)+'\n')

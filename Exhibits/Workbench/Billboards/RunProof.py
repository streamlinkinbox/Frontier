#!/usr/bin/env python3
"""Reconstruct native sources, compile, click actual ImGui controls, render real environment A/Bs."""
from pathlib import Path
import argparse,concurrent.futures,json,os,shutil,subprocess,sys,hashlib
R=Path(__file__).resolve().parents[3];T=R/'.cache/cpp-sun-full';O=R/'Exhibits/Gallery/NativeBillboards';O.mkdir(parents=True,exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--reuse-target',action='store_true');p.add_argument('--incremental',action='store_true');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
if not a.reuse_target:subprocess.run([sys.executable,str(R/'Exhibits/Workbench/Construct/RunNativePanel.py')],check=True,cwd=R)
patch=R/'Tools/Build/Patches/NativeBillboards.patch';env=dict(os.environ,GIT_CEILING_DIRECTORIES=str(T))
if subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=T,env=env,capture_output=True).returncode:subprocess.run(['git','apply',str(patch)],cwd=T,env=env,check=True)
for n in ['Engine/Editor/ViewportBillboards.h','Projects/Project-Zero/Source/EditorInspectorSequence.h']:shutil.copy2(R/n,T/n)
B=R/'.cache'/('native-billboards-asan' if a.sanitize else 'native-billboards');B.mkdir(exist_ok=True)
commands=json.loads((R/'Exhibits/Gallery/MainEditorNative/ReleaseCommands.json').read_text())
for c in commands:
 for i,arg in enumerate(c):
  if arg.endswith('NativeIntegrationProof.cpp'):c[i]=str(R/'Exhibits/Workbench/Billboards/NativeSceneProof.cpp')
  if '/.cache/main-editor-release/' in arg:c[i]=str(B/Path(arg).name)
 if '-c' in c:c+=['-O2']
 if a.sanitize:c+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
def run(c):
 if a.incremental and '-c' in c:
  src=Path(c[c.index('-c')+1]);obj=Path(c[c.index('-o')+1])
  if obj.exists() and obj.stat().st_mtime>max(src.stat().st_mtime,(R/'Engine/Editor/ViewportBillboards.h').stat().st_mtime,(R/'Projects/Project-Zero/Source/EditorInspectorSequence.h').stat().st_mtime):return
 r=subprocess.run(c,cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError(' '.join(c)+'\n'+r.stdout)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(run,commands[:-1]))
run(commands[-1]);(T/'Exhibits/Gallery/NativeBillboards').mkdir(parents=True,exist_ok=True)
r=subprocess.run([str(B/'Proof')],cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'));print(r.stdout)
for f in (T/'Exhibits/Gallery/NativeBillboards').iterdir():shutil.copy2(f,O/f.name)
inputs=[patch,R/'Engine/Editor/ViewportBillboards.h',R/'Projects/Project-Zero/Source/EditorInspectorSequence.h',Path(__file__),R/'Exhibits/Workbench/Billboards/NativeSceneProof.cpp']
report={'exit':r.returncode,'cpuOnly':True,'output':r.stdout,'commands':commands,'sha256':{str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}}
(O/('Sanitized.json' if a.sanitize else 'Proof.json')).write_text(json.dumps(report,indent=2)+'\n');assert r.returncode==0
syntax=json.loads((R/'Exhibits/Gallery/MainEditorNative/GameSyntax.json').read_text())['command'];r=subprocess.run(syntax,cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(O/'GameSyntax.json').write_text(json.dumps({'command':syntax,'exit':r.returncode,'output':r.stdout},indent=2)+'\n');assert r.returncode==0

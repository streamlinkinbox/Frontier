#!/usr/bin/env python3
"""Link and exercise Construct in the real native EditorHost (CPU ImGui proof)."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,os,shutil,subprocess,sys
R=Path(__file__).resolve().parents[3];T=R/'.cache/cpp-sun-full';O=R/'Exhibits/Gallery/NativeConstruct';O.mkdir(parents=True,exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--reuse-target',action='store_true');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
if not a.reuse_target:
 subprocess.run([sys.executable,str(R/'Exhibits/Workbench/IconArt/PrepareTarget.py')],check=True,cwd=R)
 sys.path.insert(0,str(R/'Exhibits/Workbench/IconArt'))
 from PrepareTarget import acquire
 acquire('KhronosGroup/Vulkan-Headers','b379292b2ab6df5771ba9870d53cf8b2c9295daf',R/'.cache/vulkan-headers-sky')
 subprocess.run([sys.executable,str(R/'Exhibits/Workbench/MainEditor/RunIntegrationProof.py')],check=True,cwd=R)
env=dict(os.environ,GIT_CEILING_DIRECTORIES=str(T));patch=R/'Tools/Build/Patches/NativeConstruct.patch'
if subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=T,env=env,capture_output=True).returncode:
 subprocess.run(['git','apply',str(patch)],cwd=T,env=env,check=True)
shutil.copy2(R/'Engine/Editor/NativeConstructPanel.h',T/'Engine/Editor/NativeConstructPanel.h')
for name in ['IconArt.cpp','BakedIconArt.h']:shutil.copy2(R/'Engine/DisplayPresentation'/name,T/'Engine/DisplayPresentation'/name)
commands=json.loads((R/'Exhibits/Gallery/MainEditorNative/ReleaseCommands.json').read_text())
B=R/'.cache'/('native-construct-asan' if a.sanitize else 'native-construct');B.mkdir(exist_ok=True)
for c in commands:
 for i,arg in enumerate(c):
  if arg.endswith('NativeIntegrationProof.cpp'):c[i]=str(R/'Exhibits/Workbench/Construct/NativePanelProof.cpp')
  if '/.cache/main-editor-release/' in arg:c[i]=str(B/Path(arg).name)
 if a.sanitize:c+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
def run(c):
 r=subprocess.run(c,cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError(' '.join(c)+'\n'+r.stdout)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(run,commands[:-1]))
run(commands[-1])
shutil.copytree(R/'EngineContent/Icons',T/'EngineContent/Icons',dirs_exist_ok=True)
(T/'Exhibits/Gallery/NativeConstruct').mkdir(parents=True,exist_ok=True)
(T/'Exhibits/Gallery/MainEditorNative').mkdir(parents=True,exist_ok=True)
result=subprocess.run([str(B/'Proof')],cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
print(result.stdout)
for image in (T/'Exhibits/Gallery/NativeConstruct').glob('*.png'):shutil.copy2(image,O/image.name)
inputs=[R/'Engine/Editor/NativeConstructPanel.h',patch,R/'Engine/DisplayPresentation/IconArt.cpp',Path(__file__),R/'Exhibits/Workbench/Construct/NativePanelProof.cpp']
report={'exit':result.returncode,'cpuOnly':True,'output':result.stdout,'commands':commands,'sha256':{str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}}
(O/('Sanitized.json' if a.sanitize else 'Proof.json')).write_text(json.dumps(report,indent=2)+'\n')
assert result.returncode==0
syntax=json.loads((R/'Exhibits/Gallery/MainEditorNative/GameSyntax.json').read_text())['command'];r=subprocess.run(syntax,cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(O/'GameSyntax.json').write_text(json.dumps({'command':syntax,'exit':r.returncode,'output':r.stdout},indent=2)+'\n');assert r.returncode==0

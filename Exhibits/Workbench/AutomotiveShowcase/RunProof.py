#!/usr/bin/env python3
"""Build the real 400-sphere C++ structure, shade the five added families, check shared shader."""
import argparse, hashlib, json, os, resource, subprocess
from pathlib import Path
from PrepareTarget import ROOT, STAGE, BASE, OVERLAYS, prepare
p=argparse.ArgumentParser();p.add_argument('--skip-render',action='store_true');p.add_argument('--glslang',type=Path);a=p.parse_args()
dependencies=prepare()
O=ROOT/'Exhibits/Gallery/AutomotiveShowcase';O.mkdir(parents=True,exist_ok=True)
inputs=[ROOT/x for x in OVERLAYS]+list((ROOT/'Exhibits/Workbench/AutomotiveShowcase').glob('*.*'))+[ROOT/'Exhibits/Workbench/AutomotiveFlakes/PaintScene.slang']
hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}
if a.skip_render:
 old=json.loads((O/'NativeProof.json').read_text())
 # Runner/docs may change without altering native rendering; compare the exact source inputs.
 for f,h in hashes.items():
  if f.endswith(('.cpp','.h','.slang')):assert old['sha256'].get(f)==h,'Render inputs changed: '+f
includes=['-I'+str(STAGE),'-I'+str(STAGE/'stub')]+['-I'+str(STAGE/x) for x in ['Engine/Shaders','Engine/ContentInterchange','Engine/DisplayPresentation','Engine/GeometricRaster','Projects/Project-Zero/Source','Exhibits/Workbench/Materials']]
sources=[ROOT/'Exhibits/Workbench/AutomotiveShowcase/NativeShowcaseProof.cpp']+[STAGE/x for x in ['Engine/ContentInterchange/ShowcaseStructure.cpp','Engine/ContentInterchange/MaterialIndex.cpp','Engine/DeviceExchange/OrientationClassifier.cpp','Tools/Build/Gates/ShowcaseLevelGateCodecStub.cpp']]
def run(cmd,**kwargs):
 r=subprocess.run([str(x) for x in cmd],cwd=ROOT,text=True,capture_output=True,**kwargs);print(r.stdout,r.stderr);assert r.returncode==0,cmd;return {'command':[str(x) for x in cmd],'exit':r.returncode,'output':r.stdout+r.stderr}
def stack_limit():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
reports=[]
for mode,opt in [('Release',['-O2']),('Debug',['-O0']),('Sanitized',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 binary=STAGE/mode
 c=run(['g++','-std=c++20','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fstack-usage',*opt,*includes,*sources,'-pthread','-o',binary])
 test=run([binary,O]+([] if mode=='Release' and not a.skip_render else ['--test']),preexec_fn=None if mode=='Sanitized' else stack_limit,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 frames=[]
 for f in STAGE.glob(mode+'*.su'):
  for line in f.read_text().splitlines():
   if any(x in line for x in ['AutomotiveShowcase.slang:','void Test','int main(','vec3 Studio']):frames.append(line)
 for line in frames:assert int(line.split('\t')[1])<=8192,'8 KiB frame gate: '+line
 reports.append({'mode':mode,'compile':c,'test':test,'stackFrames':frames})
host=run(['g++','-std=c++20','-fsyntax-only',*includes,ROOT/'Projects/Project-Zero/Host/MaterialLevelViewport.cpp'])
# Existing scene gate also validates luminaires, spans, original lobe census, and bounded geometry.
exe=STAGE/'SceneGate';run(['g++','-std=c++20','-O2',*includes,STAGE/'Tools/Build/Gates/ShowcaseLevelGate.cpp',*sources[1:2],*sources[3:],'-o',exe]);gate=run([exe])
shader=None
if a.glslang:
 shader=run([a.glslang.resolve(),'-V','--target-env','vulkan1.2','-S','comp','-I'+str(STAGE/'Engine'),'-I'+str(STAGE/'Engine/Shaders'),'-o',STAGE/'viewport.spv',STAGE/'Engine/Shaders/ReSTIRViewport.slang'])
for f in O.glob('*.png'):hashes[str(f.relative_to(ROOT))]=hashlib.sha256(f.read_bytes()).hexdigest()
(O/'NativeProof.json').write_text(json.dumps({'modes':reports,'hostSyntax':host,'sceneGate':gate,'shaderCompilation':shader,'gpuExecution':False,'windowsExecution':False,'baseline':'f6c99702c769ef9fd44c39ac8819e0ebeae35b9b','sha256':hashes,'dependencies':{name:hashlib.sha256((BASE/name).read_bytes()).hexdigest() for name in dependencies}},indent=2)+'\n')

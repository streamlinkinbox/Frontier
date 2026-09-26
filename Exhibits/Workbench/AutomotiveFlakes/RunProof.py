#!/usr/bin/env python3
"""Build the exact shared shader as C++, run three-mode tests, emit the GLSL exhibit.
Requires the pinned reconstructed target (.cache/cpp-sun-full); does not mutate it.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,resource,struct,subprocess,zlib
R=Path(__file__).resolve().parents[3];T=R/'.cache/cpp-sun-full';B=R/'.cache/automotive-flakes';O=R/'Exhibits/Gallery/AutomotiveFlakes';B.mkdir(exist_ok=True);O.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--skip-render',action='store_true');a=p.parse_args()
# Never stamp old native images with hashes from changed shader/render sources.
if a.skip_render and any(O.glob('paint-*.png')):
 previous=json.loads((O/'Provenance.json').read_text())['sha256']
 render_inputs=[R/'Engine/Shaders/AutomotiveFlakePaint.slang',T/'Engine/Shaders/MaterialEvaluation.slang',T/'Engine/Shaders/AutomotiveMaterialProfiles.slang',T/'Engine/Shaders/SlangCpuShim.h',T/'Engine/ContentInterchange/PngWriteCounterpart.h',R/'Exhibits/Workbench/AutomotiveFlakes/CpuShader.h',R/'Exhibits/Workbench/AutomotiveFlakes/PaintScene.slang',R/'Exhibits/Workbench/AutomotiveFlakes/NativeFlakeProof.cpp']
 for file in render_inputs:
  assert previous.get(str(file.relative_to(R)))==hashlib.sha256(file.read_bytes()).hexdigest(),'Native render source changed; rerun without --skip-render: '+str(file)
def limit():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
reports=[]
for mode,opt in [('Release',['-O2']),('Debug',['-O0']),('Sanitized',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 command=['g++','-std=c++20','-g','-fstack-usage','-Wall','-Wextra','-Werror','-Wno-unused-function',*opt,'-I'+str(T/'Engine/Shaders'),'-I'+str(R/'Engine/Shaders'),'-I'+str(T/'Exhibits/Workbench/Editor'),str(R/'Exhibits/Workbench/AutomotiveFlakes/NativeFlakeProof.cpp'),'-pthread','-o',str(B/mode)]
 subprocess.run(command,cwd=R,check=True)
 run=[str(B/mode),str(O)]+([] if mode=='Release' and not a.skip_render else ['--test'])
 result=subprocess.run(run,cwd=R,text=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'),preexec_fn=None if mode=='Sanitized' else limit)
 print(mode,result.stdout,result.stderr);assert result.returncode==0
 stack=[]
 for file in B.glob(mode+'*.su'):
  for line in file.read_text().splitlines():
   if 'AutomotiveFlakePaint.slang:' in line or any(key in line for key in ['AutomotivePrepareFlakes(', 'AutomotiveEvaluatePaint(', 'AutomotiveEvaluateCoat(', 'APStudio(', 'void Test', 'void WriteProbes(', 'int main(']):stack.append(line)
 for line in stack:
  assert int(line.split('\t')[1])<=8192,'8 KiB frame budget exceeded: '+line
 reports.append({'mode':mode,'compile':command,'run':run,'exit':result.returncode,'output':result.stdout+result.stderr,'stackLimit':'default' if mode=='Sanitized' else 262144,'stackFrames':stack})
(O/'NativeProof.json').write_text(json.dumps(reports,indent=2)+'\n')
# Flatten existing material dependencies, preserving the actual engine thin-film functions.
# LUT stubs are unused by the flake evaluator (the CPU counterparts throw if reached).
def flatten(path):
 text=path.read_text()
 return re.sub(r'^#include "([^"]+)"',lambda m:flatten(path.parent/m.group(1)),text,flags=re.M)
engine=flatten(T/'Engine/Shaders/MaterialEvaluation.slang')
preamble='''#define FRONTIER_CPU_PORT 1
vec3 FetchEnergy(float a,float b){return vec3(0.0);}
vec3 FetchSheen(float a,float b){return vec3(0.0);}
vec4 FetchSheenFull(float a,float b){return vec4(0.0);}
'''
shader=preamble+engine+'\n'+(R/'Engine/Shaders/AutomotiveFlakePaint.slang').read_text()+'\n'+(R/'Exhibits/Workbench/AutomotiveFlakes/PaintScene.slang').read_text()
(O/'shared.glsl').write_text(shader)
from VersionAssets import version_assets
version_assets()
for path in O.glob('*.png'):
 if path.name.startswith('PaintLab'):continue # Browser screenshot has its own provenance.
 blob=path.read_bytes();chunks=[];i=8
 while i<len(blob):
  n=struct.unpack('>I',blob[i:i+4])[0];chunks.append((blob[i+4:i+8],blob[i+8:i+8+n]));i+=n+12
 packed=zlib.compress(zlib.decompress(b''.join(d for tag,d in chunks if tag==b'IDAT')),9);out=bytearray(blob[:8]);done=False
 for tag,data in chunks:
  if tag==b'IDAT':
   if done:continue
   data=packed;done=True
  out+=struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
 path.write_bytes(out)
inputs=[R/'Engine/Shaders/AutomotiveFlakePaint.slang',T/'Engine/Shaders/MaterialEvaluation.slang',T/'Engine/Shaders/AutomotiveMaterialProfiles.slang',T/'Engine/Shaders/SlangCpuShim.h',T/'Engine/ContentInterchange/PngWriteCounterpart.h',*list((R/'Exhibits/Workbench/AutomotiveFlakes').glob('*.*')),*[p for p in O.glob('*.png') if not p.name.startswith('PaintLab')],O/'shared.glsl']
(O/'Provenance.json').write_text(json.dumps({'target':'f6c99702c769ef9fd44c39ac8819e0ebeae35b9b','mode':'Native C++ reference; separate GLSL WebGL exhibit; not Project-Zero GPU deployment','sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs if p.is_file()}},indent=2)+'\n')

#!/usr/bin/env python3
"""Build real SceneStructure CPU authoring against verified pinned engine sources.
Only a Vulkan declaration stub is used; no Vulkan execution is claimed.
"""
from pathlib import Path
import argparse, base64, hashlib, json, os, re, shutil, subprocess
ROOT=Path(__file__).resolve().parents[3]
PIN='f6c99702c769ef9fd44c39ac8819e0ebeae35b9b'
STAGE=ROOT/'.cache/construct/engine'
SOURCES=['Engine/GeometricRaster/SceneStructure.cpp','Engine/GeometricRaster/GeometryStructure.cpp','Engine/ContentInterchange/MaterialIndex.cpp','Engine/DeviceExchange/OrientationClassifier.cpp']

def prepare():
 tree=json.loads(subprocess.check_output(['gh','api',f'repos/SultanAladin/Frontier-/git/trees/{PIN}?recursive=1']))
 entries={x['path']:x for x in tree['tree'] if x['type']=='blob'};visited=set()
 def fetch(name):
  if name in visited:return
  visited.add(name);destination=STAGE/name
  if not destination.exists():
   # Reuse only content that verifies against the upstream Git object.
   cached=ROOT/'.cache/cpp-sun-full'/name
   if cached.exists():data=cached.read_bytes()
   else:data=base64.b64decode(json.loads(subprocess.check_output(['gh','api',f'repos/SultanAladin/Frontier-/contents/{name}?ref={PIN}']))['content'])
   destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data)
  data=destination.read_bytes()
  assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==entries[name]['sha'],'Modified pinned source: '+name
  for include in re.findall(r'^\s*#include\s+"([^\"]+)"',data.decode(),re.M):
   relative=os.path.normpath(str(Path(name).parent/include))
   if relative in entries:fetch(relative)
   else:
    matches=[p for p in entries if p.endswith('/'+include)]
    if len(matches)==1:fetch(matches[0])
 for source in SOURCES:fetch(source)
 return sorted(visited)

if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
 dependencies=prepare()
 for name in ['ConstructWorld.h','ConstructWorld.cpp']:
  p=STAGE/'Engine/Editor'/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'Engine/Editor'/name,p)
 stub=STAGE/'stub/vulkan/vulkan.h';stub.parent.mkdir(parents=True,exist_ok=True)
 stub.write_text('#pragma once\n#include <cstdint>\n'+''.join(f'typedef struct {h}_T* {h};\n' for h in 'VkPhysicalDevice VkDevice VkQueue VkCommandPool VkCommandBuffer VkImage VkBuffer VkInstance'.split()))
 output=ROOT/'.cache/construct'/('WorldHost-ASAN' if args.sanitize else 'WorldHost')
 cmd=['g++','-std=c++20','-O1' if args.sanitize else '-O2','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(STAGE/'stub'),'-I'+str(STAGE/'Engine/Editor'),*[str(STAGE/s) for s in SOURCES],str(STAGE/'Engine/Editor/ConstructWorld.cpp'),str(Path(__file__).with_name('WorldHost.cpp')),'-o',str(output)]
 if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True)
 result=subprocess.run([str(output),'--test'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 print(result.stdout);assert result.returncode==0
 report=ROOT/'Exhibits/Gallery/Construct'/('Native-sanitized.json' if args.sanitize else 'Native-proof.json');report.parent.mkdir(parents=True,exist_ok=True)
 files=[STAGE/p for p in dependencies]+[ROOT/'Engine/Editor'/n for n in ['ConstructWorld.h','ConstructWorld.cpp']]+[Path(__file__),Path(__file__).with_name('WorldHost.cpp')]
 report.write_text(json.dumps({'pin':PIN,'command':cmd,'exit':result.returncode,'output':result.stdout,'cpuOnly':True,'sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')

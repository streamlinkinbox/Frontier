#!/usr/bin/env python3
"""Rebuild pinned integration and regress preceding inspectors, then exercise native Stars."""
import argparse,hashlib,json,os,resource,subprocess,struct,zlib
from pathlib import Path
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--debug',action='store_true');p.add_argument('--sanitize',action='store_true');p.add_argument('--reuse-build',action='store_true');a=p.parse_args()
mode='Sanitized' if a.sanitize else 'Debug' if a.debug else 'Release';option=['--sanitize'] if a.sanitize else ['--debug'] if a.debug else []
if not a.reuse_build:subprocess.run(['python3',str(R/'Exhibits/Workbench/Moon/RunNativePanel.py'),*option],cwd=R,check=True)
b=R/'.cache'/('sun-full-sanitized' if a.sanitize else 'sun-full-debug' if a.debug else 'sun-full-build');t=R/'.cache/cpp-sun-full';out=R/'Exhibits/Gallery/StarsNative';out.mkdir(parents=True,exist_ok=True)
base=json.loads((R/'Exhibits/Gallery/SunFullPanel'/(mode+'Commands.json')).read_text());template=next(c[:] for c in base if any(x.endswith('/FullPanelProof.cpp') for x in c));commands=[];objects=[]
for src in [R/'Exhibits/Workbench/Stars/NativePanelProof.cpp',t/'Engine/GeometricRaster/StarCatalogueIndex.cpp',t/'Engine/ContentInterchange/AssetResolution.cpp',t/'Engine/GeometricRaster/VisibilityRaster.cpp']:
 c=template[:]+['-I'+str(R/'.cache/vulkan-headers-sky/include')];obj=b/(('StarsProof' if src.name=='NativePanelProof.cpp' else src.stem)+'.o');c[c.index('-c')+1]=str(src);c[c.index('-o')+1]=str(obj);subprocess.run(c,cwd=R,check=True);commands.append(c)
 if src.name!='VisibilityRaster.cpp':objects.append(str(obj))
link=[x for x in base[-1] if not x.endswith('/FullPanelProof.o')];link=[str(b/'StarsProof') if x.endswith('/FullPanelProof') else x for x in link];link+=objects;subprocess.run(link,cwd=R,check=True);commands.append(link)
def limit():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');result=subprocess.run([str(b/'StarsProof')],cwd=R,env=env,preexec_fn=None if a.sanitize else limit,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/(mode+'Proof.txt')).write_text(result.stdout+f'Exit: {result.returncode}; '+('sanitizer default stack' if a.sanitize else 'Linux stack limit: 262144 bytes')+'\n');print(result.stdout);assert result.returncode==0
# Lossless PNG re-encoding, no retouching of native draw output.
for path in out.glob('Stars-*.png'):
 blob=path.read_bytes();parts=[];i=8
 while i<len(blob):
  size=struct.unpack('>I',blob[i:i+4])[0];parts.append((blob[i+4:i+8],blob[i+8:i+8+size]));i+=size+12
 raw=zlib.decompress(b''.join(p for tag,p in parts if tag==b'IDAT'));packed=zlib.compress(raw,9);new=bytearray(blob[:8]);done=False
 for tag,data in parts:
  if tag==b'IDAT':
   if done:continue
   data=packed;done=True
  new+=struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
 path.write_bytes(new)
report=[]
for file in ['StarsInspectorPanel.su','CelestialSequence.su','StarsProof.su','StarCatalogueIndex.su']:
 for line in (b/file).read_text().splitlines():
  if any(s in line for s in ['RecordStarsInspector(', 'BuildStarsSheet(', 'ApplyTo(', 'ApplySheet(', 'int main()', 'StarCatalogueIndex::Load(']):
   fields=line.split('\t');size=int(fields[1]);report.append({'function':fields[0],'bytes':size,'classification':fields[2]})
   if not a.sanitize:assert size<=8192,'8KiB entry-point frame budget exceeded'
(out/(mode+'Stack.json')).write_text(json.dumps(report,indent=2)+'\n');(out/(mode+'Commands.json')).write_text(json.dumps(commands,indent=2)+'\n')
inputs=[R/'Engine/Editor/StarsInspectorPanel.cpp',R/'Engine/Editor/StarsInspectorPanel.h',R/'Engine/DisplayPresentation/StarFieldControls.h',R/'Tools/Build/Patches/StarsInspector.patch',Path(__file__),R/'Exhibits/Workbench/Stars/NativePanelProof.cpp',t/'EngineContent/StarCatalogue/BrightStars.bin',*out.glob('Stars-*.png')]
(out/(mode+'Hashes.json')).write_text(json.dumps({str(x.relative_to(R)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},indent=2)+'\n')

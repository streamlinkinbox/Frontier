#!/usr/bin/env python3
"""Capture the already-built native integration, without changing its proof results.
Run RunIntegrationProof.py first if the matching Release build is not available.
"""
from pathlib import Path
import hashlib,json,shutil,struct,subprocess,zlib
R=Path(__file__).resolve().parents[3];T=R/'.cache/cpp-sun-full';B=R/'.cache/main-editor-release';O=R/'Exhibits/Gallery/MainEditorNative/Editor';O.mkdir(parents=True,exist_ok=True)
commands=json.loads((R/'Exhibits/Gallery/MainEditorNative/ReleaseCommands.json').read_text())
source=R/'Exhibits/Workbench/MainEditor/RenderExhibit.cpp'
compile=next(c[:] for c in commands if any(x.endswith('/NativeIntegrationProof.cpp') for x in c))
compile[compile.index('-c')+1]=str(source);compile[compile.index('-o')+1]=str(B/'RenderExhibit.o')
link=commands[-1][:];link=[str(B/'RenderExhibit.o') if x.endswith('/NativeIntegrationProof.o') else x for x in link];link[link.index('-o')+1]=str(B/'RenderExhibit')
# A real runtime asset root: the pinned editor fonts plus the exact shipped icon sources.
shutil.copytree(R/'EngineContent/Icons',T/'EngineContent/Icons',dirs_exist_ok=True)
for cmd in [compile,link]:subprocess.run(cmd,cwd=T,check=True)
run=[str(B/'RenderExhibit'),str(O)];result=subprocess.run(run,cwd=T,check=True,capture_output=True,text=True);print(result.stdout)
# Lossless PNG compression only. No visual edits to native draw output.
for p in O.glob('*.png'):
 blob=p.read_bytes();parts=[];i=8
 while i<len(blob):
  n=struct.unpack('>I',blob[i:i+4])[0];parts.append((blob[i+4:i+8],blob[i+8:i+8+n]));i+=n+12
 data=zlib.compress(zlib.decompress(b''.join(d for t,d in parts if t==b'IDAT')),9);out=bytearray(blob[:8]);done=False
 for t,d in parts:
  if t==b'IDAT':
   if done:continue
   d=data;done=True
  out+=struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
 p.write_bytes(out)
inputs=[source,Path(__file__),R/'Engine/DisplayPresentation/IconArt.cpp',R/'Engine/DisplayPresentation/BakedIconArt.h',*list((R/'EngineContent/Icons/Baked').glob('*')),*O.glob('*.png'),O/'captures.json',*list((T/'EngineContent/FontArchives').rglob('*.ttf')),*list((R/'EngineContent/Icons').glob('*.svg'))]
(O/'Provenance.json').write_text(json.dumps({'mode':'Native EditorHost CPU capture, not GPU execution','fixture':'No imported level; environment eyes enabled; fixed 1/60 simulation step; project defaults otherwise','runtimeRoot':str(T),'commands':[compile,link,run],'output':result.stdout,'sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}},indent=2)+'\n')

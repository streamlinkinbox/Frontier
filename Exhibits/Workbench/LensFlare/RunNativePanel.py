#!/usr/bin/env python3
"""Reconstruct pinned integration, regress Sun, then test native Lens Flare. No GPU claim."""
import argparse, hashlib, json, os, resource, subprocess
from pathlib import Path
import struct, zlib
def compress_png(path):
    """Lossless encoding only; preserve all native scanlines and non-IDAT chunks."""
    blob=path.read_bytes();assert blob[:8]==b'\x89PNG\r\n\x1a\n'
    parts=[];at=8
    while at<len(blob):
        size=struct.unpack('>I',blob[at:at+4])[0];tag=blob[at+4:at+8];payload=blob[at+8:at+8+size]
        parts.append((tag,payload));at+=size+12
    raw=zlib.decompress(b''.join(payload for tag,payload in parts if tag==b'IDAT'))
    packed=zlib.compress(raw,9);result=bytearray(blob[:8]);written=False
    for tag,payload in parts:
        if tag==b'IDAT':
            if written:continue
            payload=packed;written=True
        result+=struct.pack('>I',len(payload))+tag+payload+struct.pack('>I',zlib.crc32(tag+payload)&0xffffffff)
    assert zlib.decompress(packed)==raw
    path.write_bytes(result)
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--debug',action='store_true');p.add_argument('--sanitize',action='store_true');p.add_argument('--reuse-build',action='store_true');a=p.parse_args()
mode='Sanitized' if a.sanitize else 'Debug' if a.debug else 'Release'
option=['--sanitize'] if a.sanitize else ['--debug'] if a.debug else []
if not a.reuse_build:subprocess.run(['python3',str(R/'Exhibits/Workbench/SunInspector/RunFullPanelProof.py'),*option],cwd=R,check=True)
b=R/'.cache'/('sun-full-sanitized' if a.sanitize else 'sun-full-debug' if a.debug else 'sun-full-build')
out=R/'Exhibits/Gallery/LensFlareNative';out.mkdir(parents=True,exist_ok=True)
commands=json.loads((R/'Exhibits/Gallery/SunFullPanel'/(mode+'Commands.json')).read_text())
compile=next(c[:] for c in commands if any(x.endswith('/FullPanelProof.cpp') for x in c))
compile[compile.index('-c')+1]=str(R/'Exhibits/Workbench/LensFlare/NativePanelProof.cpp');compile[compile.index('-o')+1]=str(b/'LensFlareProof.o')
subprocess.run(compile,cwd=R,check=True)
link=commands[-1][:];link=[str(b/'LensFlareProof.o') if x.endswith('/FullPanelProof.o') else str(b/'LensFlareProof') if x.endswith('/FullPanelProof') else x for x in link];subprocess.run(link,cwd=R,check=True)
def limit():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
result=subprocess.run([str(b/'LensFlareProof')],cwd=R,env=env,preexec_fn=None if a.sanitize else limit,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/(mode+'Proof.txt')).write_text(result.stdout+f'Exit: {result.returncode}; '+('sanitizer default stack' if a.sanitize else 'Linux stack limit: 262144 bytes')+'\n');print(result.stdout);assert result.returncode==0
for path in out.glob('*.png'):
 compress_png(path)
report=[]
for file in ['CelestialSequence.su','LensFlareInspectorPanel.su','LensFlareProof.su']:
 for line in (b/file).read_text().splitlines():
  if any(s in line for s in ['BuildFlareSheet(', 'RecordLensFlareInspector(', 'void Frontier::{anonymous}::Update(', 'int main()', 'ApplySheet(']):
   fields=line.split('\t');size=int(fields[1]);report.append({'function':fields[0],'bytes':size,'classification':fields[2]})
   if not a.sanitize:assert size<=8192,'8KiB entry-point frame budget exceeded'
(out/(mode+'Stack.json')).write_text(json.dumps(report,indent=2)+'\n')
inputs=[R/'Engine/Editor/LensFlareInspectorPanel.cpp',R/'Engine/Editor/LensFlareInspectorPanel.h',R/'Engine/DisplayPresentation/LensFlareKernel.h',R/'Tools/Build/Patches/LensFlareInspector.patch',R/'Exhibits/Workbench/LensFlare/NativePanelProof.cpp',R/'Exhibits/Workbench/LensFlare/RunNativePanel.py',*out.glob('*.png')]
(out/(mode+'Hashes.json')).write_text(json.dumps({str(x.relative_to(R)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},indent=2)+'\n')
(out/(mode+'Commands.json')).write_text(json.dumps([compile,link],indent=2)+'\n')

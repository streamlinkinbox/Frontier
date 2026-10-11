#!/usr/bin/env python3
"""Reconstruct integration, regress Sun/Lens Flare, then prove the actual sky bake preview."""
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
if not a.reuse_build:subprocess.run(['python3',str(R/'Exhibits/Workbench/LensFlare/RunNativePanel.py'),*option],cwd=R,check=True)
b=R/'.cache'/('sun-full-sanitized' if a.sanitize else 'sun-full-debug' if a.debug else 'sun-full-build')
out=R/'Exhibits/Gallery/AtmosphereSky';out.mkdir(parents=True,exist_ok=True)
commands=json.loads((R/'Exhibits/Gallery/SunFullPanel'/(mode+'Commands.json')).read_text())
compile=next(c[:] for c in commands if any(x.endswith('/FullPanelProof.cpp') for x in c))
compile[compile.index('-c')+1]=str(R/'Exhibits/Workbench/AtmosphereSky/NativePanelProof.cpp');compile[compile.index('-o')+1]=str(b/'AtmosphereSkyProof.o')
subprocess.run(compile,cwd=R,check=True)
# SpaceExport includes scene declarations which require headers only, not a Vulkan device/library.
import runpy
headers=R/'.cache/vulkan-headers-sky'
runpy.run_path(str(R/'Exhibits/Workbench/IconArt/PrepareTarget.py'))['acquire']('KhronosGroup/Vulkan-Headers','b379292b2ab6df5771ba9870d53cf8b2c9295daf',headers)
extra=[];extra_commands=[]
for name in ['SpaceCodec','SpaceExport']:
 command=compile[:]+["-I"+str(headers/"include")];command[command.index('-c')+1]=str(R/'.cache/cpp-sun-full/Engine/ContentInterchange'/(name+'.cpp'));command[command.index('-o')+1]=str(b/(name+'.o'))
 subprocess.run(command,cwd=R,check=True);extra.append(str(b/(name+'.o')));extra_commands.append(command)
link=commands[-1][:];link=[str(b/'AtmosphereSkyProof.o') if x.endswith('/FullPanelProof.o') else str(b/'AtmosphereSkyProof') if x.endswith('/FullPanelProof') else x for x in link];link+=extra;subprocess.run(link,cwd=R,check=True)
def limit():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
result=subprocess.run([str(b/'AtmosphereSkyProof')],cwd=R,env=env,preexec_fn=None if a.sanitize else limit,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/(mode+'Proof.txt')).write_text(result.stdout+f'Exit: {result.returncode}; '+('sanitizer default stack' if a.sanitize else 'Linux stack limit: 262144 bytes')+'\n');print(result.stdout);assert result.returncode==0
for path in out.glob('*.png'):
 compress_png(path)
report=[]
for file in ['CelestialSequence.su','AtmosphereSkyInspectorPanel.su','AtmosphereSkyProof.su']:
 for line in (b/file).read_text().splitlines():
  if any(s in line for s in ['BuildAtmosphereSkySheet(', 'RecordAtmosphereSkyInspector(', 'void Frontier::{anonymous}::Update(', 'int main()', 'ApplySheet(']):
   fields=line.split('\t');size=int(fields[1]);report.append({'function':fields[0],'bytes':size,'classification':fields[2]})
   if not a.sanitize:assert size<=8192,'8KiB entry-point frame budget exceeded'
(out/(mode+'Stack.json')).write_text(json.dumps(report,indent=2)+'\n')
inputs=[R/'Engine/Editor/AtmosphereSkyInspectorPanel.cpp',R/'Engine/Editor/AtmosphereSkyInspectorPanel.h',R/'Engine/DisplayPresentation/SkyBakePreview.h',R/'Tools/Build/Patches/AtmosphereSkyInspector.patch',R/'Tools/Build/Patches/AtmosphereOverview.patch',R/'Exhibits/Workbench/AtmosphereSky/NativePanelProof.cpp',R/'Exhibits/Workbench/AtmosphereSky/RunNativePanel.py',*out.glob('*.png')]
(out/(mode+'Hashes.json')).write_text(json.dumps({str(x.relative_to(R)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},indent=2)+'\n')
(out/(mode+'Commands.json')).write_text(json.dumps([compile,*extra_commands,link],indent=2)+'\n')

#!/usr/bin/env python3
"""Restore reviewed C++ dependencies, verify Git blob hashes, stage task-only overlays.
Requires the configured gh connection; does not change branches or the immutable baseline.
"""
import base64, hashlib, json, os, re, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
PIN='f6c99702c769ef9fd44c39ac8819e0ebeae35b9b'
BASE=ROOT/'.cache/cpp-sun-full'
STAGE=ROOT/'.cache/automotive-showcase'
OVERLAYS=[
 'Engine/ContentInterchange/'+n for n in ['ShowcaseStructure.cpp','ShowcaseStructure.h','MaterialDescriptor.h','MaterialIndex.cpp','MaterialIndex.h','MaterialCodec.cpp','AutomotiveShowcasePresets.h']
]+['Engine/Shaders/'+n for n in ['MaterialEvaluation.slang','AutomotiveMaterialProfiles.slang','AutomotiveFlakePaint.slang','AutomotiveShowcase.slang','ReSTIRViewport.slang']]+['Projects/Project-Zero/Host/MaterialLevelViewport.cpp']
SEARCH=['Engine/GeometricRaster','Engine/DisplayPresentation','Engine/ContentInterchange','Engine/DeviceExchange','Engine/FunctionCore','Projects/Project-Zero/Source','Exhibits/Workbench/Materials']
def prepare(extra_sources=()):
    tree=json.loads(subprocess.check_output(['gh','api',f'repos/SultanAladin/Frontier-/git/trees/{PIN}?recursive=1']))
    entries={x['path']:x for x in tree['tree'] if x['type']=='blob'}
    visited=set()
    def fetch(name):
        if name in visited or name not in entries:return
        visited.add(name); dest=BASE/name
        if not dest.exists():
            data=json.loads(subprocess.check_output(['gh','api',f'repos/SultanAladin/Frontier-/contents/{name}?ref={PIN}']))
            dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(base64.b64decode(data['content']))
        data=dest.read_bytes()
        assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==entries[name]['sha'], 'Modified baseline: '+name
        for inc in re.findall(r'^#include\s+"([^\"]+)"',data.decode(),re.M):
            choices=[os.path.normpath(str(Path(name).parent/inc)),inc]+[d+'/'+inc for d in SEARCH]
            match=next((p for p in choices if p in entries),None)
            if match:fetch(match)
            else:
                matches=[p for p in entries if p.endswith('/'+inc)]
                if len(matches)==1:fetch(matches[0])
    for name in OVERLAYS+['Engine/DeviceExchange/OrientationClassifier.cpp','Tools/Build/Gates/ShowcaseLevelGate.cpp','Tools/Build/Gates/ShowcaseLevelGateCodecStub.cpp','Engine/ContentInterchange/PngWriteCounterpart.h']+list(extra_sources):
        fetch(name)
    for name in visited:
        dest=STAGE/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(BASE/name,dest)
    for name in OVERLAYS:
        dest=STAGE/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,dest)
    (STAGE/'stub/vulkan').mkdir(parents=True,exist_ok=True)
    handles='VkPhysicalDevice VkDevice VkQueue VkCommandPool VkCommandBuffer VkImage VkBuffer VkInstance'
    (STAGE/'stub/vulkan/vulkan.h').write_text('#pragma once\n#include <cstdint>\n'+''.join(f'typedef struct {h}_T* {h};\n' for h in handles.split()))
    return sorted(visited)
if __name__=='__main__':print('Verified/staged',len(prepare()),'pinned dependencies')

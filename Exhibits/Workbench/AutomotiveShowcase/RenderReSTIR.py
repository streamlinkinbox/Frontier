#!/usr/bin/env python3
"""Capture the actual default scene through MaterialLevelViewport --restir.
Outputs contain the renderer's own ACES presentation and optional native à-trous filter;
raw accumulated images are kept too. No swatch renderer or image generator is used.
"""
import hashlib,json,subprocess,time
from pathlib import Path
from PrepareTarget import ROOT,STAGE,OVERLAYS
from BuildReSTIR import SOURCES
O=ROOT/'Exhibits/Gallery/AutomotiveShowcase/ReSTIR';O.mkdir(parents=True,exist_ok=True)
views=[('grid400',512,32)]+[(f'paint-{n}',384,16) for n in ['candy','glitter','iridescent','cobalt','copper']]+[('paint-glitter-macro',384,16)]
manifest=O/'RenderProof.json'
previous=json.loads(manifest.read_text()) if manifest.exists() else None
results=previous['renders'] if previous else []
if previous:
    for path,digest in previous['sourceSha256'].items():
        assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==digest,'Source changed: '+path
    for name,digest in previous['artifactSha256'].items():
        assert hashlib.sha256((O/name).read_bytes()).hexdigest()==digest,'Artifact changed: '+name
for name,size,frames in views:
    if any(r['view']==name and r['resolution']==[size,size] and r['frames']==frames for r in results):continue
    cmd=[str(STAGE/'MaterialLevelViewport'),'--level','showcase','--view',name,'--restir','--width',str(size),'--height',str(size),'--frames',str(frames),'--spp','1','--sun','13','--threads','8','--denoise','--denoise-levels','3','--raw-out',str(O/(name+'-raw.png')),'--out',str(O/(name+'.png'))]
    start=time.time();print('Rendering',name,size,frames,flush=True)
    with (O/(name+'.log')).open('w') as log:r=subprocess.run(cmd,cwd=STAGE,stdout=log,stderr=subprocess.STDOUT)
    text=(O/(name+'.log')).read_text()
    assert r.returncode==0 and ', 0 non-finite/out-of-range samples' in text,name+' failed'
    assert 'RESTIR DI (CPU mirror)' in text and 'reservoirs:' in text,'wrong rendering path'
    results.append({'view':name,'command':cmd,'exit':r.returncode,'seconds':round(time.time()-start,2),'resolution':[size,size],'frames':frames,'estimator':'native C++ ReSTIR DI + GI mirror','hardwareGpu':False,'denoiseLevels':3})
    print('Completed',name,results[-1]['seconds'],'seconds',flush=True)
    inputs=set(STAGE/s for s in SOURCES)|set(ROOT/s for s in OVERLAYS)|set((STAGE/'Engine/Shaders').glob('*.slang'))
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    (O/'RenderProof.json').write_text(json.dumps({'renders':results,'build':json.loads((STAGE/'restir-build.json').read_text()),'binarySha256':sha(STAGE/'MaterialLevelViewport'),'sourceSha256':{str(p.relative_to(ROOT)):sha(p) for p in sorted(inputs)},'artifactSha256':{p.name:sha(p) for p in O.iterdir() if p.suffix in ['.png','.log']}},indent=2)+'\n')

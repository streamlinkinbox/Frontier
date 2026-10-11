#!/usr/bin/env python3
"""Record exact artifact identities and compiler invocations for the CPU icon run."""
import hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
out=ROOT/'Exhibits/Gallery/IconArt'
for name,filename in [('release','ReleaseCommands.json'),('sanitized','SanitizerCommands.json')]:
    shutil.copy2(ROOT/'.cache/icon-art'/name/'Commands.json',out/filename)
fixtures=out/'Fixtures';fixtures.mkdir(exist_ok=True)
for p in (ROOT/'.cache/icon-art/fixtures').glob('*.png'):shutil.copy2(p,fixtures/p.name)
files=sorted([*(out/'Raster').glob('*.png'),*(out/'Reference').glob('*.png'),*out.glob('IconArt*.png')])
(out/'ImageHashes.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(out))+'\n' for p in files))
comparison=json.loads((out/'Compatibility.json').read_text())['icons']
meta={'targetRevision':'f6c99702c769ef9fd44c39ac8819e0ebeae35b9b','webRevision':'41cf997','thorvgRevision':'6715f99ac106b6d4587f384a78c59fe957cfd2a3','thorvgVersion':'1.0.0','thorvgCorrection':'Tools/Build/StageThorVG.py; removes global new/delete replacements','rgba':'straight-alpha RGBA8; sRGB colour; 4 * width byte stride','rendering':'C++ ThorVG SwCanvas only; no GPU; not a React screenshot','reference':'@napi-rs/canvas SVG decoder at explicit 128px intrinsic extent','iconResults':{'admitted':sum(i['result']=='ready' for i in comparison),'unsupported':sum(i['result']=='unsupported' for i in comparison),'withinReferenceTolerance':sum(i['fidelity']=='WITHIN_TOLERANCE' for i in comparison)},'presentation':'C++ icon proof sheet compositing; ImGui icon-texture presentation is deferred to outliner integration'}
(out/'Provenance.json').write_text(json.dumps(meta,indent=2)+'\n')

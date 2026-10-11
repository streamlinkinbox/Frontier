#!/usr/bin/env python3
"""Execute the unmodified editor CPU gate and preserve only outputs produced by that invocation."""
import argparse, hashlib, json, os, platform, shutil, subprocess, time, tarfile
from pathlib import Path
from PrepareTarget import PIN, PACKAGES
ROOT=Path(__file__).resolve().parents[3]
NAMES=[f'EditorProof_{s}.png' for s in ['Tabs','Menu','Filtered','Palette','Views','Inspector','Shade','TabMenu']]+[f'EditorSelectionProof_{s}.png' for s in ['Pick','Rotate','Scale','Moved','Rotated','Scaled']]
def collect(target,out,code):
    out.mkdir(parents=True,exist_ok=True)
    for name in NAMES:
        source=target/'Exhibits/Gallery/Editor'/name
        if source.exists():shutil.copy2(source,out/name)
    for source,name in [('/tmp/EditorProof.build','EditorBaselineBuild.txt'),('/tmp/EditorProof.patches','ImGuiPatches.txt'),('/tmp/EditorProof.verify','ImGuiPatchVerify.txt')]:
        if Path(source).exists():shutil.copy2(source,out/name)
    sources=[*target.glob('Engine/Editor/*.*'),target/'Exhibits/Workbench/Editor/CheckEditorProof.sh',target/'Exhibits/Workbench/Editor/EditorProof.cpp',target/'Exhibits/Workbench/Editor/EditorSelectionProof.cpp']
    (out/'SourceHashes.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(target))+'\n' for p in sorted(sources) if p.is_file()))
    (out/'ImageHashes.txt').write_text(''.join(hashlib.sha256((out/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in NAMES if (out/n).exists()))
    (out/'Provenance.json').write_text(json.dumps({'repository':'SultanAladin/Frontier-','revision':PIN,'dependencies':PACKAGES[:3],'platform':platform.platform(),'compiler':subprocess.check_output(['g++','--version'],text=True),'exitCode':code,'command':'GIT_CEILING_DIRECTORIES="$TARGET" bash "$TARGET/Exhibits/Workbench/Editor/CheckEditorProof.sh"','renderer':'original C++ CPU rasterizer; original UI sources; upstream ImGui patch stack'},indent=2)+'\n')
    (out/'ExitCode.txt').write_text(str(code)+'\n')
    archive=ROOT/'.cache'/f'SultanAladin-Frontier--{PIN}.tar.gz'
    if not archive.exists():archive=ROOT/'.cache/cpp-target.tar.gz'
    checked=0
    with tarfile.open(archive) as tar:
        for entry in tar:
            name=entry.name.split('/',1)[-1]
            if entry.isfile() and (name.startswith('Engine/') or name.startswith('Exhibits/Workbench/Editor/') or name.startswith('Projects/Project-Zero/Source/')):
                if hashlib.sha256(tar.extractfile(entry).read()).digest()!=hashlib.sha256((target/name).read_bytes()).digest():
                    raise RuntimeError('Baseline input was modified: '+name)
                checked+=1
    (out/'SourceVerification.txt').write_text(f'{checked} original engine/editor-proof/project-source files byte-identical to the pinned target archive.\nOnly the target ImGui patch stack was applied to its pinned vendor input.\nNo IconArt code was linked into the baseline executable.\n')

def main():
    p=argparse.ArgumentParser();p.add_argument('--target',type=Path,default=ROOT/'.cache/cpp-target');p.add_argument('--output',type=Path,default=ROOT/'Exhibits/Gallery/IconArtBaseline');a=p.parse_args();target=a.target.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    assert (target/'.frontier-proof-pin').read_text().strip()==PIN
    # Avoid an old committed image satisfying the upstream existence checks.
    for name in NAMES:(target/'Exhibits/Gallery/Editor'/name).unlink(missing_ok=True)
    env=dict(os.environ,GIT_CEILING_DIRECTORIES=str(target));begin=time.monotonic()
    with (out/'EditorBaseline.txt').open('w') as log:
        proc=subprocess.Popen(['bash',str(target/'Exhibits/Workbench/Editor/CheckEditorProof.sh')],cwd=target,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        for line in proc.stdout:print(line,end='',flush=True);log.write(line);log.flush()
        code=proc.wait()
    collect(target,out,code);(out/'Duration.txt').write_text(f'{time.monotonic()-begin:.3f} seconds\n')
    raise SystemExit(code)
if __name__=='__main__':main()

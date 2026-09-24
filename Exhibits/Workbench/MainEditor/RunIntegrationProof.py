#!/usr/bin/env python3
"""Run after RunFullPanelProof.py reconstructs the completed inspector target."""
from pathlib import Path
import json,subprocess,re,concurrent.futures,os,shutil,argparse,hashlib,resource
R=Path(__file__).resolve().parents[3];T=R/'.cache/cpp-sun-full';B=R/'.cache/main-editor-build';B.mkdir(exist_ok=True);O=R/'Exhibits/Gallery/MainEditorNative';O.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--reuse-target',action='store_true');p.add_argument('--reuse-objects',action='store_true');p.add_argument('--debug',action='store_true');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
mode='Sanitized' if args.sanitize else 'Debug' if args.debug else 'Release'
B=R/'.cache'/('main-editor-'+mode.lower());B.mkdir(exist_ok=True)
if not args.reuse_target:
 subprocess.run(['python3',str(R/'Exhibits/Workbench/SunInspector/RunFullPanelProof.py')],cwd=R,check=True)
 subprocess.run(['git','apply',str(R/'Tools/Build/Patches/MainEditorIntegration.patch')],cwd=T,env=dict(os.environ,GIT_CEILING_DIRECTORIES=str(T)),check=True)
shutil.copy2(R/'Engine/Editor/ViewportBillboards.h',T/'Engine/Editor/ViewportBillboards.h')
shutil.copy2(R/'Projects/Project-Zero/Source/EditorInspectorSequence.h',T/'Projects/Project-Zero/Source/EditorInspectorSequence.h')
for name in ['IconArt.cpp','IconPresentation.cpp','BakedIconArt.h']:shutil.copy2(R/'Engine/DisplayPresentation'/name,T/'Engine/DisplayPresentation'/name)
if not (R/'.cache/icon-art/release/libthorvg.a').exists():subprocess.run(['python3',str(R/'Exhibits/Workbench/IconArt/BuildProof.py')],cwd=R,check=True)
base=json.loads((R/'Exhibits/Gallery/SunFullPanel/ReleaseCommands.json').read_text());template=base[0][:base[0].index('-c')]
# Reconstruct the application syntax command from source paths, not a stale report
# containing another machine's absolute checkout path.
syntax=template+['-I'+str(T/'Engine'/d) for d in ['ContentInterchange','DeviceExchange','DisplayPresentation','Editor','GeometricRaster','PhysicalDynamics','PlatformInterchange','Shaders','SpatialInterface']]+['-I'+str(R/'.cache/vulkan-headers-sky/include'),'-I'+str(T/'Projects/Project-Dyno/Source'),'-fsyntax-only',str(T/'Projects/Project-Zero/Source/GameExecution.cpp')]
template+= [x for x in syntax if x.startswith('-I')]+['-I'+str(T/'Exhibits/Workbench/Editor/Counterparts'),'-I'+str(T/'ExternalPackages/thorvg/inc'),'-DTVG_STATIC','-pthread']
template=[x for x in template if not x.startswith('-O')]+['-O1' if args.sanitize else '-O0' if args.debug else '-O2']
if args.sanitize:template+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
script=(T/'Exhibits/Workbench/Editor/CheckEditorProof.sh').read_text();part=script[script.index('if ! g++'):script.index(' 2>/tmp/EditorProof.build')]
sources=[T/x for x in re.findall(r'(?:Engine|Projects|ExternalPackages)/[\w/]+\.cpp',part)]+[Path(c[c.index('-c')+1]) for c in base[:-1] if '-c' in c and not any('FullPanelProof.cpp' in x for x in c)]
sources += [T/'Engine/DisplayPresentation'/x for x in ['IconArt.cpp','IconPresentation.cpp']]
sources += [T/'Engine/GeometricRaster'/x for x in ['StarCatalogueIndex.cpp','VisibilityRaster.cpp','GeometryStructure.cpp','SceneStructure.cpp']]+[T/'Projects/Project-Zero/Source/EditorFeedSequence.cpp',T/'Projects/Project-Zero/Source/FlyThroughSolver.cpp',R/'Exhibits/Workbench/MainEditor/NativeIntegrationProof.cpp']
sources=list(dict.fromkeys(sources));commands=[template+['-c',str(s),'-o',str(B/(s.stem+'.o'))] for s in sources]
def run(c):
 p=subprocess.run(c,cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 if p.returncode:raise RuntimeError(' '.join(c)+'\n'+p.stdout)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(run,[c for c in commands if not args.reuse_objects or not Path(c[-1]).exists()]))
link=['g++',*(['-fsanitize=address,undefined'] if args.sanitize else []),'-Wl,--gc-sections',*[str(B/(s.stem+'.o')) for s in sources],str(R/'.cache/icon-art/release/libthorvg.a'),'-pthread','-o',str(B/'Proof')];run(link);commands.append(link)
def stack_limit():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
p=subprocess.run([str(B/'Proof')],cwd=R,env=env,preexec_fn=None if args.sanitize else stack_limit,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
print(p.stdout);(O/(mode+'Proof.txt')).write_text(p.stdout+f'Exit: {p.returncode}\n');(O/(mode+'Commands.json')).write_text(json.dumps(commands,indent=2)+'\n');assert p.returncode==0
checks=[]
for f in B.glob('*.su'):
 for line in f.read_text().splitlines():
  if any(n in line for n in ['EditorInspectorSequence::','EditorHost::Record(', 'EditorHost::ConstructLayout(', 'int main()']):checks.append(line)
(O/(mode+'Stack.txt')).write_text('\n'.join(checks)+'\n')
inputs=[R/'Projects/Project-Zero/Source/EditorInspectorSequence.h',R/'Tools/Build/Patches/MainEditorIntegration.patch',Path(__file__),R/'Exhibits/Workbench/MainEditor/NativeIntegrationProof.cpp',*sources]
(O/(mode+'Hashes.json')).write_text(json.dumps({str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},indent=2)+'\n')
syntax_result=subprocess.run(syntax,cwd=T,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(O/'GameSyntax.json').write_text(json.dumps({'command':syntax,'exit':syntax_result.returncode,'output':syntax_result.stdout},indent=2)+'\n');assert syntax_result.returncode==0

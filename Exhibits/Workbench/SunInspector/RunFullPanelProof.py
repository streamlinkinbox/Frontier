#!/usr/bin/env python3
"""Complete native Sun inspector proof and measured Linux stack guard. No window/GPU required."""
import argparse, hashlib, json, os, resource, shutil, subprocess, struct, zlib, tarfile
from pathlib import Path
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
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--debug',action='store_true');a=p.parse_args()
base=R/'.cache/cpp-target';t=R/'.cache/cpp-sun-full';out=R/'Exhibits/Gallery/SunFullPanel';out.mkdir(parents=True,exist_ok=True)
assert (base/'.frontier-proof-pin').read_text().strip()=='f6c99702c769ef9fd44c39ac8819e0ebeae35b9b'
archive=R/'.cache/SultanAladin-Frontier--f6c99702c769ef9fd44c39ac8819e0ebeae35b9b.tar.gz'
count=0
with tarfile.open(archive) as tar:
    for member in tar:
        name=member.name.split('/',1)[-1]
        if member.isfile() and name.startswith(('Engine/','Exhibits/Workbench/Editor/','Projects/Project-Zero/Source/')):
            assert tar.extractfile(member).read()==(base/name).read_bytes(),'Baseline modified: '+name
            count+=1
(out/'BaselineVerification.txt').write_text(f'{count} original engine/editor-proof/project-source files remain byte-identical to the pinned archive. Full-panel integration is in a separate staging tree.\n')
# Rebuild the isolated integration, leaving original engine sources untouched.
if t.exists():shutil.rmtree(t)
shutil.copytree(base,t)
env=dict(os.environ,GIT_CEILING_DIRECTORIES=str(t))
for patch in ['NativeOutliner.patch','SunInspector.patch','FullSunInspector.patch','SunLightingBindings.patch','SunMotionBindings.patch','LensFlareInspector.patch','AtmosphereSkyInspector.patch','MoonInspector.patch','MoonCatalogue.patch','AtmosphereOverview.patch','StarsInspector.patch','CloudsInspector.patch','FogInspector.patch','WeatherInspector.patch','CameraInspector.patch']:
 subprocess.run(['git','apply',str(R/'Tools/Build/Patches'/patch)],cwd=t,env=env,check=True)
for name in ['IconArt.h','IconSymbols.inc','IconPresentation.h']:shutil.copy2(R/'Engine/DisplayPresentation'/name,t/'Engine/DisplayPresentation'/name)
for folder,names in [('Engine/DisplayPresentation',['SunReferenceDraw.h','SunColourTemperature.h','LensFlareKernel.h','SkyBakePreview.h','MoonReferenceDraw.h','MoonAtlasPreview.h','StarFieldControls.h','CloudDensityPreview.h','FogModel.h','WeatherDiagnostics.h','CameraOptics.h','InspectorReferenceDraw.h']),('Engine/Editor',['SunInspectorPanel.h','SunInspectorPanel.cpp','LensFlareInspectorPanel.h','LensFlareInspectorPanel.cpp','AtmosphereSkyInspectorPanel.h','AtmosphereSkyInspectorPanel.cpp','MoonInspectorPanel.h','MoonInspectorPanel.cpp','StarsInspectorPanel.h','StarsInspectorPanel.cpp','CloudsInspectorPanel.h','CloudsInspectorPanel.cpp','FogInspectorPanel.h','FogInspectorPanel.cpp','WeatherInspectorPanel.h','WeatherInspectorPanel.cpp','CameraInspectorPanel.h','CameraInspectorPanel.cpp'])]:
 for name in names:shutil.copy2(R/folder/name,t/folder/name)
shutil.copy2(R/'Projects/Project-Zero/Source/CameraInspectorBinding.h',t/'Projects/Project-Zero/Source/CameraInspectorBinding.h')
shutil.copytree(R/'EngineContent/Fonts/SunReference',t/'EngineContent/Fonts/SunReference',dirs_exist_ok=True)
subprocess.run(['python3','Tools/Build/ApplyImGuiPatches.py'],cwd=t,env=env,check=True)
b=R/'.cache'/('sun-full-sanitized' if a.sanitize else 'sun-full-debug' if a.debug else 'sun-full-build');b.mkdir(exist_ok=True)
inc=[t/x for x in ['Engine','Engine/Editor','Engine/DisplayPresentation','Projects/Project-Zero/Source','ExternalPackages/imgui','ExternalPackages/tomlpp/include','Exhibits/Workbench/Editor']]+[R/'Exhibits/Workbench/IconArt']
flags=['-std=c++20','-O0' if a.debug else '-O1' if a.sanitize else '-O2','-g','-ffunction-sections','-fdata-sections','-fstack-usage','-DFRONTIER_DEVELOPMENT']+['-I'+str(x) for x in inc]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
sources=[t/x for x in ['Projects/Project-Zero/Source/CelestialSequence.cpp','Engine/Editor/InspectorPanel.cpp','Engine/Editor/ControlPanel.cpp','Engine/Editor/SunInspectorPanel.cpp','Engine/Editor/LensFlareInspectorPanel.cpp','Engine/Editor/AtmosphereSkyInspectorPanel.cpp','Engine/Editor/MoonInspectorPanel.cpp','Engine/Editor/StarsInspectorPanel.cpp','Engine/Editor/CloudsInspectorPanel.cpp','Engine/Editor/FogInspectorPanel.cpp','Engine/Editor/WeatherInspectorPanel.cpp','Engine/Editor/CameraInspectorPanel.cpp','Engine/DisplayPresentation/CelestialSolver.cpp']]+[R/'Exhibits/Workbench/SunInspector/FullPanelProof.cpp']+[t/'ExternalPackages/imgui'/x for x in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]
commands=[];objects=[]
for src in sources:
 obj=b/(src.stem+'.o');objects.append(obj)
 cmd=['g++',*flags,*(['-Wall','-Wextra','-Werror'] if src.name in ['SunInspectorPanel.cpp','LensFlareInspectorPanel.cpp','AtmosphereSkyInspectorPanel.cpp','MoonInspectorPanel.cpp','StarsInspectorPanel.cpp','CloudsInspectorPanel.cpp','FogInspectorPanel.cpp','WeatherInspectorPanel.h','WeatherInspectorPanel.cpp','CameraInspectorPanel.h','CameraInspectorPanel.cpp'] else []),'-c',str(src),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,check=True)
cmd=['g++','-Wl,--gc-sections',*[str(x) for x in objects],'-o',str(b/'FullPanelProof')]
if a.sanitize:cmd+=['-fsanitize=address,undefined']
commands.append(cmd);subprocess.run(cmd,check=True)
mode='Sanitized' if a.sanitize else 'Debug' if a.debug else 'Release'
(out/(mode+'Commands.json')).write_text(json.dumps(commands,indent=2)+'\n')
def constrained():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for image in out.glob('Sun-*.png'):image.unlink()
result=subprocess.run([str(b/'FullPanelProof')],cwd=R,env=env,preexec_fn=None if a.sanitize else constrained,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/(mode+'Proof.txt')).write_text(result.stdout+f'Exit: {result.returncode}; '+('sanitizer default stack' if a.sanitize else 'Linux stack limit: 262144 bytes')+'\n')
print(result.stdout);assert result.returncode==0
for image in out.glob('Sun-*.png'):compress_png(image)
if not a.sanitize and not a.debug:
    subprocess.run(['git','apply','--check',str(R/'Tools/Build/Patches/IconArt-TargetCMake.patch')],cwd=t,env=env,check=True)
    syntax=['g++','-std=c++20','-DFRONTIER_DEVELOPMENT','-IEngine','-IEngine/Editor','-IEngine/DisplayPresentation','-IEditor/AuthoringTools/Modelling/SolidArc','-IExternalPackages/imgui','-IExternalPackages/tomlpp/include','-fsyntax-only','Engine/Editor/EditorHost.cpp','Editor/AuthoringTools/Modelling/SolidArc/Editor/SolidArcEditorHost.cpp']
    with (out/'HostSyntax.txt').open('w') as log:
        log.write(' '.join(syntax)+'\n');log.flush()
        subprocess.run(syntax,cwd=t,stdout=log,stderr=subprocess.STDOUT,check=True)
        log.write('PASS syntax only; CMake integration patch apply-check passed. No full application/device build claimed.\n')
# Record actual function frames for this optimization mode, including the full renderer.
report=[]
for name in ['CelestialSequence','InspectorPanel','SunInspectorPanel','FullPanelProof']:
 for line in (b/(name+'.su')).read_text().splitlines():
  if any(n in line for n in ['::BuildSheet(', '::BuildSunSheet(', '::ApplySheet(', '::Record(', 'RecordSunInspector(', '::Bake(', '::Native(', '::SolvedSlider(', 'int main()']):
   fields=line.split('\t');report.append({'function':fields[0],'bytes':int(fields[1]),'classification':fields[2]})
   if not a.sanitize:assert int(fields[1])<=8192,'8KiB per-function budget exceeded'
(out/(mode+'Stack.json')).write_text(json.dumps(report,indent=2)+'\n')
(out/(mode+'Hashes.json')).write_text(json.dumps({str(x.relative_to(R)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [R/'Tools/Build/Patches/FullSunInspector.patch',R/'Tools/Build/Patches/SunLightingBindings.patch',R/'Tools/Build/Patches/SunMotionBindings.patch',R/'Tools/Build/Patches/LensFlareInspector.patch',R/'Tools/Build/Patches/AtmosphereSkyInspector.patch',R/'Engine/DisplayPresentation/SkyBakePreview.h',R/'Engine/Editor/AtmosphereSkyInspectorPanel.h',R/'Engine/Editor/AtmosphereSkyInspectorPanel.cpp',R/'Engine/DisplayPresentation/LensFlareKernel.h',R/'Engine/Editor/LensFlareInspectorPanel.h',R/'Engine/Editor/LensFlareInspectorPanel.cpp',R/'Engine/DisplayPresentation/SunColourTemperature.h',R/'Engine/Editor/SunInspectorPanel.cpp',R/'Engine/Editor/SunInspectorPanel.h',R/'Engine/DisplayPresentation/SunReferenceDraw.h',R/'EngineContent/Fonts/SunReference/DMSans-Regular.ttf',R/'EngineContent/Fonts/SunReference/DMSans-Light.ttf',R/'Exhibits/Workbench/SunInspector/FullPanelProof.cpp',*out.glob('Sun-*.png')]},indent=2)+'\n')

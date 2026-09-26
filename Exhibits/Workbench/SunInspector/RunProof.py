#!/usr/bin/env python3
"""Native Sun inspector proof and measured Linux stack guard. No window/GPU required."""
import argparse, hashlib, json, os, resource, shutil, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--debug',action='store_true');a=p.parse_args()
base=R/'.cache/cpp-target';t=R/'.cache/cpp-sun';out=R/'Exhibits/Gallery/SunInspector';out.mkdir(parents=True,exist_ok=True)
assert (base/'.frontier-proof-pin').read_text().strip()=='f6c99702c769ef9fd44c39ac8819e0ebeae35b9b'
# Rebuild the isolated integration, leaving original engine sources untouched.
if t.exists():shutil.rmtree(t)
shutil.copytree(base,t)
env=dict(os.environ,GIT_CEILING_DIRECTORIES=str(t))
for patch in ['NativeOutliner.patch','SunInspector.patch']:
 subprocess.run(['git','apply',str(R/'Tools/Build/Patches'/patch)],cwd=t,env=env,check=True)
for name in ['IconArt.h','IconSymbols.inc']:shutil.copy2(R/'Engine/DisplayPresentation'/name,t/'Engine/DisplayPresentation'/name)
subprocess.run(['python3','Tools/Build/ApplyImGuiPatches.py'],cwd=t,env=env,check=True)
b=R/'.cache'/('sun-sanitized' if a.sanitize else 'sun-debug' if a.debug else 'sun-build');b.mkdir(exist_ok=True)
inc=[t/x for x in ['Engine','Engine/Editor','Engine/DisplayPresentation','Projects/Project-Zero/Source','ExternalPackages/imgui','ExternalPackages/tomlpp/include','Exhibits/Workbench/Editor']]+[R/'Exhibits/Workbench/IconArt']
flags=['-std=c++20','-O0' if a.debug else '-O2','-g','-ffunction-sections','-fdata-sections','-fstack-usage','-DFRONTIER_DEVELOPMENT']+['-I'+str(x) for x in inc]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
sources=[t/x for x in ['Projects/Project-Zero/Source/CelestialSequence.cpp','Engine/Editor/InspectorPanel.cpp','Engine/Editor/ControlPanel.cpp','Engine/DisplayPresentation/CelestialSolver.cpp']]+[R/'Exhibits/Workbench/SunInspector/SunProof.cpp']+[t/'ExternalPackages/imgui'/x for x in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]
commands=[];objects=[]
for src in sources:
 obj=b/(src.stem+'.o');objects.append(obj)
 cmd=['g++',*flags,'-c',str(src),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,check=True)
cmd=['g++','-Wl,--gc-sections',*[str(x) for x in objects],'-o',str(b/'SunProof')]
if a.sanitize:cmd+=['-fsanitize=address,undefined']
commands.append(cmd);subprocess.run(cmd,check=True)
mode='Sanitized' if a.sanitize else 'Debug' if a.debug else 'Release'
(out/(mode+'Commands.json')).write_text(json.dumps(commands,indent=2)+'\n')
def constrained():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
result=subprocess.run([str(b/'SunProof')],cwd=R,env=env,preexec_fn=None if a.sanitize else constrained,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/(mode+'Proof.txt')).write_text(result.stdout+f'Exit: {result.returncode}; '+('sanitizer default stack' if a.sanitize else 'Linux stack limit: 262144 bytes')+'\n')
print(result.stdout);assert result.returncode==0
# Compare production entry points at both release and debug optimization, including original inputs.
if not a.sanitize and not a.debug:
 report=[]
 for label,target in [('Before',base),('Sun',t)]:
  for opt in ['-O0','-O2']:
   for name,path in [('InspectorPanel','Engine/Editor/InspectorPanel.cpp'),('CelestialSequence','Projects/Project-Zero/Source/CelestialSequence.cpp')]:
    obj=b/(label+opt+name+'.o')
    cmd=['g++','-std=c++20',opt,'-fstack-usage','-DFRONTIER_DEVELOPMENT','-I'+str(target/'Engine'),'-I'+str(t/'ExternalPackages/imgui'),'-c',str(target/path),'-o',str(obj)]
    subprocess.run(cmd,check=True)
    for line in obj.with_suffix('.su').read_text().splitlines():
     if any(n in line for n in ['::BuildSheet(', '::BuildSunSheet(', '::BuildOtherSheet(', '::ApplySheet(', '::RecordCard(', '::Record(']):
      fields=line.split('\t');report.append({'version':label,'optimization':opt,'function':fields[0].split(':',3)[-1],'bytes':int(fields[1]),'classification':fields[2]})
      if label=='Sun' and '::BuildOtherSheet(' not in line:assert int(fields[1])<=8192,'Per-function 8KiB budget exceeded'
 (out/'Stack.json').write_text(json.dumps(report,indent=2)+'\n')
(out/(mode+'Hashes.json')).write_text(json.dumps({str(x.relative_to(R)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [R/'Tools/Build/Patches/SunInspector.patch',R/'Exhibits/Workbench/SunInspector/SunProof.cpp',*out.glob('Sun-*.png')]},indent=2)+'\n')

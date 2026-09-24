#!/usr/bin/env python3
"""Compile the source-transcribed Sun drawings, independently of the rejected card layout."""
import argparse, hashlib, json, os, resource, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--debug',action='store_true');a=p.parse_args()
mode='Sanitized' if a.sanitize else 'Debug' if a.debug else 'Release'
t=R/'.cache/cpp-target';b=R/'.cache'/('sun-reference-'+mode);b.mkdir(parents=True,exist_ok=True)
o=R/'Exhibits/Gallery/SunReference';o.mkdir(parents=True,exist_ok=True)
assert (t/'.frontier-proof-pin').read_text().strip()=='f6c99702c769ef9fd44c39ac8819e0ebeae35b9b'
inc=[R/'Engine/DisplayPresentation',R/'Exhibits/Workbench/IconArt',t/'ExternalPackages/imgui',t/'Exhibits/Workbench/Editor']
flags=['-std=c++20','-O0' if a.debug else '-O1' if a.sanitize else '-O2','-g','-fstack-usage']+['-I'+str(x) for x in inc]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
sources=[R/'Exhibits/Workbench/SunReference/DrawProof.cpp']+[t/'ExternalPackages/imgui'/n for n in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]
commands=[];objs=[]
for src in sources:
 obj=b/(src.stem+'.o');objs.append(obj);cmd=['g++',*flags,'-c',str(src),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,check=True)
cmd=['g++',*[str(x) for x in objs],'-o',str(b/'DrawProof')]
if a.sanitize:cmd+=['-fsanitize=address,undefined']
commands.append(cmd);subprocess.run(cmd,check=True)
(o/(mode+'Commands.json')).write_text(json.dumps(commands,indent=2)+'\n')
stack=(b/'DrawProof.su').read_text();(o/(mode+'Stack.txt')).write_text(stack)
for line in stack.splitlines():
 if 'SunReference' in line or 'main()' in line: assert int(line.split('\t')[1])<=8192,'8KiB function-frame budget exceeded'
def limit():resource.setrlimit(resource.RLIMIT_STACK,(256*1024,256*1024))
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
r=subprocess.run([str(b/'DrawProof')],cwd=R,env=env,preexec_fn=None if a.sanitize else limit,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(o/(mode+'Proof.txt')).write_text(r.stdout+f'Exit {r.returncode}; '+('default stack under sanitizers' if a.sanitize else '256KiB Linux stack limit')+'\n')
print(r.stdout);assert r.returncode==0
files=[R/'Experimental/FrontierEditor/environment-graphics.jsx',R/'Experimental/FrontierEditor/src.jsx',R/'Experimental/FrontierEditor/style.css',R/'Engine/DisplayPresentation/SunReferenceDraw.h',R/'EngineContent/Fonts/SunReference/DMSans-Regular.ttf',*o.glob('Native-*.png')]
(o/(mode+'Hashes.json')).write_text(json.dumps({str(x.relative_to(R)):hashlib.sha256(x.read_bytes()).hexdigest() for x in files},indent=2)+'\n')

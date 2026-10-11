#!/usr/bin/env python3
import argparse, json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');args=p.parse_args()
imgui=ROOT/'.cache/cpp-target/ExternalPackages/imgui'
mode='sanitized' if args.sanitize else 'release'
out=ROOT/'Exhibits/Gallery/NativeOutliner';out.mkdir(parents=True,exist_ok=True)
cmd=['g++','-std=c++20','-O1','-g','-pthread','-DTVG_STATIC','-Wall','-Wextra','-Werror','-I'+str(imgui),'-I'+str(ROOT/'Engine/DisplayPresentation'),'-I'+str(ROOT/'.cache/cpp-target/ExternalPackages/thorvg/inc')]
if args.sanitize: cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd += [str(ROOT/'Engine/DisplayPresentation'/n) for n in ['IconArt.cpp','IconPresentation.cpp']]
cmd += [str(imgui/n) for n in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]
cmd += [str(ROOT/'Exhibits/Workbench/IconArt/PresentationProof.cpp'),str(ROOT/'.cache/icon-art'/mode/'libthorvg.a'),'-o',str(ROOT/'.cache'/('PresentationProof-'+mode))]
(out/('PresentationCommand-'+mode+'.json')).write_text(json.dumps(cmd,indent=2)+'\n')
subprocess.run(cmd,check=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
result=subprocess.run([cmd[-1]],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/('Presentation-'+mode+'.log')).write_text(result.stdout)
print(result.stdout)
raise SystemExit(result.returncode)

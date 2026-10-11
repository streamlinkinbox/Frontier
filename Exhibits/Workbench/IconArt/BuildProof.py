#!/usr/bin/env python3
"""Build a pinned ThorVG software renderer and the dedicated C++ icon proof, without any graphics SDK."""
import argparse, concurrent.futures, hashlib, json, os, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser();parser.add_argument('--target',type=Path,default=ROOT/'.cache/cpp-target');parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
tvg=args.target.resolve()/'ExternalPackages/thorvg'
expected='6715f99ac106b6d4587f384a78c59fe957cfd2a3'
assert (tvg/'.frontier-proof-pin').read_text().strip()==expected, 'Run PrepareTarget.py first.'
build=ROOT/'.cache/icon-art'/('sanitized' if args.sanitize else 'release');build.mkdir(parents=True,exist_ok=True)
config='#pragma once\n#define THORVG_VERSION_STRING "1.0.0"\n#define THORVG_SW_RASTER_SUPPORT 1\n#define THORVG_SVG_LOADER_SUPPORT 1\n#define THORVG_THREAD_SUPPORT 1\n#define THORVG_FILE_IO_SUPPORT 1\n'
(build/'config.h').write_text(config)
dirs=['src/common','src/renderer','src/renderer/sw_engine','src/loaders/svg','src/loaders/raw']
sys.path.insert(0,str(ROOT/'Tools/Build'))
from StageThorVG import stage
staged=stage(tvg,build/'tvgInitializer.cpp')
sources=sorted(staged if p.name=='tvgInitializer.cpp' else p for d in dirs for p in (tvg/d).glob('*.cpp'))
includes=['-I'+str(tvg/'inc'),'-I'+str(build)]+['-I'+str(tvg/d) for d in dirs]
flags=['-std=c++20','-pthread','-DTVG_STATIC','-DTVG_BUILD','-fPIC','-O1' if args.sanitize else '-O2']
if args.sanitize:flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
compiler=os.environ.get('CXX','g++'); commands=[]
def compile(path):
    obj=build/(path.stem+'.o'); command=[compiler,*flags,*includes,'-c',str(path),'-o',str(obj)]
    signature=hashlib.sha256((json.dumps(command)+path.read_text()+config+expected).encode()).hexdigest();marker=obj.with_suffix('.sha256')
    if not obj.exists() or not marker.exists() or marker.read_text()!=signature:
        print('Compiling '+path.name,flush=True);subprocess.run(command,check=True);marker.write_text(signature)
    return obj,command
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:compiled=list(ex.map(compile,sources))
lib=build/'libthorvg.a';subprocess.run(['ar','rcs',str(lib),*[str(obj) for obj,_ in compiled]],check=True)
command=[compiler,*flags,'-Wall','-Wextra','-Werror','-Wno-unused-function','-I'+str(tvg/'inc'),'-isystem',str(args.target.resolve()/'ExternalPackages/stb'),'-I'+str(ROOT/'Engine/DisplayPresentation'),str(ROOT/'Engine/DisplayPresentation/IconArt.cpp'),str(ROOT/'Exhibits/Workbench/IconArt/IconArtProof.cpp'),str(lib),'-o',str(build/'IconArtProof')]
subprocess.run(command,check=True)
(build/'Commands.json').write_text(json.dumps({'compiler':subprocess.check_output([compiler,'--version'],text=True),'thorvg':expected,'compile':[cmd for _,cmd in compiled],'link':command},indent=2)+'\n')
print(build/'IconArtProof')

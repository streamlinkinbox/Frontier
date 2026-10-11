#!/usr/bin/env python3
"""Build GLFW/ThorVG required by Project-Zero's direct MSVC toolchain."""
from pathlib import Path
import argparse,shutil,subprocess,sys
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--configuration',choices=['Release','Debug'],default='Release');a=p.parse_args()
if sys.platform!='win32':raise SystemExit('This helper is for MSVC. Linux uses the root CMake application target.')
build=root/'build'/('dependencies-'+a.configuration)
for command in [
 ['cmake','-S',str(root/'Tools/Build/Dependencies'),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE='+a.configuration],
 ['cmake','--build',str(build),'--target','glfw','thorvg_static','--parallel','3']]:subprocess.run(command,cwd=root,check=True)
for name,folder in [('glfw3dll.lib','glfw/lib-vc2022'),('glfw3.dll','glfw/lib-vc2022'),('thorvg.lib','thorvg/lib/'+a.configuration)]:
 matches=list(build.rglob(name))
 if len(matches)!=1:raise SystemExit('Expected one build artifact '+name+', found '+str(matches))
 destination=root/'ExternalPackages'/folder;destination.mkdir(parents=True,exist_ok=True);shutil.copy2(matches[0],destination/name)

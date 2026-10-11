#!/usr/bin/env python3
"""Compile the unchanged GPU shader table as a regression gate, not a GPU cloud-volume proof."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[3];p=argparse.ArgumentParser();p.add_argument('--compiler',default=str(R/'.cache/stars-glslang-build/StandAlone/glslang'));a=p.parse_args();compiler=Path(a.compiler).resolve();assert compiler.is_file()
t=R/'.cache/cpp-sun-full';out=R/'Exhibits/Gallery/CloudsNative';stage=R/'.cache/clouds-shader-output';env=dict(os.environ,PATH=str(compiler.parent)+os.pathsep+os.environ['PATH'],SHADER_OUT=str(stage));command=['bash',str(t/'Tools/Build/CheckShaders.sh')]
result=subprocess.run(command,cwd=t,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'ShaderCheck.txt').write_text(result.stdout+'\nRegression compilation only. The GPU cloud-shadow path is not a full global/local cloud-volume renderer. No GPU execution claim.\n');print(result.stdout);assert result.returncode==0 and 'GREEN — 18/18' in result.stdout
(out/'ShaderCommands.json').write_text(json.dumps({'command':command,'compiler':str(compiler),'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'SHADER_OUT':str(stage),'version':subprocess.check_output([str(compiler),'--version'],text=True)},indent=2)+'\n')
files=[t/'Engine/Shaders/CloudShadow.slang',t/'Engine/Shaders/PostRecords.slang',t/'Engine/Shaders/ReSTIRViewport.slang',R/'Tools/Build/Patches/CloudsInspector.patch',Path(__file__)]
(out/'ShaderSources.json').write_text(json.dumps({str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},indent=2)+'\n')

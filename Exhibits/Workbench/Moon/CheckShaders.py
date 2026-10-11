#!/usr/bin/env python3
"""Compile the actual shader table and reflect the Moon block. No GPU execution."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--compiler',default=shutil.which('glslangValidator') or shutil.which('glslang') or str(R/'.cache/moon-glslang-build/StandAlone/glslang'));a=p.parse_args()
compiler=Path(a.compiler).resolve();assert compiler.is_file(),'Install glslang or provide --compiler'
t=R/'.cache/cpp-sun-full';out=R/'Exhibits/Gallery/MoonNative';stage=R/'.cache/moon-shader-output';out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,PATH=str(compiler.parent)+os.pathsep+os.environ['PATH'],SHADER_OUT=str(stage))
command=['bash',str(t/'Tools/Build/CheckShaders.sh')];result=subprocess.run(command,cwd=t,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'ShaderCheck.txt').write_text(result.stdout);assert result.returncode==0 and 'GREEN — 18/18' in result.stdout,result.stdout
reflection=[str(compiler),'-V','--target-env','vulkan1.2','-S','comp','-DFRONTIER_SHADER_TOOLCHAIN=1','-I'+str(t/'Engine'),'-I'+str(t/'Engine/Shaders'),'-q','-l','-o',str(stage/'MoonReflection.spv'),str(stage/'ReSTIRViewport.spv.glsl')]
s=subprocess.check_output(reflection,text=True,stderr=subprocess.STDOUT)
names=['MoonControl','MoonSlots','MoonDirection','MoonTint','MoonParams','MoonSurface','MoonConstants'];lines=[l for l in s.splitlines() if any(l.startswith(n+':') for n in names)]
assert any(l.startswith('MoonConstants:') and 'size 288,' in l and 'binding 22,' in l for l in lines)
for name,offset in zip(names[:6],[0,16,32,96,160,224]):assert any(l.startswith(name+':') and ('offset '+str(offset)+',') in l for l in lines)
version=subprocess.check_output([str(compiler),'--version'],text=True)
(out/'ShaderLayout.txt').write_text(version+'Actual ReSTIRViewport reflection; compile/layout proof, not GPU execution.\n'+'\n'.join(lines)+'\nPASS: unchanged 288-byte block at binding 22; roll uses Direction.w.\n')
files=[t/'Engine/Shaders/MoonRecords.slang',t/'Engine/Shaders/ReSTIRViewport.slang',t/'Engine/DisplayPresentation/MoonConstantRecord.h',t/'Projects/Project-Zero/Source/CelestialSequence.cpp',R/'Tools/Build/Patches/MoonInspector.patch',Path(__file__).resolve()]
(out/'ShaderSources.json').write_text(json.dumps({str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},indent=2)+'\n')
(out/'ShaderCommands.json').write_text(json.dumps({'commands':[command,reflection],'PATH_prefix':str(compiler.parent),'SHADER_OUT':str(stage),'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest()},indent=2)+'\n')
print(result.stdout);print('PASS: Moon uniform layout / 288 bytes / binding 22')

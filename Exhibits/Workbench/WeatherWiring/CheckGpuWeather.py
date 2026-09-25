#!/usr/bin/env python3
"""Compile production shaders and verify the actual post/weather SPIR-V ABI.
Does not execute Vulkan; RunShaderMirror.py is the separate CPU algorithm test.
"""
import argparse, os, shutil, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--compiler',default=shutil.which('glslang') or shutil.which('glslangValidator'));a=p.parse_args()
assert a.compiler,'Provide --compiler pointing to glslang/glslangValidator'
compiler=Path(a.compiler).resolve();stage=R/'build/weather-shaders';stage.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,PATH=str(compiler.parent)+os.pathsep+os.environ['PATH'],SHADER_OUT=str(stage))
result=subprocess.run(['bash','Tools/Build/CheckShaders.sh'],cwd=R,env=env,text=True,capture_output=True)
(stage/'compile.txt').write_text(result.stdout+result.stderr);print(result.stdout,end='');assert result.returncode==0 and 'GREEN' in result.stdout
reflect=subprocess.check_output([str(compiler),'-V','--target-env','vulkan1.2','-S','comp','-DFRONTIER_SHADER_TOOLCHAIN=1','-IEngine','-IEngine/Shaders','-q','-l','-o',str(stage/'reflection.spv'),str(stage/'ReSTIRViewport.spv.glsl')],cwd=R,text=True)
lines=[l for l in reflect.splitlines() if l.startswith(('Post','Weather:','HistoryPair:','HistorySurfacePair:','MomentPair:'))]
assert any(l.startswith('Weather:') and 'offset 208,' in l and 'size 21,' in l and 'arrayStride 16' in l for l in lines)
assert any(l.startswith('PostConstants:') and 'size 544,' in l and 'binding 24,' in l for l in lines)
assert any(l.startswith('PostStarEffects:') and 'offset 192,' in l for l in lines)
# Current/snapshot arrays must match the host's descriptor counts; binding 31 stays last.
for name,binding in [('HistoryPair',3),('HistorySurfacePair',18),('MomentPair',19)]:
    assert any(l.startswith(name+':') and 'size 2,' in l and f'binding {binding},' in l for l in lines)
(stage/'layout.txt').write_text('\n'.join(lines)+'\n')
# Integration guardrails; execution of these routes still needs a Vulkan runtime.
s=(R/'Engine/Shaders/ReSTIRViewport.slang').read_text();begin=s.index('void ResolveSurface(');end=s.index('\nvoid Resolve(',begin);resolve=s[begin:end]
assert resolve.index('imageStore(HistoryImage')<resolve.index('ApplyWeather(')<resolve.index('imageStore(DenoiseImage')
assert 'surfaceHit' in resolve and 'weatherSurface.xyz-CameraOrigin' in resolve
host=(R/'Projects/Project-Zero/Source/GameExecution.cpp').read_text()
assert 'offsetof(Frontier::PostConstantRecord, Weather)' in host
assert 'FinalDispatch.AccumulationIndex = Integrator.QueryAccumulationIndex()' in host
host=(R/'Engine/DeviceExchange/SwapchainExchange.cpp').read_text()
assert 'kPostRecordBytes = 544u' in host and 'PostPending.data()+492u' in host
assert 'vkCmdUpdateBuffer(Command,Vulkan->PostBuffer' in host and '!Vulkan->PostWeatherActive' in host
print('PASS weather ABI / post-history composition / depth / GI-off routing / queued upload guards')

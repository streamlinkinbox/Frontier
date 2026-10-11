#!/usr/bin/env python3
"""Execute the actual scalar/vector weather shader functions as C++ using GLM.
This is not GPU execution. GLSL source is syntax-adapted, not reimplemented.
Pass --glm pointing to a GLM 1.0.1 checkout (0af55ccecd98d4e5a8d1fad7de25ba429d60e863).
"""
import argparse, re, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--glm',type=Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
b=R/'build/weather-shader-mirror';b.mkdir(parents=True,exist_ok=True)
def adapt(s):
 s=re.sub(r'//[^\n]*','',s)
 s=re.sub(r'^\s*#.*$','',s,flags=re.M)
 s=re.sub(r'\bout float (\w+)',r'float& \1',s)
 s=re.sub(r'\.(xyz|yzw)\b',r'.\1()',s)
 # GLSL unsuffixed floats are float32; C++ would otherwise promote to double.
 s=re.sub(r'(?<![\w.])((?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?|\d+[eE][+-]?\d+)(?![\w.])',r'\1f',s)
 return s
cloud=(R/'Engine/Shaders/CloudShadow.slang').read_text();cloud=cloud[cloud.index('float CloudShadowProfile'):cloud.index('float CloudShadowTransmittance')]
weather=(R/'Engine/Shaders/WeatherMedia.slang').read_text()
header='''#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include "WeatherConstantRecord.h"
#include "FogModel.h"
using namespace glm;
using uint=unsigned;
vec4 Weather[21],SkySunRadiance(1,1,1,1),SkySunDirection(0,0,1,90),SkyRayleigh(0.0000058f,0.0000135f,0.0000331f,8000),SkyMie(0.000021f,1200,0,0);
'''
(b/'WeatherShaderMirror.cpp').write_text(header+adapt(cloud)+adapt(weather)+(R/'Exhibits/Workbench/WeatherWiring/ShaderMirrorTests.inc').read_text())
cmd=['g++','-std=c++20','-O1' if a.sanitize else '-O2','-I'+str(a.glm.resolve()),'-I'+str(R/'Engine/DisplayPresentation'),str(b/'WeatherShaderMirror.cpp'),'-o',str(b/'WeatherShaderMirror')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True)
result=subprocess.run([str(b/'WeatherShaderMirror')],capture_output=True,text=True)
print(result.stdout,end='');print(result.stderr,end='')
(b/('sanitized.txt' if a.sanitize else 'release.txt')).write_text(result.stdout+result.stderr)
raise SystemExit(result.returncode)

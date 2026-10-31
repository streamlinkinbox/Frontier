#============================================================================================================================================
#                                                      PREPAREGLINTCOMPARISON.PY
#============================================================================================================================================
# 🧩 Adapt pinned external research shaders into ignored, isolated WebGL comparison inputs without changing production rendering.

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import OpenEXR
import numpy as np

Parser = argparse.ArgumentParser()
Parser.add_argument('--chermain', type=Path, required=True)
Parser.add_argument('--glinty', type=Path, required=True)
Parser.add_argument('--destination', type=Path, required=True)
Arguments = Parser.parse_args()
Root = Path(__file__).resolve().parents[3]
Destination = Arguments.destination
Destination.mkdir(parents=True, exist_ok=True)
ChermainFile = Arguments.chermain / 'real_time_glint/shader/glint.frag.glsl'
ChermainCommit = subprocess.check_output(['git', '-C', str(Arguments.chermain), 'rev-parse', 'HEAD'], text=True).strip()
if ChermainCommit != '96c08fdff986e9eb83509850770b4946d829bcea':
    raise RuntimeError('Expected pinned Chermain reference revision')
subprocess.run(['git', '-C', str(Arguments.chermain), 'diff', '--exit-code', 'HEAD'], check=True)
GlintyDigest = hashlib.sha256(Arguments.glinty.read_bytes()).hexdigest()
if GlintyDigest != '6750c456cb25ea6524414df696543b740a70eea0151ee7ed0bbec1662de4a5fd':
    raise RuntimeError('Expected pinned external Glinty adaptation; do not silently substitute another shader')
Chermain = ChermainFile.read_text()
# Sampling the centre of an atlas row preserves the original 1D-array distribution lookup.
Chermain = Chermain[Chermain.index('// The MIT License'):Chermain.index('void main()')]
Chermain = Chermain.replace('uniform sampler1DArray DictionaryTex;', 'uniform sampler2D DictionaryTex;')
Chermain = Chermain.replace('textureLod(DictionaryTex, vec2(texCoordX, l_dist * Dictionary.N / 3 + distIdxXOver3), 0)',
                           'textureLod(DictionaryTex, vec2(texCoordX, (l_dist * float(Dictionary.N) / 3.0 + float(distIdxXOver3) + .5) / 1024.0), 0.0)')
Chermain = Chermain.replace('textureLod(DictionaryTex, vec2(texCoordY, l_dist * Dictionary.N / 3 + distIdxYOver3), 0)',
                           'textureLod(DictionaryTex, vec2(texCoordY, (l_dist * float(Dictionary.N) / 3.0 + float(distIdxYOver3) + .5) / 1024.0), 0.0)')
Chermain = Chermain.replace('in vec2 TexCoord;', 'vec2 TexCoord;')
for Caption in ['VertexPos', 'VertexNorm', 'VertexTang']:
    Chermain = Chermain.replace('in vec3 ' + Caption + ';', '')
# Uniform structs become initialized local-global descriptors for the isolated fixed fixture.
Chermain = Chermain.replace(r'\alpha_{dist}', r'\alpha_dist')
Chermain = re.sub(r'uniform struct (\w+)\s*\{([^}]+)\}\s*(\w+);', r'struct \1 {\2}; \1 \3;', Chermain)
Chermain = Chermain.replace('uniform vec3 CameraPosition;', '')
Chermain = Chermain.replace('uniform float MicrofacetRelativeArea;', 'float MicrofacetRelativeArea;')
Chermain = Chermain.replace('uniform float MaxAnisotropy;', 'float MaxAnisotropy;')
Chermain = Chermain.replace('layout(location = 0) out vec4 FragColor;', '')
Chermain = re.sub(r'(\d+(?:\.\d*)?)f\b', r'\1', Chermain)
Chermain = Chermain.replace('uint rngSeed = s0 + 1549 * t0;', 'uint rngSeed = uint(s0 + 1549 * t0);')
Chermain = Chermain.replace('l_dist = clamp(int(round(l_dist)), 0, Dictionary.NLevels);', 'l_dist = float(clamp(int(round(l_dist)), 0, Dictionary.NLevels));')
Chermain = Chermain.replace('l_dist == Dictionary.NLevels', 'l_dist == float(Dictionary.NLevels)')
Chermain = Chermain.replace('float pyrSize = pyramidSize(l);', 'float pyrSize = float(pyramidSize(l));')
Chermain = Chermain.replace('Dictionary.NLevels - 1. +', 'float(Dictionary.NLevels) - 1. +')
Chermain = Chermain.replace('mod(i, 3)', 'mod(float(i), 3.0)').replace('mod(j, 3)', 'mod(float(j), 3.0)')
Chermain = Chermain.replace('P22__P_', 'P22Footprint')
for Before, After in [('4 * A * C', '4.0 * A * C'), ('1 / det', '1.0 / det'), ('float sumWts = 0;', 'float sumWts = 0.0;'),
                      ('r2 < 1)', 'r2 < 1.0)'), ('float alpha = 2;', 'float alpha = 2.0;'), ('minorLength == 0)', 'minorLength == 0.0)')]:
    Chermain = Chermain.replace(Before, After)
Chermain = Chermain.replace('float tt = it - st[1];', 'float tt = float(it) - st[1];').replace('float ss = is - st[0];', 'float ss = float(is) - st[0];')
Glinty = Arguments.glinty.read_text()
Glinty = re.sub(r'^#.*$', '', Glinty, flags=re.MULTILINE)
Glinty = Glinty.replace('namespace Glint\n{', '')
Glinty = Glinty[:Glinty.rfind('} // namespace Glint')]
Glinty = Glinty.replace('[unroll]', '').replace('asuint(', 'floatBitsToUint(')
Glinty = Glinty.replace('mul(invCov, x)', '(invCov * x)').replace('mul(uvJ2, transpose(uvJ2))', '(uvJ2 * transpose(uvJ2))')
for Before, After in [('float2x2', 'mat2'), ('float2', 'vec2'), ('float3', 'vec3'), ('float4', 'vec4'), ('uint2', 'uvec2')]:
    Glinty = re.sub(r'\b' + Before + r'\b', After, Glinty)
Glinty = 'const float K_PI = 3.141592653589793;\n' + Glinty
PaintFile = Root / 'Engine/Render/Shader/AutomotivePaint.slang'
Paint = PaintFile.read_text()
Paint = Paint.replace('return frac((Position.x + Position.y) * Position.z);', 'return fract((Position.x + Position.y) * Position.z);')
Paint = re.sub(r'Point = mul\(float3x3\((.*?)\), Point\);', r'Point = transpose(mat3(\1)) * Point;', Paint)
for Before, After in [('float3x3', 'mat3'), ('float2', 'vec2'), ('float3', 'vec3'), ('float4', 'vec4'), ('uint4', 'uvec4'),
                      ('frac', 'fract'), ('lerp', 'mix'), ('ddx', 'dFdx'), ('ddy', 'dFdy')]:
    Paint = re.sub(r'\b' + Before + r'\b', After, Paint)
Paint = Paint.replace('saturate(', 'clamp(').replace('max(.0001, Paint.Palette[Index].w - Paint.Palette[Index - 1].w)))',
                                                              'max(.0001, Paint.Palette[Index].w - Paint.Palette[Index - 1].w), 0.0, 1.0))')
for Before, After in [('int Z =', 'float Z ='), ('int Y =', 'float Y ='), ('int X =', 'float X ='),
                      ('Nearest = 0;', 'Nearest = vec3(0);'), ('uint Layer = 0;', 'float Layer = 0;'),
                      ('Layer < Paint.Configuration.y', 'Layer < float(Paint.Configuration.y)'),
                      ('Paint.Configuration.z == 2', 'Paint.Configuration.z == 2u'),
                      ('Paint.Configuration.z == 0', 'Paint.Configuration.z == 0u'),
                      ('Paint.Configuration.w - 1', 'Paint.Configuration.w - 1u'),
                      ('Paint.Configuration.y - 1', 'Paint.Configuration.y - 1u'),
                      ('Position * Paint.Configuration.w', 'Position * float(Paint.Configuration.w)'),
                      ('uint Index = 1;', 'uint Index = 1u;'), ('Index - 1', 'Index - 1u')]:
    Paint = Paint.replace(Before, After)
Paint = re.sub(r'(?<![\w.])\d+(?:\.\d*)?(?:e[+-]?\d+)?[uUfF]?(?![\w.])',
               lambda Match: Match[0] + '.0' if Match[0].isdigit() else Match[0], Paint)
Paint = Paint.replace('Palette[12.0]', 'Palette[12]').replace('Palette[0.0]', 'Palette[0]')
Paint = Paint.replace('float(Paint.Configuration.y - 1u - Layer)', '(float(Paint.Configuration.y - 1u) - Layer)')
Prefix = '#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nuniform float Phase;\nuniform float Density;\nuniform float Roughness;\nin vec2 Uv;\nout vec4 OutputColour;\n'
FragmentFixture = '''
void main()
{
    vec3 View = normalize(vec3(.1, .2, 1.0));
    vec3 LightDirection = normalize(vec3((Uv - .5) * .7 + vec2(Phase * .0001), 1.0));
    vec3 HalfDirection = normalize(View + LightDirection);
    float Response = 0.0;
    EVALUATION
    vec3 Radiance = vec3(Response);
    OutputColour = vec4(Radiance / (1.0 + Radiance), 1.0);
}
'''
Body = 'float D = EvalGlintNDF(HalfDirection, Roughness, .001, Uv, mat2(dFdx(Uv), dFdy(Uv)), exp(Density), 1.0);\n'
Body += 'float G = min(1.0, 2.0 * HalfDirection.z * View.z / dot(View, HalfDirection)) * min(1.0, 2.0 * HalfDirection.z * LightDirection.z / dot(LightDirection, HalfDirection));\nResponse = D * G / (4.0 * View.z);'
Sources = {'GlintyAdapter': Prefix + Glinty + FragmentFixture.replace('EVALUATION', Body),
           'RasterFloor': Prefix + FragmentFixture.replace('EVALUATION', 'Response = Uv.x * .01 + .01;')}
Body = '''TexCoord = Uv; Material.Alpha_x = Roughness; Material.Alpha_y = Roughness;
Material.LogMicrofacetDensity = Density; Dictionary.Alpha = .5; Dictionary.N = 192;
Dictionary.NLevels = 16; Dictionary.Pyramid0Size = 32768; MicrofacetRelativeArea = 1.0; MaxAnisotropy = 4.0;
Response = f_P(View, LightDirection).r;'''
Sources['ChermainReference'] = Prefix + Chermain + FragmentFixture.replace('EVALUATION', Body)
Body = '''AutomotivePaint Parameters; Parameters.Configuration = uvec4(1, 3, 2, 1);
Parameters.Distribution = vec4(.64, 450.0, 1.0, .65); Parameters.Finish = vec4(.1, .3, .8, 1.0);
Parameters.Relief = vec4(.35, .1, .02, 20.0); Parameters.Palette[0] = vec4(.8,.5,.2,0.0);
Parameters.AverageTint = Parameters.Palette[0];
PaintSurface Surface = EvaluatePaintSurface(Parameters, vec3(Uv * 2.0 + Phase * .0001, .2), vec3(.2), Roughness, .8);
vec3 Normal = DerivePaintNormal(Parameters, Surface, vec3(0,0,1), vec3(Uv * 2.0, 0), false);
float Alpha = max(.045, Surface.Roughness); float Term = pow(max(0.0, dot(Normal, HalfDirection)), 2.0) * (Alpha * Alpha - 1.0) + 1.0;
Response = Surface.Albedo.r * Alpha * Alpha / (3.14159265 * Term * Term) / (4.0 * View.z);'''
Sources['CellularAdapter'] = Prefix + Paint + FragmentFixture.replace('EVALUATION', Body)
Dictionary = np.empty((1024, 64, 3), dtype=np.float32)
Digest = hashlib.sha256()
for Level in range(16):
    for Distribution in range(64):
        File = Arguments.chermain / 'media/dictionary' / f'dict_16_192_64_0p5_0p02_{Distribution:04}_{Level:04}.exr'
        Digest.update(File.read_bytes())
        Pixels = OpenEXR.File(str(File)).channels()['RGB'].pixels
        assert Pixels.shape == (1, 64, 3), Pixels.shape
        Dictionary[Level * 64 + Distribution] = Pixels[0]
assert np.isfinite(Dictionary).all()
(Destination / 'Dictionary.bin').write_bytes(Dictionary.tobytes())
for Caption, Source in Sources.items():
    (Destination / (Caption + '.frag')).write_text(Source)
Manifest = {'chermain_commit': ChermainCommit, 'chermain_licence': 'MIT',
            'glinty_adapter_commit': '0ca4ab63223f2a836413daab9ab05cf4d29a8591',
            'glinty_adapter_repository': 'Gistix/CreationEngineRaytracing', 'glinty_adapter_licence': 'GPL-3.0',
            'glinty_adapter_source_sha256': hashlib.sha256(Arguments.glinty.read_bytes()).hexdigest(),
            'chermain_source_sha256': hashlib.sha256(ChermainFile.read_bytes()).hexdigest(),
            'cellular_source_sha256': hashlib.sha256(PaintFile.read_bytes()).hexdigest(),
            'dictionary_exr_sequence_sha256': Digest.hexdigest(), 'dictionary_logical_gpu_bytes': 64 * 1024 * 3 * 2,
            'dictionary_upload_bytes': Dictionary.nbytes,
            'adapted_shader_sha256': {Name: hashlib.sha256(Text.encode()).hexdigest() for Name, Text in Sources.items()}}
(Destination / 'Inputs.json').write_text(json.dumps(Manifest, indent=2) + '\n')
print('Prepared three isolated research-adapter shaders, raster floor and original 1024 dictionary rows.')

#============================================================================================================================================
#                                                       ROUGHREFLECTIONIMAGES.PY
#============================================================================================================================================
# 🧩 Retain real Vulkan captures, a labelled sharp/rough comparison, source motion and executed acceptance metadata.

from pathlib import Path
import hashlib
import json
import sys

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw, ImageFont

Output = Root / 'Exhibits/LightCompute'
Capture = Root / '_ClaudeScratch/light-compute'
Names = ['RoughHybrid', 'RoughBvh', 'RoughRepeat', 'RoughNoHiddenSource', 'RoughSourcesOff', 'RoughMoved', 'RoughSharp']
Pictures = {}
for Name in Names:
    Picture = Image.open(Capture / (Name + '.ppm')).convert('RGB')
    assert Picture.size == (192, 120)
    Picture.save(Output / 'Images' / (Name + '.png'))
    Pictures[Name] = Picture
assert Pictures['RoughHybrid'].tobytes() == Pictures['RoughRepeat'].tobytes()
assert not any(Pictures['RoughSourcesOff'].tobytes())
Font = ImageFont.load_default(size=16)
Comparison = Image.new('RGB', (768, 292), (22, 26, 31))
Draw = ImageDraw.Draw(Comparison)
for X, Name, Label in [(0, 'RoughSharp', 'Before: one sharp reflection'), (384, 'RoughHybrid', 'After: sampled roughness lobe')]:
    Comparison.paste(Pictures[Name].resize((384, 240), Image.Resampling.NEAREST), (X, 28))
    Draw.text((X + 9, 6), Label, font=Font, fill='white')
Draw.text((9, 273), 'Floor left to right: roughness 0.045 / 0.20 / 0.45 / 0.80.  Native 192 x 120; enlarged 2x.',
          font=ImageFont.load_default(size=14), fill='white')
Comparison.save(Output / 'Images/RoughReflectionChange.png')
Animation = []
for Name in ['RoughHybrid', 'RoughMoved']:
    Picture = Image.new('RGB', (384, 268), (22, 26, 31))
    Picture.paste(Pictures[Name].resize((384, 240), Image.Resampling.NEAREST), (0, 28))
    ImageDraw.Draw(Picture).text((8, 6), 'Emissive geometry: ' + ('original' if Name == 'RoughHybrid' else 'shifted 0.5 m'), font=Font, fill='white')
    Animation.append(Picture)
Animation[0].save(Output / 'Images/RoughReflectionMotion.gif', save_all=True, append_images=Animation[1:], duration=900, loop=0)
Lines = (Output / 'Results/RoughReflection.txt').read_text().splitlines()
def Read(Line):
    return {K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:])}
Summary = Read(next(L for L in Lines if L.startswith('ROUGH_REFLECTION ')))
Uniform = [Read(L) for L in Lines if L.startswith('ROUGH_UNIFORM ')]
assert len(Uniform) == 4 and all(0 < R['mean_red'] <= R['limit'] for R in Uniform)
assert Summary['screen_rays'] > 0 and Summary['bvh_fallback_rays'] > 0
assert Summary['hybrid_bvh_max_rgb_error'] < .002 and Summary['hybrid_bvh_mean_rgb_error'] < .00001
assert Summary['deterministic_repeat'] == Summary['source_off_black'] == Summary['moving_source_fresh_equal'] == 1
assert Summary['hidden_source_primary_pixels'] == 0 and Summary['hidden_source_red_arrival'] > .01
Files = [Root / P for P in (
    'Frontier/Engine/Rendering/Shaders/MaterialRayTransport.slang', 'Frontier/Engine/Rendering/Shaders/LightInfluence.slang',
    'Exhibits/LightCompute/Code/RoughReflectionValidation.h', 'Exhibits/LightCompute/Code/LightComputeValidation.cpp',
    'Exhibits/LightCompute/Code/RoughReflectionImages.py', 'Exhibits/LightCompute/RunRoughReflections.sh')]
Files += [Output / 'Images' / (N + '.png') for N in Names + ['RoughReflectionChange']]
Files += [Output / 'Results' / N for N in ('RoughReflection.txt', 'RoughReflectionSharp.txt')]
Files += [Output / 'Images/RoughReflectionMotion.gif']
Report = {
    'result': 'passed_scoped_rough_reflection_checks', 'device': Lines[0],
    'platform': 'Linux', 'compiler': 'GCC 12.2.0, C++20 Release, -Wall -Wextra -Werror',
    'shader_compiler': 'pinned slangtorch 1.3.22 / Slang 2026.14.1, SPIR-V 1.3, -O0',
    'software_vulkan_executed': 'type=4 ' in Lines[0], 'physical_gpu_executed': 'type=1 ' in Lines[0] or 'type=2 ' in Lines[0],
    'summary': Summary, 'uniform_illumination': Uniform, 'sharp_shader_revision': '05e4090',
    'extent': [192, 120], 'comparison_enlargement': '2x nearest-neighbour, no denoising or invented detail',
    'animation': 'two actual source-geometry positions, 900 ms display duration; not a frame-rate measurement',
    'transport': 'full shared BRDF, cosine/GGX deterministic mixture; verified screen candidate or BVH per sampled ray',
    'screen_hit': 'actual reflected-ray/facet intersection, clear-segment query including thin glass; fresh hit reshading',
    'not_implemented': ['roughness-mip screen-colour history', 'temporal specular reconstruction', 'multi-bounce specular transport',
                        'solid-volume refraction', 'physical RTX/GTX/Windows/editor viewport verification'],
    'sha256': {str(P.relative_to(Root)): hashlib.sha256(P.read_bytes()).hexdigest() for P in Files},
    'shader_sha256': {str(P.relative_to(Root)): hashlib.sha256(P.read_bytes()).hexdigest() for P in [
        Root / '_ClaudeScratch/build/light-compute/Transport.spv', Root / '_ClaudeScratch/build/SharpReflections/Transport.spv']}}
(Output / 'Results/RoughReflection.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS actual sharp/rough captures, deterministic repeat, source motion/off-screen/off checks and source hashes')

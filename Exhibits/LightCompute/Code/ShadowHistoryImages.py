#============================================================================================================================================
#                                                       SHADOWHISTORYIMAGES.PY
#============================================================================================================================================
# 🧩 Preserve actual pole-shadow irradiance readbacks and independently measured reflection-history captures.

from pathlib import Path
import hashlib
import json
import sys

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw, ImageFont

Output = Root / 'Exhibits/LightCompute'
Capture = Root / '_ClaudeScratch/light-compute'
Font = ImageFont.load_default(size=16)
Pole = Image.new('RGB', (384, 3 * 154), (22, 26, 31))
for I, Label in enumerate(['Directional light', 'Point light', 'Spot light']):
    Picture = Image.open(Capture / ('PoleShadow0' + str(I) + '.ppm')).convert('RGB')
    assert Picture.size == (384, 128)
    Pole.paste(Picture, (0, I * 154 + 26))
    ImageDraw.Draw(Pole).text((8, I * 154 + 5), Label + ': complete pole shadow', font=Font, fill='white')
Pole.save(Output / 'Images/PoleShadowCoverage.png')
Names = ['SpecularHistoryRaw', 'SpecularHistoryFiltered', 'SpecularHistoryReference', 'SpecularHistoryMoved', 'SpecularHistoryCamera']
Pictures = {}
for Name in Names:
    Pictures[Name] = Image.open(Capture / (Name + '.ppm')).convert('RGB')
    assert Pictures[Name].size == (96, 64)
    Pictures[Name].save(Output / 'Images' / (Name + '.png'))
Comparison = Image.new('RGB', (864, 242), (22, 26, 31))
for I, (Name, Label) in enumerate(zip(Names[:3], ['Fresh: 4 samples/proposal', 'History: 8 updates', 'Reference: 8 independent seeds'])):
    Comparison.paste(Pictures[Name].resize((288, 192), Image.Resampling.NEAREST), (I * 288, 27))
    ImageDraw.Draw(Comparison).text((I * 288 + 6, 5), Label, font=ImageFont.load_default(size=14), fill='white')
ImageDraw.Draw(Comparison).text((8, 223), 'Actual 96 x 64 captures enlarged 3x; smooth left strip is deliberately not filtered.', font=Font, fill='white')
Comparison.save(Output / 'Images/SpecularHistoryComparison.png')

def Rows(Name, Prefix):
    Lines = (Output / 'Results' / Name).read_text().splitlines()
    return [{K: float(V) for K, V in (P.split('=', 1) for P in L.split()[1:])} for L in Lines if L.startswith(Prefix + ' ')]

PoleSummary = Rows('PoleShadow.txt', 'POLE_SUMMARY')[0]
Current = Rows('SpecularHistory.txt', 'SPECULAR_HISTORY')[0]
Original = Rows('SpecularHistoryOriginal.txt', 'SPECULAR_HISTORY')[0]
Native = Rows('SpecularHistory.txt', 'SPECULAR_FILTER')[0]
Camera = Rows('SpecularHistory.txt', 'SPECULAR_CAMERA')[0]
assert PoleSummary['cases'] == 72 and PoleSummary['mismatches'] == 0 and PoleSummary['far_half_probes'] > 0
assert Current['filtered_rmse'] < Current['raw_rmse'] and Current['moved_fresh_rmse'] == Current['moved_history_pixels'] == 0
assert Original['moved_fresh_rmse'] > .0001 and Original['moved_history_pixels'] > 0
assert Native['native_probes'] > 20 and Native['max_rgb_error'] < .0005
assert Camera['accepted'] > 0 and Camera['rejected'] > 0 and Camera['smooth_exact_pixels'] > 0
Reset = Rows('SpecularHistory.txt', 'SPECULAR_HISTORY_RESET')[0]
assert Reset['source_off_black'] == Reset['compact_policy_retained'] == 1
Files = [Root / P for P in (
    'Frontier/Engine/Rendering/Shaders/LightInfluence.slang', 'Exhibits/LightCompute/Code/LightComputeValidation.cpp',
    'Exhibits/LightCompute/Code/PoleShadowValidation.h', 'Exhibits/LightCompute/Code/SpecularHistoryValidation.h',
    'Exhibits/LightCompute/Code/ShadowHistoryImages.py', 'Exhibits/LightCompute/RunShadowHistory.sh')]
Files += [Output / 'Results' / N for N in ('PoleShadow.txt', 'SpecularHistory.txt', 'SpecularHistoryOriginal.txt', 'ShadowHistoryNative.txt')]
Files += [Output / 'Images' / (N + '.png') for N in Names + ['PoleShadowCoverage', 'SpecularHistoryComparison']]
Device = (Output / 'Results/SpecularHistory.txt').read_text().splitlines()[0]
Report = {
    'result': 'passed_scoped_checks', 'platform': 'Linux', 'compiler': 'GCC 12.2.0 C++20 Release -Wall -Wextra -Werror',
    'shader_compiler': 'Slang 2026.14.1, SPIR-V 1.3, -O0',
    'device': Device,
    'physical_gpu_executed': 'type=1' in Device or 'type=2' in Device, 'software_vulkan_executed': 'type=4' in Device,
    'pole': PoleSummary, 'user_shadow_cutoff_reproduced': False,
    'pole_capture': 'actual Vulkan lighting on an uploaded top-down receiver grid; not a primary-camera pole beauty render',
    'pole_scope': 'directional/point/spot; three receiver viewing directions; two caster routes; exact/cluster/fresh-RIS/cold-start history selection, reconstruction disabled; analytic closed-box silhouette',
    'history_current': Current, 'history_geometry_revision_disabled': Original, 'native_filter': Native, 'camera_move': Camera,
    'rmse_reduction_fraction': 1 - Current['filtered_rmse'] / Current['raw_rmse'],
    'reference': 'finite eight-seed, sixteen-samples-per-proposal reference; not converged ground truth',
    'negative_control': 'same current angular filter, but geometry revision disabled to reproduce inherited cache invalidation defect',
    'geometry_invalidation': 'conservative full packed-facet comparison for authored/mixed scenes; compact-only diffuse moving-history policy retained; no speed or memory-saving claim',
    'compact_diffuse_policy_guard_passed': True,
    'limitations': ['reported old-project shadow cutoff remains unlocated', 'no roughness-mip screen-colour history',
                    'no complete temporal/disocclusion/specular-denoising acceptance', 'no physical RTX/GTX/Windows/editor execution',
                    'no fresh full-application sanitizer run'],
    'sha256': {str(P.relative_to(Root)): hashlib.sha256(P.read_bytes()).hexdigest() for P in Files}}
(Output / 'Results/ShadowHistory.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS complete analytic pole shadows, native angular-filter probes, geometry negative control, camera motion and actual captures')

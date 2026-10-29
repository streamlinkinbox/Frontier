#============================================================================================================================================
#                                                       MATERIALENERGYIMAGES.PY
#============================================================================================================================================
# 🧩 Retain unaltered before/after Vulkan captures and parse the executed native/shader acceptance reports.

from pathlib import Path
import hashlib
import io
import json
import subprocess
import sys

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw, ImageFont

Images = Root / 'Exhibits/LightCompute/Images'
Results = Root / 'Exhibits/TextureStudio/Results'
Before = subprocess.check_output(['git', 'show', 'b6bc49e:Exhibits/LightCompute/Images/MaterialSpecimens.png'], cwd=Root)
(Images / 'MaterialSpecimensBeforeEnergy.png').write_bytes(Before)
BeforeScene = json.loads(subprocess.check_output(['git', 'show', 'b6bc49e:Exhibits/LightCompute/Results/MaterialScene.json'], cwd=Root))
AfterScene = json.loads((Root / 'Exhibits/LightCompute/Results/MaterialScene.json').read_text())
assert BeforeScene['inputs'] == AfterScene['inputs'] and BeforeScene['extent'] == AfterScene['extent']
for Name in ('MaterialSceneValidation.h', 'PrepareMaterialScene.py'):
    Pathname = 'Exhibits/LightCompute/Code/' + Name
    assert subprocess.check_output(['git', 'show', 'b6bc49e:' + Pathname], cwd=Root) == (Root / Pathname).read_bytes()
After = (Images / 'MaterialSpecimens.png').read_bytes()
assert Before != After, 'Expected the authored sheen appearance to change'
Comparison = Image.new('RGB', (1280, 432), (22, 26, 31))
Comparison.paste(Image.open(io.BytesIO(Before)), (0, 32))
Comparison.paste(Image.open(io.BytesIO(After)), (640, 32))
Draw = ImageDraw.Draw(Comparison)
Font = ImageFont.load_default(size=18)
Draw.text((12, 7), 'Before: additive sheen (b6bc49e)', font=Font, fill='white')
Draw.text((652, 7), 'After: bounded sheen coverage', font=Font, fill='white')
Comparison.save(Images / 'MaterialEnergyChange.png')

def Summary(Name, Prefix):
    Lines = (Results / Name).read_text().splitlines()
    Line = next(L for L in Lines if L.startswith(Prefix + ' '))
    return {K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:])}

Native = Summary('MaterialEnergyAcceptance.txt', 'MATERIAL_ENERGY_SUMMARY')
Current = Summary('MaterialEnergyGpuCurrent.txt', 'ENERGY_GPU_SUMMARY')
Original = Summary('MaterialEnergyGpuOriginal.txt', 'ENERGY_GPU_SUMMARY')
Sanitizer = Summary('MaterialEnergySanitizer.txt', 'MATERIAL_ENERGY_SUMMARY')
assert Sanitizer == Native
assert Native['cases'] == 384 and Native['excess_cases'] == Native['unsettled_cases'] == 0
assert Current['cases'] == 34 and Current['excess_cases'] == 0 and Current['native_max_rgb_error'] < .001
assert Original['cases'] == 34 and Original['original24_excess'] == 3
assert Native['maximum_reflectance'] <= 1.02 and Current['maximum_reflectance'] <= 1.02
Device = (Results / 'MaterialEnergyGpuCurrent.txt').read_text().splitlines()[0]
Files = [Root / P for P in (
    'Engine/Render/ScatteringSequence.h', 'Engine/Render/Shader/ScatteringEvaluation.slang',
    'Engine/Render/Shader/MaterialSurface.slang', 'Exhibits/TextureStudio/Code/MaterialEnergyValidation.cpp',
    'Exhibits/TextureStudio/Code/MaterialEnergyBaseline.cpp', 'Exhibits/LightCompute/Code/MaterialEnergy.slang',
    'Exhibits/LightCompute/Code/MaterialEnergyGpu.h', 'Exhibits/LightCompute/RunMaterialEnergy.sh',
    'Exhibits/LightCompute/Code/MaterialEnergyImages.py')]
Files += [Results / N for N in ('MaterialEnergyAcceptance.txt', 'MaterialEnergyCurrent.txt',
                               'MaterialEnergyGpuCurrent.txt', 'MaterialEnergyGpuOriginal.txt', 'MaterialEnergySanitizer.txt')]
Files += [Images / N for N in ('MaterialSpecimensBeforeEnergy.png', 'MaterialSpecimens.png', 'MaterialEnergyChange.png')]
Shaders = [Root / '_ClaudeScratch/build' / D / 'Receivers.spv'
           for D in ('MaterialEnergyCurrentGpu', 'MaterialEnergyOriginalGpu')]
Report = {
    'result': 'passed_scoped_energy_acceptance', 'baseline_revision': 'b6bc49e',
    'device': Device, 'software_vulkan_executed': 'type=4 ' in Device,
    'physical_gpu_executed': 'type=1 ' in Device or 'type=2 ' in Device,
    'scope': 'retained isotropic Schlick/default-specular path; not entire material library or renderer',
    'native': Native, 'vulkan_current': Current, 'vulkan_original_negative_control': Original,
    'negative_control_native_reference_checked': False,
    'standalone_native_energy_asan_ubsan_cases': 384, 'full_vulkan_application_sanitized': False,
    'energy_acceptance_limit': 1.02, 'quadrature_delta_limit': .01, 'reciprocity_absolute_tolerance': 1e-10,
    'diffuse': 'paired entrance/exit Schlick transmission at light/view normal angles',
    'sheen': 'convex per-channel coverage mixture with analytically bounded Charlie/Ashikhmin lobe',
    'output_clamp_added': False, 'runtime_lut_added': False, 'abi_changed': False,
    'archived_materials_modified': False, 'full_library_acceptance': False,
    'before_after': 'unmodified 640x400 PNG pixels; only labels and juxtaposition added',
    'not_covered': ['anisotropy', 'exact Fresnel/specular-strength combinations', 'all 399 archived assets',
                    'analytic environment approximation', 'legacy compact BRDF', 'solid-volume refraction',
                    'physical RTX/GTX/Windows/editor viewport', 'full sampler/PDF and temporal stability'],
    'sha256': {str(P.relative_to(Root)): hashlib.sha256(P.read_bytes()).hexdigest() for P in Files},
    'scratch_shader_sha256': {str(P.relative_to(Root)): hashlib.sha256(P.read_bytes()).hexdigest() for P in Shaders},
}
(Results / 'MaterialEnergy.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS energy reports, original negative control, before/after capture comparison and source hashes')

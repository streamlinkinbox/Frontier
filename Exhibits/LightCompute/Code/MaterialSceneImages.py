#============================================================================================================================================
#                                                         MATERIALSCENEIMAGES.PY
#============================================================================================================================================
# 🧩 Retain actual Vulkan material-scene readbacks with reproducible source, reflection and native-evaluation checks.

from Images import Root, Output, Png
import hashlib
import json
import re
import sys
import zlib

Log = (Output / 'Results/MaterialScene.txt').read_text()
Rows = []
for Line in Log.splitlines():
    if Line.startswith('MATERIAL_SCENE '):
        Row = dict(P.split('=', 1) for P in Line.split()[1:])
        Rows.append({K: V if K == 'name' else float(V) for K, V in Row.items()})
assert [R['name'] for R in Rows] == ['MaterialVehicle', 'MaterialVehicleRepeat', 'MaterialVehicleDetail', 'MaterialSpecimens']
assert all(R['native_brdf_probes'] == 128 and R['native_brdf_scaled_error'] < .005 for R in Rows)
assert Rows[0]['ssr_hits'] > 0 and Rows[0]['ssr_bvh_fallback'] > 0 and Rows[0]['side_mirror_hits'] > 0
assert Rows[0]['native_mirror_probes'] == 64 and Rows[2]['native_mirror_probes'] == 64
Pixels = {}
for Name in [R['name'] for R in Rows]:
    Raw = (Root / '_ClaudeScratch/light-compute' / (Name + '.ppm')).read_bytes()
    Header = b'P6\n640 400\n255\n'
    assert Raw.startswith(Header) and len(Raw) == len(Header) + 640 * 400 * 3
    Pixels[Name] = Raw[len(Header):]
assert Pixels['MaterialVehicle'] == Pixels['MaterialVehicleRepeat']
for Name in ['MaterialVehicle', 'MaterialVehicleDetail', 'MaterialSpecimens']:
    Encoded = Png(640, 400, Pixels[Name])
    At, Compressed = 8, b''
    while At < len(Encoded):
        Size = int.from_bytes(Encoded[At:At + 4], 'big')
        Tag, Content = Encoded[At + 4:At + 8], Encoded[At + 8:At + 8 + Size]
        assert zlib.crc32(Tag + Content) == int.from_bytes(Encoded[At + 8 + Size:At + 12 + Size], 'big')
        if Tag == b'IDAT': Compressed += Content
        At += 12 + Size
    Decoded = zlib.decompress(Compressed)
    assert b''.join(Decoded[Y * 1921 + 1:(Y + 1) * 1921] for Y in range(400)) == Pixels[Name]
    (Output / 'Images' / (Name + '.png')).write_bytes(Encoded)
Sources = ['Exhibits/LightCompute/CMakeLists.txt', 'Exhibits/LightCompute/RunMaterialScene.sh', 'Exhibits/LightCompute/Code/MaterialSceneValidation.h', 'Exhibits/LightCompute/Code/PrepareMaterialScene.py',
           'Exhibits/LightCompute/Code/MaterialSceneImages.py', 'Exhibits/LightCompute/Code/LightComputeValidation.cpp',
           'Frontier/Engine/Rendering/Shaders/LightInfluence.slang',
           'Frontier/Engine/Rendering/Shaders/MaterialRayScattering.slang',
           'Frontier/Engine/Rendering/Shaders/MaterialRayTransport.slang',
           'Engine/Render/Shader/AutomotivePaint.slang', 'Engine/Render/Shader/ScatteringEvaluation.slang',
           'Engine/Render/ScatteringSequence.h', 'Engine/Render/MaterialPreviewGeometry.h',
           'Engine/Render/DrapedClothPositions.h']
Inputs = json.loads((Root / '_ClaudeScratch/build/light-compute/MaterialSceneInputs.json').read_text())
Report = {'result': 'passed_scoped_checks', 'device': Log.splitlines()[0],
          'software_vulkan_executed': bool(re.search(r'type=4\b', Log)), 'physical_gpu_executed': bool(re.search(r'type=[12]\b', Log)),
          'native_cpp_slang_executed': True, 'windows_executed': sys.platform == 'win32', 'editor_viewport_executed': False,
          'extent': [640, 400], 'full_material_library_acceptance': False,
          'geometry': 'original 25068-facet ControlVehicle plus generated cones and added reflective/transmitting panes; reused native shaderball and draped cloth',
          'glass': 'parallel thin sheets, Schlick Fresnel, tinted Beer attenuation and transmitting direct-light shadows; at most eight panes, no solid-volume Snell refraction',
          'mirror': 'delta reflection through the existing triangle BVH, no screen-colour substitution; one shaded opaque secondary bounce',
          'plastic': 'full shared BRDF with cosine/GGX sampled roughness; reflected-ray facet intersection and clear-segment validation for screen proposals, BVH fallback; no roughness-mip colour history or temporal specular reconstruction',
          'layering': 'retained shared scattering equations, cosine/GGX deterministic-mixture sampling for ordinary authored materials; one surface normal shared by strata in this bridge',
          'filtering': 'bounded explicit camera-ray footprint, first-stratum albedo/normal mip lookup, bilinear within one selected mip; not full authoring-map/anisotropic/trilinear parity',
          'atmosphere': 'native atmosphere cube retained; cloud/fog disabled in these material-isolation captures, existing cloud/fog presets untouched',
          'inputs': Inputs, 'checks': Rows,
          'readback_rgb_sha256': {N: hashlib.sha256(P).hexdigest() for N, P in Pixels.items()},
          'source_sha256': {N: hashlib.sha256((Root / N).read_bytes()).hexdigest() for N in Sources},
          'spirv_sha256': {P.name: hashlib.sha256(P.read_bytes()).hexdigest() for P in sorted((Root / '_ClaudeScratch/build/light-compute').glob('*.spv'))}}
(Output / 'Results/MaterialScene.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS three lossless material renders, deterministic repeat, native scattering and mirror BVH checks')

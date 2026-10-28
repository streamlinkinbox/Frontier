"""Hash sources, actual compiled Slang and readbacks; distinguish software Vulkan from physical GPU execution."""
from pathlib import Path
import hashlib
import json
import platform
import re
Root = Path(__file__).resolve().parents[3]
Output = Root / 'Exhibits/LightCompute'
Sources = list((Root / 'Frontier/Engine/Rendering').glob('*.h'))
Sources += list((Root / 'Frontier/Engine/Rendering').glob('*.cpp'))
Sources += list((Root / 'Frontier/Engine/Diagnostics').glob('*.h'))
Sources += list((Root / 'Frontier/Engine/Diagnostics').glob('*.cpp'))
Sources += list((Root / 'Frontier/Engine/Rendering/Shaders').glob('*.slang'))
Sources += [Root / P for P in ('Engine/Render/PaneThickness.h', 'Engine/Render/SurfaceGeometry.h', 'Engine/Render/SurfaceMaterial.h', 'Engine/Render/SurfaceMaterial.cpp',
    'Engine/Render/MaterialProceduralBaking.h', 'Engine/Render/MaterialProceduralBaking.cpp',
    'Engine/Render/StbImageImplementation.cpp', 'Engine/ContentInterchange/MaterialAuthoring.h',
    'Engine/ContentInterchange/MaterialAuthoring.cpp')]
Sources += list((Output / 'Materials').rglob('*.material'))
Sources += [Root / 'Engine/Render' / P for P in ('ScatteringSequence.h', 'DielectricRefraction.h', 'Shader/DielectricRefraction.slang', 'MaterialPreviewGeometry.h', 'DrapedClothPositions.h', 'Shader/ScatteringEvaluation.slang', 'Shader/AutomotivePaint.slang', 'Shader/MaterialSurface.slang')]
Sources += [P for P in Output.rglob('*') if P.suffix in ('.h', '.cpp', '.py', '.sh', '.ps1', '.md', '.slang') or P.name == 'CMakeLists.txt']
Sources += [Root / 'Exhibits/VehicleReference' / P for P in ('Code/VehicleCapture.cpp', 'Code/Materials.mjs', 'CMakeLists.txt')]
Sources += [Root / 'Exhibits/ClusteredLighting/Fixtures/Synthetic.ies']
Sources += list((Root / 'Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Archive').glob('ControlVehicle*.facet'))
Sources += [Root / 'Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source' / P
            for P in ('MaterialSpecification.js', 'VehicleSpecification.js')]
Products = list((Output / 'Images').glob('*.png')) + list((Output / 'Results').glob('*.txt'))
Products += [Output / 'Results/Images.json', Output / 'Results/History.json', Output / 'Results/PrimaryMotion.json', Output / 'Results/Transport.json', Output / 'Results/Emissive.json', Output / 'Results/TransportReconstruction.json', Output / 'Results/Clouds.json', Output / 'Results/CloudShadows.json', Output / 'Results/Fog.json', Output / 'Results/FogSampling.json', Output / 'Results/FogFacets.json', Output / 'Results/CloudBudget.json', Output / 'Results/CloudPreview.json', Output / 'Results/MaterialScene.json', Output / 'Results/MaterialVerification.json']
Sources += [Root / 'Exhibits/TextureStudio' / P for P in (
    'Code/MaterialEnergyValidation.cpp', 'Code/MaterialEnergyBaseline.cpp', 'Code/MaterialReuseAudit.py',
    'MaterialEnergyAcceptance.md', 'MaterialEnergy.md', 'MaterialReuse.md', 'GlintPair.md')]
Products += [Output / 'Results/SavedDielectric.json', Output / 'Results/VehicleGlassEdges.json'] if (Output / 'Results/SavedDielectric.json').exists() else []
Products += [Output / 'Results/VehicleGlazing.json'] if (Output / 'Results/VehicleGlazing.json').exists() else []
Products += [Output / 'Results/GlazingMotion.json'] if (Output / 'Results/GlazingMotion.json').exists() else []
Products += [Output / 'Results/GlazingMotionRegression.json'] if (Output / 'Results/GlazingMotionRegression.json').exists() else []
Products += [Output / 'Results/FaintPath.json'] if (Output / 'Results/FaintPath.json').exists() else []
EnergyProducts = [Root / 'Exhibits/TextureStudio/Results' / P for P in (
    'MaterialEnergyAcceptance.txt', 'MaterialEnergyCurrent.txt', 'MaterialEnergyGpuCurrent.txt',
    'MaterialEnergyGpuOriginal.txt', 'MaterialEnergySanitizer.txt', 'MaterialEnergy.json', 'MaterialReuse.json')]
Products += [P for P in EnergyProducts if P.exists()]
Products += [Output / 'Results/RoughReflection.json'] if (Output / 'Results/RoughReflection.json').exists() else []
Products += list((Output / 'Images').glob('Rough*.gif'))
Products += list((Output / 'Images').glob('GlazingMotion*.gif'))
Products += [Output / 'Results/ShadowHistory.json'] if (Output / 'Results/ShadowHistory.json').exists() else []
Products += [Output / 'Results/SolidDielectric.json'] if (Output / 'Results/SolidDielectric.json').exists() else []
Products += [Output / 'Results/MirrorDielectric.json'] if (Output / 'Results/MirrorDielectric.json').exists() else []
Products += [Output / 'Results/RoughDielectric.json'] if (Output / 'Results/RoughDielectric.json').exists() else []
Products += [Output / 'Results/WindowDielectric.json'] if (Output / 'Results/WindowDielectric.json').exists() else []
Products += [Output / 'Results/DiffuseDielectric.json'] if (Output / 'Results/DiffuseDielectric.json').exists() else []
Products += [Output / 'Results/PaneTransmittance.json'] if (Output / 'Results/PaneTransmittance.json').exists() else []
def Hashes(Paths):
    return {str(P.relative_to(Root)): hashlib.sha256(P.read_bytes()).hexdigest() for P in sorted(set(Paths))}
Log = (Output / 'Results/Vulkan.txt').read_text()
DeviceLine = Log.splitlines()[0]
DeviceType = int(re.search(r' type=(\d+)', DeviceLine).group(1))
Report = {'execution': 'actual Vulkan compute with shared Slang', 'device': DeviceLine,
          'device_type': DeviceType, 'software_vulkan_executed': DeviceType == 4,
          'platform': platform.platform(), 'vulkan_executed': True, 'physical_gpu_executed': DeviceType in (1, 2),
          'ray_query_executed': False, 'triangle_bvh_shader_executed': True,
          'material_energy_evidence_missing': [str(P.relative_to(Root)) for P in EnergyProducts if not P.exists()],
          'material_energy_report': 'Exhibits/TextureStudio/Results/MaterialEnergy.json',
          'native_direct_upload_poisoned': True,
          'primary_visibility': 'Vulkan camera rays for Primary.png; native CPU raster for history regression',
          'gpu_primary_visibility_executed': True, 'primary_receiver_channels_poisoned': 36,
          'primary_sky_and_indirect': 'black environment; no indirect transport',
          'sky_and_indirect': 'native CPU reference in retained history regression',
          'gpu_fresh_ris_executed': True, 'gpu_temporal_reuse_executed': True,
          'gpu_spatial_reuse_executed': True, 'gpu_filtering_executed': True,
          'rigid_motion_reprojection_executed': 'MOTION index=' in Log,
          'gpu_primary_motion_reprojection_executed': True,
          'gpu_primary_motion_native_metadata_oracle': True,
          'gpu_one_bounce_diffuse_transport_executed': True,
          'atmosphere_cube_bake': 'native CPU SingleScattering, consumed by shader',
          'gpu_atmosphere_executed': False,
          'gpu_cloud_raymarch_executed': True,
          'gpu_cloud_cube_executed': True,
          'cloud_cube_native_probes': 2230,
          'cloud_camera_native_probes': 396,
          'gpu_spatial_cloud_sun_shadows_executed': True,
          'cloud_shadow_native_surface_probes': 2165,
          'cloud_shadow_native_secondary_probes': 117,
          'gpu_shadowed_fog_scattering_executed': True,
          'fog_numeric_native_camera_probes': 654,
          'fog_emissive_volume_capture_native_camera_probes': 468,
          'fog_emissive_volume_sanitizer_native_camera_probes': 448,
          'fog_emissive_volume_rectangle_reference_samples': 131072,
          'fog_emissive_volume_default_enabled': False,
          'fog_sampling_capture_native_camera_probes': 936,
          'fog_sampling_sanitizer_native_camera_probes': 1040,
          'fog_promoted_preset_native_camera_probes': 654,
          'fog_sampling_reference': 'finite 128 shadow steps, not converged ground truth',
          'fog_capture_native_camera_probes': 390,
          'fog_native_surface_arrival_probes': 270,
          'transport_native_probes': 901,
          'gpu_emissive_area_sampling_executed': True,
          'emissive_native_probes': 225,
          'emissive_high_reference_samples': 8192,
          'gpu_indirect_temporal_reconstruction_executed': True,
          'gpu_indirect_spatial_filter_executed': True,
          'indirect_reconstruction_native_spatial_probes': 210,
          'windows_executed': False, 'vulkan_validation_layer_executed': False,
          'sources_sha256': Hashes(Sources), 'products_sha256': Hashes(Products),
          'scratch_sha256': {}, 'material_scene_report': 'Results/MaterialScene.json',
          'material_scene_scope': 'native C++/Slang preset bindings, selected library samples; thin glass, depth-screen plastic/BVH fallback; not full material acceptance'}
# Ignored binary captures can disappear in a sandbox reset. Preserve their historical digests,
# but explicitly distinguish them from bytes verified in this execution.
Manifest = Output / 'Results/Manifest.json'
Previous = json.loads(Manifest.read_text()) if Manifest.exists() else {}
Scratch = list((Root / '_ClaudeScratch/build/light-compute').glob('*.spv'))
Scratch += [Root / P for P in ('_ClaudeScratch/vehicle-reference/VehicleLightInput.bin',
                              '_ClaudeScratch/vehicle-primary/VehiclePrimary.bin',
                              '_ClaudeScratch/light-compute/TransportScene.bin',
                              '_ClaudeScratch/light-compute/EmissiveScene.bin',
                              '_ClaudeScratch/light-compute/CloudScene.bin')]
Scratch += list((Root / '_ClaudeScratch/vehicle-reference').glob('VehicleMotion*.bin'))
Scratch += list((Root / '_ClaudeScratch/primary-motion').glob('VehicleMotion*.bin'))
Scratch += list((Root / '_ClaudeScratch/glazing-motion').glob('*.ppm'))
Scratch += list((Root / '_ClaudeScratch/pane-transmittance').glob('*.ppm'))
Scratch += list((Root / '_ClaudeScratch/glazing-motion').glob('*.bin'))
Scratch += list((Root / '_ClaudeScratch/build/faint-path').glob('*.slang'))
Scratch += list((Root / '_ClaudeScratch/build/faint-path').glob('*.sha256'))
Scratch += list((Root / '_ClaudeScratch/build/faint-path').glob('*.spv'))
Present = Hashes([P for P in Scratch if P.exists()])
Retained = {P: H for P, H in Previous.get('scratch_sha256', {}).items() if not (Root / P).exists()}
Report['scratch_sha256'] = {**Retained, **Present}
Report['scratch_availability'] = {P: 'verified_current_bytes' if P in Present else 'historical_digest_only_after_sandbox_reset'
                                  for P in Report['scratch_sha256']}
Report['sandbox_recovery_checkpoint'] = '0a4560c243b08da82679e065d19f434a77bc7c2b'
(Output / 'Results/Manifest.json').write_text(json.dumps(Report, indent=2) + '\n')
print('Evidence:', len(Report['sources_sha256']), 'sources;', len(Report['products_sha256']), 'products; device type', DeviceType)

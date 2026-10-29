"""Record the actual CPU proof and hash its sources/readbacks, without claiming a GPU run."""
from pathlib import Path
import hashlib
import json
import platform
import subprocess
Root = Path(__file__).resolve().parents[3]
Output = Root / 'Exhibits/ClusteredLighting'
Sources = list((Root / 'Frontier/Engine/Rendering').glob('*.h'))
Sources += list((Root / 'Frontier/Engine/Rendering').glob('*.cpp'))
Sources += list((Root / 'Frontier/Engine/Diagnostics').glob('*.h'))
Sources += list((Root / 'Frontier/Engine/Diagnostics').glob('*.cpp'))
Sources += [P for P in Output.rglob('*') if P.suffix in ('.cpp', '.py', '.sh', '.md', '.ies')]
Sources += [Root / 'Exhibits/VehicleReference' / P for P in (
    'CMakeLists.txt', 'Code/VehicleCapture.cpp', 'Code/Materials.mjs',
    'Code/TriangleDistanceValidation.cpp', 'Code/TriangleVisibilityValidation.cpp')]
Sources += [Root / 'Exhibits/RendererPlanning/Code/CompareReferences.mjs']
Archive = Root / 'Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds'
Sources += list((Archive / 'Archive').glob('ControlVehicle*.facet'))
Sources += [Archive / 'Source/MaterialSpecification.js', Archive / 'Source/VehicleSpecification.js']
Sources += [Root / 'Exhibits/LightCompute/Code/LightComputeFormat.h']
Products = list(Output.glob('*Results.txt')) + list((Output / 'Capture').glob('*.png'))
Products += [Output / 'ImageResults.json']
def Hashes(Paths):
    return {str(P.relative_to(Root)): hashlib.sha256(P.read_bytes()).hexdigest() for P in sorted(set(Paths))}
Cpu = platform.processor()
if Path('/proc/cpuinfo').exists():
    Cpu = next(Line.split(':', 1)[1].strip() for Line in Path('/proc/cpuinfo').read_text().splitlines()
               if Line.startswith('model name'))
Report = {'rendering_path': 'native C++ CPU reference', 'cpu': Cpu, 'platform': platform.platform(),
          'compiler': subprocess.check_output(['c++', '--version'], text=True).splitlines()[0],
          'physical_gpu_verified': False, 'vulkan_executed': False, 'windows_executed': False,
          'motion_history_verified': False, 'measured_ies_fixture': False,
          'sources_sha256': Hashes(Sources), 'products_sha256': Hashes(Products)}
(Output / 'Manifest.json').write_text(json.dumps(Report, indent=2) + '\n')
print(f'Evidence: {len(Report["sources_sha256"])} sources, {len(Report["products_sha256"])} products; native CPU only')

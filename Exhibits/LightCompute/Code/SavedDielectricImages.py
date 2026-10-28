#============================================================================================================================================
#                                                     SAVEDDIELECTRICIMAGES.PY
#============================================================================================================================================
# 🧩 Present native Vulkan readbacks without fabricated imagery; retain exact capture hashes and device logs.
from pathlib import Path
import hashlib
import json
import sys

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw

Capture = Root / '_ClaudeScratch/saved-dielectric'
Images = Root / 'Exhibits/LightCompute/Images'
Results = Root / 'Exhibits/LightCompute/Results'
Images.mkdir(parents=True, exist_ok=True)
Canvas = Image.new('RGB', (1152, 330), '#151b23')
Draw = ImageDraw.Draw(Canvas)
Titles = ['Index 1.0 / no absorption', 'Index 1.5 / clear closed volume', 'Index 1.5 / distance absorption']
Hashes = {}
for I, Title in enumerate(Titles):
    Source = Capture / f'SavedDielectric{I}.ppm'
    Picture = Image.open(Source).convert('RGB')
    Picture.save(Images / f'SavedDielectric{I}.png')
    Canvas.paste(Picture.resize((384,256), Image.Resampling.NEAREST), (I*384,40))
    Draw.text((I*384+12,14), Title, fill='#eef2f8')
    Hashes[Source.name] = hashlib.sha256(Source.read_bytes()).hexdigest()
Draw.text((12,310), 'Native saved/reloaded material coefficients; 96 x 64 Vulkan, 128 paths; SwiftShader CPU. Enlarged nearest-neighbour.', fill='#b9c8db')
Canvas.save(Images / 'SavedDielectric.png')
Saved = Root / 'Exhibits/LightCompute/Materials/RoundTrips'
Saved.mkdir(parents=True, exist_ok=True)
for Material in Capture.glob('ClosedGlass*.material'):
    (Saved / Material.name).write_bytes(Material.read_bytes())
Report = {'device': 'SwiftShader Device (Subzero)', 'physical_gpu': False, 'capture_dimensions': [96,64],
          'paths_per_pixel': 128, 'interaction_cap': 2048, 'capture_sha256': Hashes,
          'authoring_log': (Results/'SavedDielectricAuthoring.txt').read_text(),
          'sanitizer_log': (Results/'SavedDielectricSanitized.txt').read_text(),
          'native_regression_log': (Results/'SavedDielectricNative.txt').read_text(),
          'previous_log': (Results/'SavedDielectricPrevious.txt').read_text(),
          'gpu_log': (Results/'SavedDielectricGpu.txt').read_text(),
          'vehicle_geometry': json.loads((Results/'VehicleGlassEdges.json').read_text()),
          'saved_material_sha256': {P.name: hashlib.sha256(P.read_bytes()).hexdigest()
                                    for P in Capture.glob('ClosedGlass*.material')},
          'scope': 'Native TOML codec and material compilation into existing exhibit volume atlas. No glass studio preview, editor controls or vehicle volume assignment.'}
(Results/'SavedDielectric.json').write_text(json.dumps(Report,indent=2)+'\n')
print('SAVED_DIELECTRIC_IMAGES captures=3 source_dimensions=96x64 software_vulkan=1')

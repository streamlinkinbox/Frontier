#============================================================================================================================================
#                                                     ROUGHDIELECTRICIMAGES.PY
#============================================================================================================================================
# 🧩 Present native Vulkan readbacks without fabricated imagery; retain exact capture hashes and device logs.
from pathlib import Path
import hashlib
import json
import sys

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw

Capture = Root / '_ClaudeScratch/light-compute'
Images = Root / 'Exhibits/LightCompute/Images'
Results = Root / 'Exhibits/LightCompute/Results'
Images.mkdir(parents=True, exist_ok=True)
Canvas = Image.new('RGB', (1152, 330), '#151b23')
Draw = ImageDraw.Draw(Canvas)
Titles = ['Index 1.0 / no absorption', 'Index 1.5 / clear closed volume', 'Index 1.5 / distance absorption']
Hashes = {}
for I, Title in enumerate(Titles):
    Source = Capture / f'RoughDielectric{I}.ppm'
    Picture = Image.open(Source).convert('RGB')
    Picture.save(Images / f'RoughDielectric{I}.png')
    Canvas.paste(Picture.resize((384,256), Image.Resampling.NEAREST), (I*384,40))
    Draw.text((I*384+12,14), Title, fill='#eef2f8')
    Hashes[Source.name] = hashlib.sha256(Source.read_bytes()).hexdigest()
Draw.text((12,310), 'Actual 96 x 64 Vulkan captures, 32 samples x 2 proposals; roughness .06 / .30 / .65; SwiftShader CPU. Enlarged nearest-neighbour.', fill='#b9c8db')
Canvas.save(Images / 'RoughDielectric.png')
Report = {'negative_control_revision': '213f050', 'device': 'SwiftShader Device (Subzero)', 'physical_gpu': False, 'capture_dimensions': [96,64],
          'samples_per_pixel': 32, 'proposals_per_sample': 2, 'reflector_roughness': [.06,.3,.65], 'interaction_cap': 2048, 'capture_sha256': Hashes,
          'rough_log': (Results/'RoughDielectric.txt').read_text(),
          'negative_control_log': (Results/'RoughDielectricOriginal.txt').read_text(),
          'scope': 'Rough cosine/GGX reflector rays through smooth closed volumes. Rough glass boundaries, nesting, caustics, saved material authoring and compact diffuse extension separately tested.'}
(Results/'RoughDielectric.json').write_text(json.dumps(Report,indent=2)+'\n')
print('ROUGH_DIELECTRIC_IMAGES captures=3 source_dimensions=96x64 software_vulkan=1')

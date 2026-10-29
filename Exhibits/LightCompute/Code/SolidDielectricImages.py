#============================================================================================================================================
#                                                     SOLIDDIELECTRICIMAGES.PY
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
    Source = Capture / f'SolidDielectric{I}.ppm'
    Picture = Image.open(Source).convert('RGB')
    Picture.save(Images / f'SolidDielectric{I}.png')
    Canvas.paste(Picture.resize((384,256), Image.Resampling.NEAREST), (I*384,40))
    Draw.text((I*384+12,14), Title, fill='#eef2f8')
    Hashes[Source.name] = hashlib.sha256(Source.read_bytes()).hexdigest()
Draw.text((12,310), 'Actual 96 x 64 Vulkan captures, 128 paths/pixel; SwiftShader CPU. Enlarged nearest-neighbour.', fill='#b9c8db')
Canvas.save(Images / 'SolidDielectric.png')
Report = {'device': 'SwiftShader Device (Subzero)', 'physical_gpu': False, 'capture_dimensions': [96,64],
          'paths_per_pixel': 128, 'interaction_cap': 2048, 'capture_sha256': Hashes,
          'boundary_log': (Results/'DielectricBoundary.txt').read_text(),
          'volume_log': (Results/'SolidDielectric.txt').read_text(),
          'scope': 'Primary smooth closed-volume acceptance; mirror and rough-reflector secondary extensions separately tested. Thin-sheet extension separately tested; compact diffuse extension separately tested; nesting, rough glass boundaries, caustics and saved material authoring remain unsupported.'}
(Results/'SolidDielectric.json').write_text(json.dumps(Report,indent=2)+'\n')
print('SOLID_IMAGES captures=3 source_dimensions=96x64 software_vulkan=1')

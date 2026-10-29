#============================================================================================================================================
#                                                     DIFFUSEDIELECTRICIMAGES.PY
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
    Source = Capture / f'DiffuseDielectric{I}.ppm'
    Picture = Image.open(Source).convert('RGB')
    Picture.save(Images / f'DiffuseDielectric{I}.png')
    Canvas.paste(Picture.resize((384,256), Image.Resampling.NEAREST), (I*384,40))
    Draw.text((I*384+12,14), Title, fill='#eef2f8')
    Hashes[Source.name] = hashlib.sha256(Source.read_bytes()).hexdigest()
Draw.text((12,310), 'Actual 96 x 64 Vulkan captures, 64 diffuse samples; albedo .30 / .60 / .90; SwiftShader CPU. Enlarged nearest-neighbour.', fill='#b9c8db')
Canvas.save(Images / 'DiffuseDielectric.png')
Vehicle = Capture / 'Transport.ppm'
VehicleDimensions = None
if Vehicle.exists():
    Picture = Image.open(Vehicle).convert('RGB')
    VehicleDimensions = list(Picture.size)
    Picture.save(Images / 'DiffuseVehicleTransport.png')
    Hashes[Vehicle.name] = hashlib.sha256(Vehicle.read_bytes()).hexdigest()
Report = {'negative_control_revision': '02d5d13', 'device': 'SwiftShader Device (Subzero)', 'physical_gpu': False, 'capture_dimensions': [96,64], 'retained_vehicle_dimensions': VehicleDimensions,
          'samples_per_pixel': 64, 'proposals_per_sample': 1, 'receiver_albedo': [.3,.6,.9], 'interaction_cap': 2048, 'capture_sha256': Hashes,
          'diffuse_log': (Results/'DiffuseDielectric.txt').read_text(),
          'negative_control_log': (Results/'DiffuseDielectricOriginal.txt').read_text(),
          'retained_vehicle_log': (Results/'DiffuseDielectricTransportRegression.txt').read_text(),
          'compact_motion_log': (Results/'DiffuseDielectricHistoryRegression.txt').read_text(),
          'scope': 'Compact cosine diffuse receivers through smooth closed volumes. Rough glass boundaries, nesting, caustics and saved material authoring remain unsupported.'}
(Results/'DiffuseDielectric.json').write_text(json.dumps(Report,indent=2)+'\n')
print('DIFFUSE_DIELECTRIC_IMAGES captures=3 source_dimensions=96x64 software_vulkan=1')

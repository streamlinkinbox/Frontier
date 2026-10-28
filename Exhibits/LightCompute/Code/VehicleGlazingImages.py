#============================================================================================================================================
#                                                       VEHICLEGLAZINGIMAGES.PY
#============================================================================================================================================
# 🧩 Retain genuine Vulkan vehicle readbacks and native round-trip material documents with source hashes.
from pathlib import Path
import hashlib
import json
import sys
Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw
Capture = Root / '_ClaudeScratch/vehicle-glazing'
Images = Root / 'Exhibits/LightCompute/Images'
Results = Root / 'Exhibits/LightCompute/Results'
Names = ['VehicleGlazingSheet', 'VehicleGlazingClear', 'VehicleGlazingAbsorbed']
Titles = ['Retained thin sheets', 'Closed 6 mm / IOR 1.52', 'Closed 6 mm / saved absorption']
Canvas = Image.new('RGB', (960, 432), '#151b23')
Draw = ImageDraw.Draw(Canvas)
Hashes = {}
for I, (Name, Title) in enumerate(zip(Names, Titles)):
    Source = Capture / (Name + '.ppm')
    Picture = Image.open(Source).convert('RGB')
    assert Picture.size == (320, 200)
    Picture.save(Images / (Name + '.png'))
    Canvas.paste(Picture, (320*I, 30))
    Draw.text((320*I+8, 10), Title, fill='#eef2f8')
    Draw.text((320*I+8, 244), 'Window crop / same pixels / nearest enlargement', fill='#b9c8db')
    Canvas.paste(Picture.crop((110,75,220,125)).resize((320,146), Image.Resampling.NEAREST), (320*I,266))
    Hashes[Source.name] = hashlib.sha256(Source.read_bytes()).hexdigest()
Draw.text((8, 416), 'Actual Vulkan 320 x 200 readbacks; SwiftShader CPU; 16 transport samples. No image synthesis.', fill='#b9c8db')
Canvas.save(Images / 'VehicleGlazing.png')
Moved = Capture / 'VehicleGlazingMoved.ppm'
Image.open(Moved).save(Images / 'VehicleGlazingMoved.png')
Hashes[Moved.name] = hashlib.sha256(Moved.read_bytes()).hexdigest()
Saved = Root / 'Exhibits/LightCompute/Materials/VehicleGlazingRoundTrips'
Saved.mkdir(parents=True, exist_ok=True)
for Material in Capture.glob('ClosedGlass*.material'):
    (Saved / Material.name).write_bytes(Material.read_bytes())
Report = {'device': 'SwiftShader Device (Subzero)', 'physical_gpu': False, 'capture_dimensions': [320, 200],
          'transport_samples': 16, 'interaction_cap': 2048, 'tir_diagnostic_saturation': 65535,
          'source_triangles': 74, 'panes': 3, 'closed_triangles': 284, 'thickness_m': .006,
          'capture_sha256': Hashes,
          'source_archive_sha256': hashlib.sha256((Root / '_ClaudeScratch/primary-motion/VehicleMotion0.bin').read_bytes()).hexdigest(),
          'saved_material_sha256': {P.name: hashlib.sha256(P.read_bytes()).hexdigest() for P in Saved.glob('*.material')},
          'logs': {Name: (Results / (Name + '.txt')).read_text() for Name in
                   ['PaneThickness', 'PaneThicknessSanitized', 'VehicleGlazingAuthoring', 'VehicleGlazingNative', 'VehicleGlazingGpu']},
          'scope': 'Derived closed retained-vehicle glazing in native compute. No editor importer, physical GPU or general CAD acceptance.'}
(Results / 'VehicleGlazing.json').write_text(json.dumps(Report, indent=2) + '\n')
print('VEHICLE_GLAZING_IMAGES actual_captures=4 source_dimensions=320x200 software_vulkan=1')

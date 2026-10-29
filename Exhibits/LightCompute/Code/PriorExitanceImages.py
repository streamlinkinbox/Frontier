#============================================================================================================================================
#                                                    PRIOREXITANCEIMAGES.PY
#============================================================================================================================================
# 🧩 Publish actual Vulkan readbacks for prior-screen exitance, rigid authored motion and unified six-pose vehicle captures.

from pathlib import Path
import hashlib
import json
import sys

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw

Source = Root / '_ClaudeScratch/unified-exitance'
Images = Root / 'Exhibits/LightCompute/Images'
Results = Root / 'Exhibits/LightCompute/Results'

Gpu = (Results / 'PriorExitance.txt').read_text()
Unified = (Results / 'UnifiedScene.txt').read_text()
assert 'PRIOR_EXITANCE_GPU ' in Gpu, 'Refusing to publish prior-exitance images without its GPU pass line'
assert 'UNIFIED_SCENE_COMPLETE ' in Unified, 'Refusing to publish unified images without its completion line'

def Load(Name):
    return Image.open(Source / Name).convert('RGB')

Hashes = {}
for Name in ['PriorExitancePoint.ppm', 'PriorExitancePyramid.ppm', 'PriorExitanceMoving.ppm', 'PriorExitanceRefresh.ppm',
             'UnifiedHeroPose0.ppm', 'UnifiedHeroPose3.ppm', 'UnifiedDistanceDiagnostic.ppm'] + [f'UnifiedPose{i}.ppm' for i in range(6)]:
    Hashes[Name] = hashlib.sha256((Source / Name).read_bytes()).hexdigest()

# Hero: 320x200 unified captures for warm stationary pose 0 and weather/light-change reset pose 3.
Hero = Image.new('RGB', (656, 500), '#131922')
for Row, (Name, Title) in enumerate([('UnifiedHeroPose0.ppm', 'Pose 0 (wind 0 s, warm reuse)'),
                                     ('UnifiedHeroPose3.ppm', 'Pose 3 (wind 90 s, weather + light change, reset)')]):
    Draw = ImageDraw.Draw(Hero)
    Y = Row * 254
    Draw.text((8, Y + 4), Title, fill='#eef2f8')
    Hero.paste(Load(Name), (8, Y + 20))
Hero.save(Images / 'UnifiedSceneHero.png')

# Six-pose sequence at 2x nearest-neighbour, plus an animated GIF of the same frames.
Poses = [Load(f'UnifiedPose{I}.ppm') for I in range(6)]
W, H = 384, 240
Labels = ['0 cold/warm', '1 moving', '2 moving', '3 weather reset', '4 moving TIR', '5 moving']
Sequence = Image.new('RGB', (3 * (W + 8) + 8, 2 * (H + 30) + 8), '#131922')
Draw = ImageDraw.Draw(Sequence)
for I, Picture in enumerate(Poses):
    Col, Row = I % 3, I // 3
    X, Y = 8 + Col * (W + 8), 8 + Row * (H + 30)
    Draw.text((X, Y), f'Pose {Labels[I]}', fill='#eef2f8')
    Sequence.paste(Picture.resize((W, H), Image.Resampling.NEAREST), (X, Y + 16))
Sequence.save(Images / 'UnifiedSceneSequence.png')
Frames = [P.resize((W, H), Image.Resampling.NEAREST) for P in Poses]
Frames[0].save(Images / 'UnifiedSceneMotion.gif', save_all=True, append_images=Frames[1:], duration=450, loop=0)

Load('UnifiedDistanceDiagnostic.ppm').resize((384, 240), Image.Resampling.NEAREST).save(Images / 'UnifiedDistanceDiagnostic.png')

# Prior-screen exitance comparison, 3x nearest-neighbour 160x100 readbacks.
Panels = [('PriorExitancePoint.ppm', 'Point-hit SSR (fresh shading), stationary'),
          ('PriorExitancePyramid.ppm', 'ExitancePyramid cone, stationary'),
          ('PriorExitanceMoving.ppm', 'ExitancePyramid, rigid prism moved'),
          ('PriorExitanceRefresh.ppm', 'Lighting change: guarded refresh')]
S = 3
Comparison = Image.new('RGB', (2 * (160 * S + 8) + 8, 2 * (100 * S + 30) + 8), '#131922')
Draw = ImageDraw.Draw(Comparison)
for I, (Name, Title) in enumerate(Panels):
    Col, Row = I % 2, I // 2
    X, Y = 8 + Col * (160 * S + 8), 8 + Row * (100 * S + 30)
    Draw.text((X, Y), Title, fill='#eef2f8')
    Comparison.paste(Load(Name).resize((160 * S, 100 * S), Image.Resampling.NEAREST), (X, Y + 16))
Comparison.save(Images / 'PriorExitanceComparison.png')

(Results / 'PriorExitanceImages.json').write_text(json.dumps({'source_sha256': Hashes}, indent=2) + '\n')
print('PRIOR_EXITANCE_IMAGES written')

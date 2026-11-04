#============================================================================================================================================
#                                                          ASSEMBLEEVIDENCE.PY
#============================================================================================================================================
# 🧩 Labelled actual Vulkan frames and a time-sequence GIF. No painted fog, generated vectors or replacement screenshots.

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import argparse
import hashlib
import json


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('results', type=Path)
    Arguments = Parser.parse_args()
    Root = Arguments.results.resolve()
    Font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 17)
    Small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
    Cases = [('Uniform fog', 'Showcase-Fog-Uniform'), ('Heterogeneous fog', 'Showcase-Fog-Noisy'),
             ('Heterogeneous + self-shadow', 'Showcase-Fog-SelfShadow'), ('Box / falloff', 'Fog-Shape-1'),
             ('Ellipsoid / falloff', 'Fog-Shape-0'), ('Sphere / falloff', 'Fog-Shape-2'),
             ('Hemisphere / falloff', 'Fog-Shape-3'), ('Local cloud / field east', 'Cloud-Field-East'),
             ('Local cloud / field west', 'Cloud-Field-West')]
    CellWidth, CellHeight = 384, 282
    Sheet = Image.new('RGB', (CellWidth * 3, CellHeight * 3 + 65), '#131719')
    Pen = ImageDraw.Draw(Sheet)
    Pen.text((16, 12), 'NATIVE VULKAN / LOCAL MEDIA', font=Font, fill='#e3e9df')
    Pen.text((16, 38), 'Production shaders on SwiftShader. Actual 256 × 160 readbacks enlarged 1.5×; not performance evidence.',
             font=Small, fill='#a1aba5')
    for Index, (Caption, Filename) in enumerate(Cases):
        Along, Across = Index % 3 * CellWidth, Index // 3 * CellHeight + 65
        Pen.text((Along + 12, Across + 9), Caption, font=Font, fill='#d8e3d6')
        Capture = Image.open(Root / 'After' / (Filename + '.png')).convert('RGB')
        Sheet.paste(Capture.resize((384, 240), Image.Resampling.NEAREST), (Along, Across + 42))
    Sheet.save(Root / 'LocalMediaOverview.png')
    Frames = []
    for Index in range(4):
        Frame = Image.new('RGB', (512, 368), '#131719')
        Pen = ImageDraw.Draw(Frame)
        Pen.text((12, 8), f'Native fog / wind 2 m/s / scene time {Index * 2} s', font=Font, fill='#e3e9df')
        Pen.text((12, 30), 'Time-compressed comparison, not a frame-rate claim.', font=Small, fill='#a1aba5')
        Capture = Image.open(Root / 'After' / f'Showcase-Fog-Wind-{Index}.png').convert('RGB')
        Frame.paste(Capture.resize((512, 320), Image.Resampling.NEAREST), (0, 48))
        Frames.append(Frame)
    Frames[0].save(Root / 'FogWind.gif', save_all=True, append_images=Frames[1:], duration=650, loop=0)
    Manifest = {}
    for Filename in sorted(Root.rglob('*')):
        if Filename.suffix.lower() in ['.png', '.gif']:
            with Image.open(Filename) as Capture:
                Manifest[Filename.relative_to(Root).as_posix()] = {'width': Capture.width, 'height': Capture.height,
                    'sha256': hashlib.sha256(Filename.read_bytes()).hexdigest()}
    (Root / 'CaptureManifest.json').write_text(json.dumps(Manifest, indent=2) + '\n')


if __name__ == '__main__':
    Main()

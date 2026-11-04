#============================================================================================================================================
#                                                          ASSEMBLEEVIDENCE.PY
#============================================================================================================================================
# 🧩 Labelled native planet inspectors and GPU lunar comparisons; only actual rendered captures are assembled.

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import argparse
import hashlib
import json


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('results', type=Path)
    Arguments = Parser.parse_args()
    Root = Arguments.results
    Font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 17)
    Small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 12)
    Sheet = Image.new('RGB', (1680, 1480), '#101416')
    Pen = ImageDraw.Draw(Sheet)
    Pen.text((18, 14), 'NATIVE C++ / FOUR PLANET INSTRUMENTS', font=Font, fill='#e2e8e1')
    Pen.text((18, 40), 'Actual standalone inspector captures; each body has its own interactive instrument geometry.',
             font=Small, fill='#9eaaa7')
    for Index, Body in enumerate(['Venus', 'Jupiter', 'Mars', 'Saturn']):
        Source = Image.open(Root / 'Inspectors' / (Body + '-0.png')).convert('RGB')
        Sheet.paste(Source.crop((0, 0, 420, min(1400, Source.height))), (Index * 420, 80))
        Pen.text((Index * 420 + 16, 64), Body, font=Small, fill='#c1cfc6')
    Sheet.save(Root / 'PlanetInstruments.png')
    Comparison = Image.new('RGB', (960, 565), '#101416')
    Pen = ImageDraw.Draw(Comparison)
    Cases = [('Full phase', 'Moon-Full'), ('New phase', 'Moon-New'), ('Smaller diameter', 'Moon-Small'),
             ('Surface rotation', 'Moon-Rotated'), ('Glow', 'Moon-Glow'), ('All four slots', 'Four-Moons')]
    for Index, (Caption, Name) in enumerate(Cases):
        Along, Across = Index % 3 * 320, Index // 3 * 282
        Pen.text((Along + 12, Across + 10), Caption, font=Font, fill='#dce5df')
        Comparison.paste(Image.open(Root / 'Gpu' / (Name + '.png')).convert('RGB'), (Along, Across + 38))
    Comparison.save(Root / 'MoonParameterChanges.png')
    Manifest = {}
    for File in sorted(Root.rglob('*.png')):
        with Image.open(File) as Capture:
            Manifest[File.relative_to(Root).as_posix()] = {'width': Capture.width, 'height': Capture.height,
                'sha256': hashlib.sha256(File.read_bytes()).hexdigest()}
    (Root / 'CaptureManifest.json').write_text(json.dumps(Manifest, indent=2) + '\n')


if __name__ == '__main__':
    Main()

#============================================================================================================================================
#                                                          ASSEMBLEEVIDENCE.PY
#============================================================================================================================================
# 🧩 Labelled excerpts of actual C++ captures and source/native comparisons; no simulated UI or generated artwork.

from pathlib import Path
import argparse
import hashlib
import json
import shutil
from PIL import Image, ImageDraw, ImageFont


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('results', type=Path)
    Parser.add_argument('--workspace', type=Path)
    Arguments = Parser.parse_args()
    Root = Arguments.results.resolve()
    Font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 16)
    SmallFont = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 12)
    Entries = [('Clouds', 'Cloud-0'), ('Wind', 'Wind-0'), ('Height fog', 'Fog-0'), ('Atmospheric fog', 'AerialFog-0'),
               ('Stars', 'Stars-0'), ('Moons', 'Moon-0'), ('Lens flare', 'Flare-0'), ('Lightning', 'Lightning-0')]
    Width, Height = 420, 900
    Sheet = Image.new('RGB', (Width * 4, (Height + 38) * 2 + 55), '#0c0e10')
    Ink = ImageDraw.Draw(Sheet)
    Ink.text((18, 14), 'FRONTIER / NATIVE C++ ENVIRONMENT INSPECTORS', fill='#e8ebdf', font=Font)
    Ink.text((18, 36), 'Actual ImGui commands, CPU-rasterized on Linux. Top-of-page excerpts; full PNGs are retained.',
             fill='#9da5a2', font=SmallFont)
    for Index, (Title, Filename) in enumerate(Entries):
        Along = (Index % 4) * Width
        Across = 55 + (Index // 4) * (Height + 38)
        Ink.text((Along + 16, Across + 10), Title, fill='#d8e0d6', font=Font)
        Capture = Image.open(Root / 'Native' / (Filename + '.png')).convert('RGB')
        Sheet.paste(Capture.crop((0, 0, min(Width, Capture.width), min(Height, Capture.height))), (Along, Across + 38))
    Sheet.save(Root / 'NativeOverview.png')
    for Native, Source in [('Cloud-0', 'clouds-0'), ('Wind-0', 'wind-0'), ('Moon-0', 'moon-0'),
                           ('Flare-0', 'flare-0'), ('AerialFog-0', 'aerial-0'), ('LocalCloud-0', 'local-cloud-0')]:
        First = Image.open(Root / 'Reference' / (Source + '.png')).convert('RGB')
        Second = Image.open(Root / 'Native' / (Native + '.png')).convert('RGB')
        Comparison = Image.new('RGB', (First.width + Second.width, max(First.height, Second.height) + 56), '#101010')
        Caption = ImageDraw.Draw(Comparison)
        Caption.text((14, 10), 'PINNED VEHICLEHMI DEMO', fill='#e5e5df', font=Font)
        Caption.text((First.width + 14, 10), 'NATIVE C++', fill='#e5e5df', font=Font)
        Caption.text((14, 34), 'Defaults differ; renderer-unavailable notices are intentional.', fill='#9da5a2', font=SmallFont)
        Comparison.paste(First, (0, 56))
        Comparison.paste(Second, (First.width, 56))
        Comparison.save(Root / ('Compare-' + Native + '.png'))
    if Arguments.workspace:
        (Root / 'Workspace').mkdir(parents=True, exist_ok=True)
        for Category in ['Clouds', 'Stars', 'Moons', 'AerialFog', 'OpticalFlare']:
            Filename = '08-InspectorPage-' + Category + '-1.png'
            shutil.copy2(Arguments.workspace / Filename, Root / 'Workspace' / Filename)
    Files = {}
    for Filename in sorted(Root.rglob('*.png')):
        with Image.open(Filename) as Capture:
            Files[Filename.relative_to(Root).as_posix()] = {
                'width': Capture.width, 'height': Capture.height,
                'sha256': hashlib.sha256(Filename.read_bytes()).hexdigest()}
    (Root / 'CaptureManifest.json').write_text(json.dumps(Files, indent=2) + '\n')


if __name__ == '__main__':
    Main()

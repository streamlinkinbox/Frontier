# 🧩 Show actual first-batch readbacks separately from aggregate unclamped error measurements.
from pathlib import Path
import sys
from PIL import Image, ImageDraw

Capture, Destination = map(Path, sys.argv[1:])
Destination.mkdir(parents=True, exist_ok=True)
Canvas = Image.new('RGB', (880, 690), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((16, 12), 'Actual Vulkan first batch / exposure 3, clipped / nearest-neighbour / not aggregate quality', fill='white')
for Column, Label in enumerate(('Whole Fresh8 MIS off', 'Whole Fresh8 balance', 'Whole Fresh8 power beta=2')):
    Pen.text((16 + Column * 285, 40), Label, fill='white')
for Condition, Label in enumerate(('Plane 32 x 32', 'Room 24 x 24')):
    Top = 90 + Condition * 290
    Pen.text((16, Top - 22), Label, fill='white')
    for Column, Suffix in enumerate(('Off', 'Balance', 'Power')):
        with Image.open(Capture / f'Power{Condition}{Suffix}.ppm') as Source:
            Source = Source.convert('RGB')
            Source.save(Destination / f'MISPower{Condition}{Suffix}.png')
            Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Column * 285, Top))
Canvas.save(Destination / 'ReplayMISPowerComparison.png')

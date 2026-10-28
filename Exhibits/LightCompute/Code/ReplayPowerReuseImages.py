# 🧩 Present fixed-exposure actual power-MIS first-batch readbacks, not a reconstruction of the measured averages.
from pathlib import Path
import sys
from PIL import Image, ImageDraw

Capture, Destination = map(Path, sys.argv[1:])
Destination.mkdir(parents=True, exist_ok=True)
Canvas = Image.new('RGB', (1160, 690), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((16, 12), 'Actual Vulkan first batch / exposure 3, clipped / nearest-neighbour enlargement / not aggregate quality', fill='white')
for Column, Label in enumerate(('Whole MIS off', 'Whole power MIS', 'Fused fresh power MIS', 'Fused reuse power MIS')):
    Pen.text((16 + Column * 285, 40), Label, fill='white')
for Condition, Label in enumerate(('Plane 32 x 32', 'Room 24 x 24')):
    Top = 90 + Condition * 290
    Pen.text((16, Top - 22), Label, fill='white')
    for Column, Suffix in enumerate(('NoMIS', 'Whole', 'Fresh', 'Reuse')):
        with Image.open(Capture / f'Split{Condition}{Suffix}.ppm') as Source:
            Source = Source.convert('RGB')
            Source.save(Destination / f'PowerReuse{Condition}{Suffix}.png')
            Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Column * 285, Top))
Canvas.save(Destination / 'ReplayPowerReuseComparison.png')

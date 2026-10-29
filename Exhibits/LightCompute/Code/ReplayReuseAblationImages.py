# 🧩 Present fixed ablation readbacks and paired MSE contrasts without treating the first batch as aggregate quality.
from pathlib import Path
import re
import sys
from PIL import Image, ImageDraw, ImageFont

Results, Capture, Destination = map(Path, sys.argv[1:])
Text = Results.read_text()
if 'ABLATION_DECISION ' not in Text:
    raise RuntimeError('Completed ablation required')
Destination.mkdir(parents=True, exist_ok=True)
Font = ImageFont.load_default(size=15)
Small = ImageFont.load_default(size=13)
Canvas = Image.new('RGB', (1160, 690), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((16, 12), 'Actual Vulkan first-batch readbacks / exposure 3, clipped / nearest-neighbour enlargement / not aggregate quality', font=Small, fill='white')
for Column, Label in enumerate(('Whole power Fresh8', 'Fused fresh D8/I8', 'Temporal-only reuse', 'Temporal + spatial reuse')):
    Pen.text((16 + Column * 285, 40), Label, font=Font, fill='white')
for Condition, Label in enumerate(('Camera plane 32 x 32', 'Static room 24 x 24')):
    Top = 90 + Condition * 290
    Pen.text((16, Top - 22), Label, font=Font, fill='white')
    for Column, Suffix in enumerate(('Whole', 'Fresh', 'Temporal', 'Spatial')):
        with Image.open(Capture / f'Ablation{Condition}{Suffix}.ppm') as Source:
            Source = Source.convert('RGB')
            Source.save(Destination / f'ReuseAblation{Condition}{Suffix}.png')
            Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Column * 285, Top))
Canvas.save(Destination / 'ReplayReuseAblationComparison.png')

Rows = [dict(re.findall(r'(\w+)=([^ ]+)', Line)) for Line in Text.splitlines() if Line.startswith('ABLATION_PAIRED ')]
if len(Rows) != 6:
    raise RuntimeError('Expected six paired contrasts')
Canvas = Image.new('RGB', (1160, 550), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((20, 16), 'Paired MSE differences +/- nominal SE / negative is lower error / 32 batches / independent CPU power reference', font=Small, fill='white')
for Condition in (0, 1):
    Top = 90 + Condition * 240
    Local = [Fields for Fields in Rows if int(Fields['condition']) == Condition]
    Magnitude = max(abs(float(F['mse_difference'])) + 3 * float(F['nominal_se']) for F in Local)
    Left, Right = 340, 1000
    def X(Number):
        return (Left + Right) / 2 + Number / Magnitude * (Right - Left) / 2
    Pen.text((20, Top - 27), 'Plane' if Condition == 0 else 'Room', font=Font, fill='white')
    Pen.line((X(0), Top - 3, X(0), Top + 145), fill='#8390a3')
    for Index, Fields in enumerate(Local):
        Mean, SE = float(Fields['mse_difference']), float(Fields['nominal_se'])
        Y = Top + Index * 48
        Label = Fields['pair'].replace('_minus_', ' - ')
        Pen.text((20, Y - 8), Label, font=Small, fill='white')
        Pen.line((X(Mean - SE), Y, X(Mean + SE), Y), fill='#77d3a6', width=4)
        Pen.ellipse((X(Mean) - 4, Y - 4, X(Mean) + 4, Y + 4), fill='#f19578')
        Pen.text((1015, Y - 8), f"{Mean:+.3g}", font=Small, fill='white')
    Pen.text((340, Top + 157), f'Horizontal range +/- {Magnitude:.4g}; each scene has its own scale', font=Small, fill='#c5d1df')
Canvas.save(Destination / 'ReplayReuseAblationMse.png')

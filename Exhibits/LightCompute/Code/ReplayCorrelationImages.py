# 🧩 Present fixed five-method actual readbacks and measured error/query tradeoffs.
from pathlib import Path
import re
import sys
from PIL import Image, ImageDraw, ImageFont

Results, Capture, Destination = map(Path, sys.argv[1:])
Text = Results.read_text()
if 'CORRELATION_DECISION ' not in Text:
    raise RuntimeError('Completed correlation control required')
Destination.mkdir(parents=True, exist_ok=True)
Font = ImageFont.load_default(size=15)
Small = ImageFont.load_default(size=13)
Labels = ('Whole power Fresh8', 'Coupled fresh D8/I8', 'Independent fresh D8/I8', 'Whole power Fresh6', 'Temporal-only reuse')
Canvas = Image.new('RGB', (1430, 690), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((16, 12), 'Actual first-batch Vulkan readbacks / exposure 3, clipped / nearest-neighbour / not aggregate quality', font=Small, fill='white')
for Column, Label in enumerate(Labels):
    Pen.text((16 + Column * 285, 40), Label, font=Small, fill='white')
for Condition, Label in enumerate(('Camera plane 32 x 32', 'Static room 24 x 24')):
    Top = 90 + Condition * 290
    Pen.text((16, Top - 22), Label, font=Font, fill='white')
    for Method in range(5):
        with Image.open(Capture / f'Correlation{Condition}{Method}.ppm') as Source:
            Source = Source.convert('RGB')
            Source.save(Destination / f'Correlation{Condition}{Method}.png')
            Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Method * 285, Top))
Canvas.save(Destination / 'ReplayCorrelationComparison.png')

Rows = [dict(re.findall(r'(\w+)=([^ ]+)', Line)) for Line in Text.splitlines() if Line.startswith('CORRELATION_RESULT ')]
if len(Rows) != 10:
    raise RuntimeError('Expected ten method results')
Canvas = Image.new('RGB', (1200, 530), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((20, 14), 'Measured final query cost versus RMSE / relative to Whole8 / 32 batches / warm-up excluded and reported separately', font=Small, fill='white')
Colors = ('#9abfff', '#c8a0ed', '#e3bd79', '#77d3a6', '#f19578')
for Panel, Condition in enumerate((0, 1)):
    Local = [F for F in Rows if int(F['condition']) == Condition]
    Whole = next(F for F in Local if F['method'] == 'whole8')
    Left, Right, Top, Bottom = 95 + Panel * 600, 510 + Panel * 600, 100, 390
    Ratios = [(float(F['queries']) / float(Whole['queries']), float(F['rmse']) / float(Whole['rmse'])) for F in Local]
    XMin, XMax = min(X for X, Y in Ratios) * .94, max(X for X, Y in Ratios) * 1.08
    YMin, YMax = min(Y for X, Y in Ratios) * .94, max(Y for X, Y in Ratios) * 1.08
    Pen.text((Left, 58), 'Plane' if Condition == 0 else 'Room', font=Font, fill='white')
    for Tick in range(5):
        Q = XMin + (XMax - XMin) * Tick / 4
        X = Left + Tick / 4 * (Right - Left)
        Pen.line((X, Top, X, Bottom), fill='#35404d')
        Pen.text((X - 14, Bottom + 8), f'{Q:.2f}', font=Small, fill='white')
        R = YMin + (YMax - YMin) * Tick / 4
        Y = Bottom - Tick / 4 * (Bottom - Top)
        Pen.line((Left, Y, Right, Y), fill='#35404d')
        Pen.text((Left - 44, Y - 8), f'{R:.2f}', font=Small, fill='white')
    for Index, ((Q, R), Fields) in enumerate(zip(Ratios, Local)):
        X = Left + (Q - XMin) / (XMax - XMin) * (Right - Left)
        Y = Bottom - (R - YMin) / (YMax - YMin) * (Bottom - Top)
        Pen.ellipse((X - 5, Y - 5, X + 5, Y + 5), fill=Colors[Index])
        Pen.text((X + 7, Y + (10 if Fields['method'] == 'coupled8' else -20)), Fields['method'], font=Small, fill=Colors[Index])
    Pen.text((Left + 30, Bottom + 34), 'Query ratio (lower is cheaper)', font=Small, fill='white')
    Pen.text((Left, 84), 'RMSE ratio (lower is better)', font=Small, fill='white')
Pen.text((20, 465), 'Each scene has separate axis ranges. No physical-GPU timing or matched wall-clock acceptance.', font=Small, fill='white')
Pen.text((20, 492), 'Markers show observed aggregates, not confidence bounds; paired MSE uncertainty is retained in the report.', font=Small, fill='white')
Canvas.save(Destination / 'ReplayCorrelationCostQuality.png')

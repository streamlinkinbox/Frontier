# 🧩 Plot fixed convergence prefixes and analytic rare-event controls from actual numerical logs.
from pathlib import Path
import math
import re
import sys
from PIL import Image, ImageDraw, ImageFont

Results, Destination = map(Path, sys.argv[1:3])
Text = Results.read_text()
if 'CONVERGENCE_DECISION agreement=1 ' not in Text:
    raise RuntimeError('A completed agreement diagnostic is required')
Rows = []
for Line in Text.splitlines():
    if Line.startswith('CONVERGENCE_'):
        Rows.append((Line.split()[0], dict(re.findall(r'(\w+)=([^ ]+)', Line))))
Destination.mkdir(parents=True, exist_ok=True)
Font = ImageFont.load_default(size=15)
Small = ImageFont.load_default(size=13)
Colors = {'nee': '#f19578', 'power': '#77d3a6', 'bsdf': '#9abfff'}
Canvas = Image.new('RGB', (1200, 840), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((22, 15), 'Room convergence / same fixed GPU seed stream / cold Fresh8 / 528 measured diffuse points', font=Font, fill='white')
Pen.text((22, 42), 'NEE-only: orange | Power MIS: green | CPU BSDF-hit mean: blue | Error bars: nominal SE, not confidence guarantees', font=Small, fill='white')
for Panel, Depth in enumerate((1, 4)):
    Top = 115 + Panel * 355
    GPU = [Fields for Tag, Fields in Rows if Tag == 'CONVERGENCE_GPU' and int(Fields['bounces']) == Depth]
    CPU = next(Fields for Tag, Fields in Rows if Tag == 'CONVERGENCE_CPU' and int(Fields['bounces']) == Depth and Fields['method'] == 'bsdf')
    Reference, Error = float(CPU['mean']), float(CPU['nominal_se'])
    Limits = [Reference - 4 * Error, Reference + 4 * Error]
    for Fields in GPU:
        Mean, SE = float(Fields['mean']), float(Fields['nominal_se'])
        Limits += [Mean - SE, Mean + SE]
    Minimum, Maximum = min(Limits), max(Limits)
    Padding = (Maximum - Minimum) * .15
    Minimum -= Padding
    Maximum += Padding
    Left, Right, Bottom = 110, 1100, Top + 240
    def PositionY(Number):
        return Bottom - (Number - Minimum) / (Maximum - Minimum) * 240
    Pen.text((22, Top - 36), f'{Depth} bounce(s) / CPU BSDF reference {Reference:.6f} +/- {Error:.6f} nominal SE', font=Font, fill='white')
    Pen.rectangle((Left, PositionY(Reference + 4 * Error), Right, PositionY(Reference - 4 * Error)), fill='#202e43')
    for Tick in range(6):
        Number = Minimum + (Maximum - Minimum) * Tick / 5
        Y = PositionY(Number)
        Pen.line((Left, Y, Right, Y), fill='#35404d')
        Pen.text((20, Y - 8), f'{Number:.5f}', font=Small, fill='#c5d1df')
    Pen.line((Left, PositionY(Reference), Right, PositionY(Reference)), fill=Colors['bsdf'], width=2)
    for Method in ('nee', 'power'):
        Points = []
        for Fields in GPU:
            if Fields['method'] != Method:
                continue
            N = int(Fields['batches'])
            X = Left + (math.log2(N) - 5) / 3 * (Right - Left)
            Mean, SE = float(Fields['mean']), float(Fields['nominal_se'])
            Y = PositionY(Mean)
            Points.append((X, Y))
            Pen.line((X, PositionY(Mean - SE), X, PositionY(Mean + SE)), fill=Colors[Method], width=2)
            Pen.ellipse((X - 4, Y - 4, X + 4, Y + 4), fill=Colors[Method])
        Pen.line(Points, fill=Colors[Method], width=3)
    for Index, N in enumerate((32, 64, 128, 256)):
        X = Left + Index / 3 * (Right - Left)
        Pen.text((X - 10, Bottom + 12), str(N), font=Small, fill='white')
    Pen.text((470, Bottom + 34), 'Paired GPU batches (log2 spacing)', font=Small, fill='white')
Pen.text((22, 810), 'Shaded band: CPU BSDF mean +/- 4 nominal SE only. Earlier 32-batch failed acceptance is preserved.', font=Small, fill='#c5d1df')
Canvas.save(Destination / 'ReplayRoomConvergence.png')

Canvas = Image.new('RGB', (1200, 500), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((22, 15), 'Independent analytic rectangle / gap .019 / uniform-area NEE rare samples versus BSDF and power', font=Font, fill='white')
Analytic = [Fields for Tag, Fields in Rows if Tag == 'CONVERGENCE_ANALYTIC']
Exact = float(Analytic[0]['exact'])
Left, Right, Top, Bottom = 90, 1100, 85, 385
Minimum = min(float(F['mean']) - float(F['nominal_se']) for F in Analytic) - .4
Maximum = max(float(F['mean']) + float(F['nominal_se']) for F in Analytic) + .4
for Tick in range(6):
    Number = Minimum + (Maximum - Minimum) * Tick / 5
    Y = Bottom - (Number - Minimum) / (Maximum - Minimum) * (Bottom - Top)
    Pen.line((Left, Y, Right, Y), fill='#35404d')
    Pen.text((20, Y - 8), f'{Number:.2f}', font=Small, fill='white')
ExactY = Bottom - (Exact - Minimum) / (Maximum - Minimum) * (Bottom - Top)
Pen.line((Left, ExactY, Right, ExactY), fill='white', width=2)
for Method in ('nee', 'power', 'bsdf'):
    Points = []
    for Fields in Analytic:
        if Fields['method'] != Method:
            continue
        X = Left + (math.log2(int(Fields['samples'])) - 10) / 12 * (Right - Left)
        Mean, SE = float(Fields['mean']), float(Fields['nominal_se'])
        Y = Bottom - (Mean - Minimum) / (Maximum - Minimum) * (Bottom - Top)
        Half = SE / (Maximum - Minimum) * (Bottom - Top)
        Pen.line((X, Y - Half, X, Y + Half), fill=Colors[Method], width=2)
        Pen.ellipse((X - 4, Y - 4, X + 4, Y + 4), fill=Colors[Method])
        Points.append((X, Y))
    Pen.line(Points, fill=Colors[Method], width=3)
for Index, N in enumerate((1024, 16384, 262144, 4194304)):
    X = Left + Index / 3 * (Right - Left)
    Pen.text((X - 22, Bottom + 12), f'{N:,}', font=Small, fill='white')
Pen.text((22, 440), f'Exact integral {Exact:.8f} (white). NEE orange; power green; BSDF blue. Bars: nominal SE.', font=Small, fill='white')
Pen.text((22, 466), f"Maximum observed uniform-area contribution: {float(Analytic[-1]['maximum_nee']):,.4f}. No GPU timing or reservoir acceptance claim.", font=Small, fill='white')
Canvas.save(Destination / 'ReplayNearEmitterConvergence.png')

# Retain original first-batch RGB readbacks as PNGs, separately from numerical plots.
if len(sys.argv) == 4:
    Capture = Path(sys.argv[3])
    for Depth in (1, 4):
        for Method in ('Nee', 'Power'):
            with Image.open(Capture / f'Room{Depth}{Method}.ppm') as Original:
                if Original.size != (24, 24):
                    raise RuntimeError('Unexpected room readback dimensions')
                Original.convert('RGB').save(Destination / f'RoomConvergence{Depth}{Method}.png')

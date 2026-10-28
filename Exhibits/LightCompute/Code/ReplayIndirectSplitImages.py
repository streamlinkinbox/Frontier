#============================================================================================================================================
#                                                  REPLAYINDIRECTSPLITIMAGES.PY
#============================================================================================================================================
# 🧩 Present all fixed direct/indirect comparisons, including the cheaper whole-path control and actual query work.

from pathlib import Path
import argparse
import re
from PIL import Image, ImageDraw

Arguments = argparse.ArgumentParser()
Arguments.add_argument('capture', type=Path)
Arguments.add_argument('results', type=Path)
Arguments.add_argument('destination', type=Path)
Options = Arguments.parse_args()
Text = Options.results.read_text()
if 'SPLIT_DECISION ' not in Text:
    raise RuntimeError('A completed split comparison is required')
Rows = {}
Paired = {}
for Line in Text.splitlines():
    if Line.startswith('SPLIT_PAIRED '):
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        Paired[int(Fields['condition'])] = Fields
    if Line.startswith('SPLIT_RESULT '):
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        Rows[(int(Fields['condition']), Fields['method'])] = Fields
if len(Rows) != 6:
    raise RuntimeError('Both conditions and all three methods must be present')
Options.destination.mkdir(parents=True, exist_ok=True)
Canvas = Image.new('RGB', (1160, 700), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((16, 12), 'Actual Vulkan first-batch readbacks / four bounces / emitter sampling ON / nearest-neighbour enlargement', fill='white')
for Column, Label in enumerate(('Whole Fresh8 (cheaper)', 'Fresh direct + fresh indirect', 'Fresh direct + indirect RIS', 'Green: measured diffuse points')):
    Pen.text((16 + Column * 280, 38), Label, fill='white')
for Condition, Label in enumerate(('Camera plane / 32 x 32', 'Colored diffuse room / 24 x 24')):
    Top = 80 + Condition * 300
    Pen.text((16, Top - 18), Label, fill='#b8c4d4')
    for Column, Suffix in enumerate(('Whole', 'Fresh', 'Reuse', 'Mask')):
        Name = f'Split{Condition}{Suffix}'
        with Image.open(Options.capture / (Name + '.ppm')) as Source:
            Source = Source.convert('RGB')
            if Source.size != ((32, 32) if Condition == 0 else (24, 24)):
                raise RuntimeError('Unexpected split capture dimensions')
            Source.save(Options.destination / (Name + '.png'), optimize=True)
            Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Column * 280, Top))
Pen.text((16, 661), 'Fresh split should match whole Fresh8 up to rounding, but computes its components separately. Reuse needs eight warm-up submissions.', fill='#b8c4d4')
Pen.text((16, 681), 'Both split methods perform extra tracing. No equal-time comparison or physical-GPU speed claim.', fill='#b8c4d4')
Canvas.save(Options.destination / 'ReplayIndirectSplitComparison.png', optimize=True)
Chart = Image.new('RGB', (920, 470), '#121923')
Pen = ImageDraw.Draw(Chart)
Pen.text((16, 12), 'Direct/indirect split / red RMSE / lower is better / all fixed conditions and controls', fill='white')
Pen.text((16, 32), '32 paired batches; independent CPU reference; direct lighting never reservoir-weighted', fill='#b8c4d4')
for Condition, Label in enumerate(('Camera plane', 'Colored diffuse room')):
    Maximum = max(float(Row['rmse']) for (Index, _), Row in Rows.items() if Index == Condition) * 1.1
    Top = 72 + Condition * 170
    Pen.text((16, Top), Label + ' (own horizontal scale)', fill='white')
    for Index, (Method, Caption, Color) in enumerate((('whole_fresh', 'Whole Fresh8', '#8195ac'),
                                                     ('split_fresh', 'Split fresh', '#b3a5d4'),
                                                     ('split_reuse', 'Indirect RIS', '#6bd1bc'))):
        Error = float(Rows[(Condition, Method)]['rmse'])
        Width = round(Error / Maximum * 490)
        Vertical = Top + 24 + Index * 34
        Pen.text((16, Vertical + 4), Caption, fill='white')
        Pen.rectangle((165, Vertical, 165 + Width, Vertical + 24), fill=Color)
        Pen.text((175 + Width, Vertical + 4), f'{Error:.6f}', fill='white')
    Difference = float(Paired[Condition]['mse_difference'])
    Error = float(Paired[Condition]['nominal_se'])
    Pen.text((16, Top + 139), f'Paired MSE change: {Difference:.6g} +/- {Error:.6g} nominal batch SE', fill='#b8c4d4')
Pen.text((16, 423), 'Horizontal scales differ by condition; numeric RMSE labels are absolute. Room improvement is uncertain.', fill='#b8c4d4')
Pen.text((16, 445), 'Whole Fresh8 is cheaper. Warm-up and direct/indirect nearest/shadow queries are recorded separately.', fill='#b8c4d4')
Chart.save(Options.destination / 'ReplayIndirectSplitRmse.png', optimize=True)
print('Presented both fixed conditions, all controls and all eight first-batch readbacks')

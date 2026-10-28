#============================================================================================================================================
#                                                  REPLAYTEMPORALHOLDOUTIMAGES.PY
#============================================================================================================================================
# 🧩 Present every fixed emitter-enabled holdout condition, without selecting a favorable seed batch.

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
if 'HOLDOUT_DECISION ' not in Text:
    raise RuntimeError('A complete holdout experiment is required')
Rows = {}
for Line in Text.splitlines():
    if Line.startswith('HOLDOUT_RESULT '):
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        Rows[(int(Fields['condition']), Fields['method'])] = Fields
if len(Rows) != 8:
    raise RuntimeError('All four conditions and both methods must be present')
Options.destination.mkdir(parents=True, exist_ok=True)
Labels = ('Stationary / diffuse albedo 0.5', 'Camera +0.125', 'Camera +0.0625 / blocker +0.25',
          'Stationary / diffuse albedo 0.8')
Canvas = Image.new('RGB', (860, 1140), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((16, 10), 'Actual 32 x 32 Vulkan readbacks / four bounces / emitter sampling ON / first batch', fill='white')
for Column, Label in enumerate(('Fresh8', 'Bounded temporal + cache', 'Green: plane / cyan: newly exposed')):
    Pen.text((16 + Column * 280, 34), Label, fill='white')
for Condition, Label in enumerate(Labels):
    Top = 76 + Condition * 256
    Pen.text((16, Top - 18), Label, fill='#b8c4d4')
    for Column, Suffix in enumerate(('Fresh', 'Temporal', 'Mask')):
        Name = f'Holdout{Condition}{Suffix}'
        with Image.open(Options.capture / (Name + '.ppm')) as Source:
            Source = Source.convert('RGB')
            if Source.size != (32, 32):
                raise RuntimeError('Unexpected holdout readback size')
            Source.save(Options.destination / (Name + '.png'), optimize=True)
            Canvas.paste(Source.resize((224, 224), Image.Resampling.NEAREST), (16 + Column * 280, Top))
Pen.text((16, 1100), 'All visible plane points measured, including newly exposed points. No radiance/history-admission filtering.',
         fill='#b8c4d4')
Pen.text((16, 1120), 'Eight-current-call ceiling, not equal work/time. Warm-up counted separately. SwiftShader CPU Vulkan.',
         fill='#b8c4d4')
Canvas.save(Options.destination / 'ReplayTemporalHoldoutComparison.png', optimize=True)
Chart = Image.new('RGB', (860, 595), '#121923')
Pen = ImageDraw.Draw(Chart)
Pen.text((16, 12), 'Emitter-enabled holdout / red RMSE / lower is better', fill='white')
Pen.text((16, 32), '32 independent paired batches / four bounces / 4096 CPU reference paths per plane pixel', fill='#b8c4d4')
Maximum = max(float(Row['rmse']) for Row in Rows.values()) * 1.1
for Condition, Label in enumerate(Labels):
    Top = 68 + Condition * 120
    Pen.text((16, Top), Label, fill='white')
    for Index, (Method, Caption, Color) in enumerate((('fresh_eight', 'Fresh8', '#8195ac'),
                                                    ('bounded_temporal', 'Bounded temporal', '#6bd1bc'))):
        Error = float(Rows[(Condition, Method)]['rmse'])
        Width = round(Error / Maximum * 475)
        Vertical = Top + 23 + Index * 31
        Pen.text((16, Vertical + 4), Caption, fill='white')
        Pen.rectangle((160, Vertical, 160 + Width, Vertical + 22), fill=Color)
        Pen.text((170 + Width, Vertical + 4), f'{Error:.6f}', fill='white')
Pen.text((16, 563), 'Same budget ceiling, not equal calls or frame time. Reference noise and batch uncertainty remain.',
         fill='#b8c4d4')
Chart.save(Options.destination / 'ReplayTemporalHoldoutRmse.png', optimize=True)
print('Presented all four fixed conditions and both methods')

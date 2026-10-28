#============================================================================================================================================
#                                                    REPLAYRESETBUDGETIMAGES.PY
#============================================================================================================================================
# 🧩 Present the first reset batch and the complete independently seeded quality comparison, with its larger work budget.

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
if 'PASS metadata-only fresh reset budget' not in Text:
    raise RuntimeError('A completed reset comparison is required')
Rows = {}
Work = dict(re.findall(r'(\w+)=([^ ]+)', next(Line for Line in Text.splitlines() if Line.startswith('RESET_WORK '))))
for Line in Text.splitlines():
    if Line.startswith('RESET_RESULT '):
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        Rows[Fields['method']] = Fields
if set(Rows) != {'previous', 'fresh_reset'}:
    raise RuntimeError('Both reset methods must be present')
Options.destination.mkdir(parents=True, exist_ok=True)
Canvas = Image.new('RGB', (860, 390), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((16, 12), 'Actual 32 x 32 readbacks / moving blocker / four bounces / emitter sampling ON / first batch', fill='white')
for Column, (Name, Label) in enumerate((('ResetPrevious', 'Previous spatial fallback'),
                                       ('ResetFixed', 'Eight-fresh reset fallback'),
                                       ('ResetMask', 'Green: all measured plane points'))):
    Pen.text((16 + Column * 280, 38), Label, fill='white')
    with Image.open(Options.capture / (Name + '.ppm')) as Source:
        Source = Source.convert('RGB')
        if Source.size != (32, 32):
            raise RuntimeError('Unexpected reset capture dimensions')
        Source.save(Options.destination / (Name + '.png'), optimize=True)
        Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Column * 280, 65))
with Image.open(Options.capture / 'ResetFresh.ppm') as Source:
    Source.convert('RGB').save(Options.destination / 'ResetFresh.png', optimize=True)
Pen.text((16, 344), 'Corrected output is byte-identical to Fresh8. Previous fallback executes at most four current paths.', fill='#b8c4d4')
Pen.text((16, 366), 'Quality is restored by spending the full eight-path budget, not by a free temporal-quality improvement.', fill='#b8c4d4')
Canvas.save(Options.destination / 'ReplayResetBudgetComparison.png', optimize=True)
Chart = Image.new('RGB', (860, 270), '#121923')
Pen = ImageDraw.Draw(Chart)
Pen.text((16, 12), 'Reset comparison / red RMSE / 32 independent paired batches / lower is better', fill='white')
Maximum = max(float(Row['rmse']) for Row in Rows.values()) * 1.1
for Index, (Method, Label, Color) in enumerate((('previous', 'Previous fallback', '#8195ac'),
                                              ('fresh_reset', 'Fresh8 reset', '#6bd1bc'))):
    Error = float(Rows[Method]['rmse'])
    Width = round(Error / Maximum * 475)
    Vertical = 65 + Index * 65
    Pen.text((16, Vertical + 4), Label, fill='white')
    Pen.rectangle((160, Vertical, 160 + Width, Vertical + 28), fill=Color)
    Pen.text((170 + Width, Vertical + 4), f'{Error:.6f}', fill='white')
Pen.text((16, 216), f"Current calls: previous {int(Work['previous_current']):,}; corrected {int(Work['fixed_current']):,}. Warm-up also costs more.", fill='#b8c4d4')
Pen.text((16, 240), 'SwiftShader CPU Vulkan; not equal time or a physical-GPU speedup.', fill='#b8c4d4')
Chart.save(Options.destination / 'ReplayResetBudgetRmse.png', optimize=True)
print('Presented first-batch reset readbacks and both complete quality rows')

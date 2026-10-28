#============================================================================================================================================
#                                                REPLAYTEMPORALCONTRACTIMAGES.PY
#============================================================================================================================================
# 🧩 Present actual temporal Vulkan captures and enumerated finite-space variances without an FPS claim.

from pathlib import Path
import argparse
import re
from PIL import Image, ImageDraw


def Main():
    Arguments = argparse.ArgumentParser()
    Arguments.add_argument('capture', type=Path)
    Arguments.add_argument('results', type=Path)
    Arguments.add_argument('destination', type=Path)
    Options = Arguments.parse_args()
    Text = Options.results.read_text()
    if 'PASS temporal GPU geometry' not in Text or 'TEMPORAL_MAX_COUNT' not in Text:
        raise RuntimeError('Completed temporal validation is required')
    Options.destination.mkdir(parents=True, exist_ok=True)
    Canvas = Image.new('RGB', (680, 350), '#121923')
    Pen = ImageDraw.Draw(Canvas)
    Pen.text((16, 12), 'Actual 64 x 64 Vulkan readbacks / one bounce / selected-history retracing', fill='white')
    for Column, (Name, Label) in enumerate((('TemporalCurrent', 'Half-pixel camera move'),
                                           ('TemporalSourceOff', 'Source off: all 4,096 RGB pixels zero'))):
        with Image.open(Options.capture / (Name + '.png')) as Source:
            Source = Source.convert('RGB')
            if Source.size != (64, 64):
                raise RuntimeError('Unexpected capture dimensions')
            Source.save(Options.destination / (Name + '.png'), optimize=True)
            Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + 330 * Column, 62))
        Pen.text((16 + 330 * Column, 40), Label, fill='white')
    Pen.text((16, 330), 'Shared exposure 3; no denoising. SwiftShader CPU Vulkan, not a game-time measurement.', fill='#b8c4d4')
    Canvas.save(Options.destination / 'ReplayTemporalContractComparison.png', optimize=True)

    Variances = {}
    for Line in Text.splitlines():
        if Line.startswith('TEMPORAL_VARIANCE '):
            Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
            Variances[int(Fields['correlated'])] = float(Fields['variance'])
    if len(Variances) != 2:
        raise RuntimeError('Both correlation conditions are required')
    Chart = Image.new('RGB', (760, 325), '#121923')
    Pen = ImageDraw.Draw(Chart)
    Pen.text((16, 12), 'Finite four-seed model: correct mean does not imply independent-sample variance', fill='white')
    Pen.text((16, 35), 'Six submissions; mean 3.5 in both temporal cases. This is not a rendered quality benchmark.', fill='#b8c4d4')
    Rows = (('Six independent fresh mean', 12.25 / 6, '#8195ac'),
            ('Temporal / independent fresh', Variances[0], '#6bd1bc'),
            ('Temporal / correlated fresh', Variances[1], '#e9af70'))
    Maximum = max(Row[1] for Row in Rows) * 1.15
    for Index, (Label, Variance, Colour) in enumerate(Rows):
        Top = 85 + Index * 65
        Pen.text((16, Top), Label, fill='white')
        Width = round(Variance / Maximum * 365)
        Pen.rectangle((250, Top, 250 + Width, Top + 23), fill=Colour)
        Pen.text((260 + Width, Top + 5), f'{Variance:.6f}', fill='white')
    Pen.text((16, 298), 'Exact outcome enumeration in long-double arithmetic; lower variance is better.', fill='#b8c4d4')
    Chart.save(Options.destination / 'ReplayTemporalCorrelation.png', optimize=True)
    print('Wrote actual readback comparison and finite-space correlation chart')


if __name__ == '__main__':
    Main()

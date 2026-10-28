#============================================================================================================================================
#                                                    REPLAYBOUNDEDTEMPORALIMAGES.PY
#============================================================================================================================================
# 🧩 Present actual eight-path Vulkan readbacks and all four logged quality comparisons without selecting winners.

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
    if 'PASS bounded eight-current-path comparison' not in Text:
        raise RuntimeError('A completed budget comparison log is required')
    Rows = {}
    for Line in Text.splitlines():
        if Line.startswith('BOUNDED_RESULT '):
            Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
            Rows[(int(Fields['bounces']), int(Fields['facets']), Fields['mode'])] = Fields
    if len(Rows) != 8:
        raise RuntimeError('All four conditions and both methods must be present')
    Options.destination.mkdir(parents=True, exist_ok=True)
    Canvas = Image.new('RGB', (860, 665), '#121923')
    Pen = ImageDraw.Draw(Canvas)
    Pen.text((16, 10), 'Actual 64 x 64 Vulkan readbacks / four bounces / enlarged nearest-neighbour', fill='white')
    Labels = ('Eight fresh current paths', 'Bounded temporal RIS', 'Green: 8-call budget ROI')
    for Index, Label in enumerate(Labels):
        Pen.text((16 + 280 * Index, 32), Label, fill='white')
    for Row, Suffix in enumerate(('Cosine', 'Facet')):
        Top = 70 + Row * 290
        Pen.text((16, Top - 16), 'Explicit emitter sampling: ' + ('off' if Row == 0 else 'on'), fill='#b8c4d4')
        for Column, Prefix in enumerate(('BoundedFresh', 'BoundedReuse', 'BoundedMask')):
            Name = Prefix + Suffix
            with Image.open(Options.capture / (Name + '.png')) as Source:
                Source = Source.convert('RGB')
                if Source.size != (64, 64):
                    raise RuntimeError('Unexpected readback dimensions')
                Source.save(Options.destination / (Name + '.png'), optimize=True)
                Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Column * 280, Top))
    Pen.text((16, 638), 'Green: at most eight current path calls. Reuse needs eight warm-up frames. Not equal total work/time.',
             fill='#b8c4d4')
    Canvas.save(Options.destination / 'ReplayBoundedTemporalComparison.png', optimize=True)
    for Suffix in ('Cosine', 'Facet'):
        Name = 'BoundedOff' + Suffix + '.png'
        with Image.open(Options.capture / Name) as Source:
            Source = Source.convert('RGB')
            if Source.size != (64, 64) or any(Source.tobytes()):
                raise RuntimeError('Source-off capture must contain 4096 black pixels')
            Source.save(Options.destination / Name, optimize=True)

    Chart = Image.new('RGB', (860, 595), '#121923')
    Pen = ImageDraw.Draw(Chart)
    Pen.text((16, 12), 'Red RMSE / eight-current-call ceiling at measured plane points / lower is better', fill='white')
    Pen.text((16, 32), '32 batches; one-bounce area integral or four-bounce independent CPU reference', fill='#b8c4d4')
    Conditions = ((1, 0), (1, 1), (4, 0), (4, 1))
    Maximum = max(float(Row['rmse']) for Row in Rows.values()) * 1.1
    for Group, (Bounces, Facets) in enumerate(Conditions):
        Top = 68 + Group * 120
        Pen.text((16, Top), f'{Bounces} bounce(s), explicit emitter sampling ' + ('on' if Facets else 'off'), fill='white')
        for Index, (Mode, Label, Color) in enumerate((('fresh_eight', 'Eight fresh', '#8195ac'),
                                                    ('bounded_temporal', 'Bounded temporal RIS', '#6bd1bc'))):
            Row = Rows[(Bounces, Facets, Mode)]
            Rmse = float(Row['rmse'])
            Vertical = Top + 23 + Index * 31
            Width = round(Rmse / Maximum * 475)
            Pen.text((16, Vertical + 4), Label, fill='white')
            Pen.rectangle((160, Vertical, 160 + Width, Vertical + 22), fill=Color)
            Pen.text((170 + Width, Vertical + 4), f'{Rmse:.6f}', fill='white')
    Pen.text((16, 563), 'SwiftShader CPU Vulkan. Not equal time or total work; warm-up excluded from current-path count.',
             fill='#b8c4d4')
    Chart.save(Options.destination / 'ReplayBoundedTemporalRmse.png', optimize=True)
    print('Wrote all-condition readback comparison and eight-row RMSE chart')


if __name__ == '__main__':
    Main()

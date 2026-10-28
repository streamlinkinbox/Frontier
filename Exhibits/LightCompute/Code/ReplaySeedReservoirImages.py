#============================================================================================================================================
#                                                    REPLAYSEEDRESERVOIRIMAGES.PY
#============================================================================================================================================
# 🧩 Present actual Vulkan readbacks and logged RMSE without smoothing, denoising or invented reference images.

from pathlib import Path
import argparse
import re
from PIL import Image, ImageDraw


def Main():
    Arguments = argparse.ArgumentParser()
    Arguments.add_argument('capture', type=Path, help='Directory containing the three captured PNG files')
    Arguments.add_argument('results', type=Path, help='Completed RunReplaySeedReservoir.sh log')
    Arguments.add_argument('destination', type=Path)
    Options = Arguments.parse_args()
    Options.destination.mkdir(parents=True, exist_ok=True)
    Names = ('SeedAverage', 'SeedReservoirLocal', 'SeedReservoirSpatial')
    Labels = ('Seed arithmetic mean', 'Seed RIS / local', 'Seed RIS / spatial')
    Canvas = Image.new('RGB', (840, 350), '#121923')
    Pen = ImageDraw.Draw(Canvas)
    Pen.text((16, 12), 'Actual Vulkan readbacks / 64 x 64 each / enlarged nearest-neighbour', fill='white')
    for Index, (Name, Label) in enumerate(zip(Names, Labels)):
        with Image.open(Options.capture / (Name + '.png')) as Source:
            Source = Source.convert('RGB')
            if Source.size != (64, 64):
                raise RuntimeError('Unexpected capture dimensions')
            Source.save(Options.destination / (Name + '.png'), optimize=True)
            Canvas.paste(Source.resize((256, 256), Image.Resampling.NEAREST), (16 + Index * 280, 55))
        Pen.text((16 + Index * 280, 35), Label, fill='white')
    Pen.text((16, 326), 'Four bounces, emissive-area sampling, black environment. Diagnostic fixture, not a game viewport.',
             fill='#b8c4d4')
    Canvas.save(Options.destination / 'ReplaySeedReservoirComparison.png', optimize=True)

    Rows = {}
    for Line in Options.results.read_text().splitlines():
        if Line.startswith('SEED_RIS_REFERENCE '):
            Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
            Rows[(int(Fields['bounces']), Fields['mode'])] = float(Fields['rmse'])
    if len(Rows) != 6 or 'PASS bounded seed-bank RIS' not in Options.results.read_text():
        raise RuntimeError('A completed six-row validation log is required')
    Chart = Image.new('RGB', (840, 420), '#121923')
    Pen = ImageDraw.Draw(Chart)
    Pen.text((20, 14), 'Measured red RMSE / independent reference / 16 batches x 794 plane points', fill='white')
    Pen.text((20, 35), 'Lower is better. NOT equal time or equal path count; no speedup claim.', fill='#b8c4d4')
    Colors = ('#8195ac', '#e3b778', '#6bd1bc')
    Modes = ('seed_average', 'seed_ris_local', 'seed_ris_spatial')
    for Group, Bounces in enumerate((1, 4)):
        Offset = 76 + Group * 155
        Pen.text((20, Offset), str(Bounces) + ' bounce(s)', fill='white')
        for Index, (Mode, Label, Color) in enumerate(zip(Modes, Labels, Colors)):
            Vertical = Offset + 24 + Index * 35
            Rmse = Rows[(Bounces, Mode)]
            Pen.text((20, Vertical + 4), Label, fill='white')
            Width = round(Rmse / .2 * 430)
            Pen.rectangle((210, Vertical, 210 + Width, Vertical + 22), fill=Color)
            Pen.text((220 + Width, Vertical + 4), f'{Rmse:.6f}', fill='white')
    Pen.text((20, 390), 'At full banks: mean and local RIS = 4 current paths; spatial RIS = up to 8. SwiftShader CPU Vulkan.',
             fill='#b8c4d4')
    Chart.save(Options.destination / 'ReplaySeedReservoirRmse.png', optimize=True)
    print('Wrote actual readback comparison and six-row RMSE chart to', Options.destination)


if __name__ == '__main__':
    Main()

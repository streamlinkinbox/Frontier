#============================================================================================================================================
#                                                    REPLAYCOMPACTSTORAGEIMAGES.PY
#============================================================================================================================================
# 🧩 Present actual matching Vulkan readbacks beside measured output-memory allocations.

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
    Rows = {}
    for Line in Options.results.read_text().splitlines():
        if Line.startswith('COMPACT_ALLOCATION '):
            Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
            Rows[Fields['layout']] = Fields
    if 'PASS actual compact' not in Options.results.read_text():
        raise RuntimeError('A completed validation log is required')
    Legacy = int(Rows['legacy_capture']['output_vk_allocation_bytes'])
    Compact = int(Rows['compact_capture']['output_vk_allocation_bytes'])
    if not 0 < Compact < Legacy:
        raise RuntimeError('Measured output allocation did not decrease')
    Images = []
    for Name in ('CompactReplayLegacy', 'CompactReplayPacked'):
        with Image.open(Options.capture / (Name + '.png')) as Source:
            Images.append(Source.convert('RGB'))
    if any(Image.size != (64, 64) for Image in Images) or Images[0].tobytes() != Images[1].tobytes():
        raise RuntimeError('Compact capture is not a pixel-identical 64x64 readback')
    Options.destination.mkdir(parents=True, exist_ok=True)
    Canvas = Image.new('RGB', (760, 480), '#121923')
    Pen = ImageDraw.Draw(Canvas)
    Pen.text((20, 12), 'Actual Vulkan readbacks: identical pixels, smaller storage / SwiftShader CPU Vulkan', fill='white')
    for Index, (Name, Label) in enumerate(zip(('CompactReplayLegacy', 'CompactReplayPacked'), ('Legacy', 'Compact'))):
        Images[Index].save(Options.destination / (Name + '.png'), optimize=True)
        Canvas.paste(Images[Index].resize((256, 256), Image.Resampling.NEAREST), (20 + Index * 370, 60))
        Pen.text((20 + Index * 370, 36), Label + ' / 64 x 64, enlarged nearest-neighbour', fill='white')
    Pen.text((20, 334), 'Measured output VkDeviceMemory allocation (not whole-engine VRAM)', fill='white')
    for Index, (Label, Bytes, Color) in enumerate((('Legacy', Legacy, '#8195ac'), ('Compact', Compact, '#6bd1bc'))):
        Vertical = 364 + Index * 36
        Width = round(Bytes / Legacy * 410)
        Pen.text((20, Vertical + 4), Label, fill='white')
        Pen.rectangle((100, Vertical, 100 + Width, Vertical + 23), fill=Color)
        Pen.text((110 + Width, Vertical + 4), f'{Bytes:,} bytes', fill='white')
    Pen.text((20, 449), f'Output allocation reduction: {100 * (1 - Compact / Legacy):.2f}%. No FPS/speedup claim. Not a game viewport.',
             fill='#b8c4d4')
    Canvas.save(Options.destination / 'ReplayCompactStorageComparison.png', optimize=True)
    print('Wrote exact readback/memory comparison')


if __name__ == '__main__':
    Main()

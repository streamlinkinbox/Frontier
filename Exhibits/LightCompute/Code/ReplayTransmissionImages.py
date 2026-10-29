#============================================================================================================================================
#                                                REPLAYTRANSMISSIONIMAGES.PY
#============================================================================================================================================
# 🧩 Plot retained Vulkan pane-transport measurements against the independent area integral.

from pathlib import Path
import re
import sys
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
BeforePath = Root / 'Exhibits/LightCompute/Results/ReplayTransmission-before.txt'
AfterPath = Root / 'Exhibits/LightCompute/Results/ReplayTransmission-release.txt'
Destination = Path(sys.argv[1]) if len(sys.argv) > 1 else Root / 'Exhibits/LightCompute/Images/ReplayTransmissionComparison.png'


def Extract(Pathname, Before):
    Result = {}
    for Line in Pathname.read_text().splitlines():
        if not Line.startswith('REPLAY_TRANSMISSION '):
            continue
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if not Before and (Fields.get('panes') != '1' or Fields.get('bounces') != '2'):
            continue
        Method = Fields['method']
        if Method in ('bsdf', 'balance', 'power'):
            Result[Method] = (float(Fields['mean']), float(Fields['batch_se']), float(Fields['reference']))
    if set(Result) != {'bsdf', 'balance', 'power'}:
        raise ValueError(f'Expected all three measured methods in {Pathname}')
    return Result


Old = Extract(BeforePath, True)
New = Extract(AfterPath, False)
Reference = New['balance'][2]
if abs(Reference - Old['balance'][2]) > 1e-9:
    raise ValueError('Before and after use different reference integrals')

FontPath = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
BoldPath = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
try:
    Title = ImageFont.truetype(BoldPath, 24)
    Heading = ImageFont.truetype(BoldPath, 17)
    Body = ImageFont.truetype(FontPath, 15)
    Small = ImageFont.truetype(FontPath, 13)
except OSError:
    Title = Heading = Body = Small = ImageFont.load_default()

Canvas = Image.new('RGB', (1200, 710), '#111923')
Draw = ImageDraw.Draw(Canvas)
Draw.text((36, 28), 'Thin-sheet replay: independent reference check', font=Title, fill='#f1f5fa')
Draw.text((36, 66), 'One pane, two bounces | 24 batches x 4,096 pixels per method | SwiftShader CPU Vulkan',
          font=Body, fill='#bdcddd')
Draw.text((36, 91), 'Bars are measured mean red radiance; whiskers show 5 nominal batch standard errors.',
          font=Small, fill='#98acbf')

Left, Top, Width, Height = 105, 158, 620, 375
Bottom = Top + Height
Maximum = .65
Draw.rounded_rectangle((36, 130, 750, 620), radius=10, fill='#192634', outline='#35495a', width=2)
for Tick in range(0, 7):
    Radiance = Tick * .1
    Y = Bottom - int(Radiance / Maximum * Height)
    Draw.line((Left, Y, Left + Width, Y), fill='#344654', width=1)
    Draw.text((55, Y - 9), f'{Radiance:.1f}', font=Small, fill='#b1c1cf')
Draw.line((Left, Top, Left, Bottom), fill='#91a4b3', width=2)
Draw.line((Left, Bottom, Left + Width, Bottom), fill='#91a4b3', width=2)
RefY = Bottom - int(Reference / Maximum * Height)
for X in range(Left, Left + Width, 12):
    Draw.line((X, RefY, min(X + 7, Left + Width), RefY), fill='#f3cd77', width=2)
Draw.text((Left + 7, Top + 10), f'Dashed line: integral {Reference:.4f}', font=Small, fill='#f3cd77')

Methods = [('bsdf', 'BSDF only'), ('balance', 'Balance MIS'), ('power', 'Power MIS')]
Colours = ('#e58a86', '#63d6b3')
for Index, (Method, Label) in enumerate(Methods):
    Centre = Left + 110 + 190 * Index
    for Offset, Group, Colour in [(-35, Old, Colours[0]), (35, New, Colours[1])]:
        Mean, Error, _ = Group[Method]
        X = Centre + Offset
        Y = Bottom - int(Mean / Maximum * Height)
        Draw.rounded_rectangle((X - 25, Y, X + 25, Bottom - 1), radius=4, fill=Colour)
        Upper = Bottom - int(min(Maximum, Mean + 5 * Error) / Maximum * Height)
        Lower = Bottom - int(max(0.0, Mean - 5 * Error) / Maximum * Height)
        Draw.line((X, Upper, X, Lower), fill='#f0f5f8', width=2)
        Draw.line((X - 6, Upper, X + 6, Upper), fill='#f0f5f8', width=2)
        Draw.line((X - 6, Lower, X + 6, Lower), fill='#f0f5f8', width=2)
        Draw.text((X - 28, max(Top + 2, Y - 22)), f'{Mean:.4f}', font=Small, fill='#f3f6f8')
    Draw.text((Centre - 54, Bottom + 15), Label, font=Small, fill='#d8e3eb')
Draw.rectangle((95, 588, 111, 604), fill=Colours[0])
Draw.text((118, 586), 'Before repair', font=Small, fill='#d8e3eb')
Draw.rectangle((245, 588, 261, 604), fill=Colours[1])
Draw.text((268, 586), 'After repair', font=Small, fill='#d8e3eb')

Draw.rounded_rectangle((775, 130, 1164, 620), radius=10, fill='#192634', outline='#35495a', width=2)
Draw.text((798, 157), 'Additional Vulkan checks', font=Heading, fill='#f1f5fa')
Draw.line((798, 188, 1140, 188), fill='#405362', width=1)
Details = [
    ('Equal-seed proposals', '0 / 64 before  ->  64 / 64 after'),
    ('Four-pane stack fallback', '4,096 forced overflows; max diff 0'),
    ('MIS ratio limits', 'Zero, one, +infinity and NaN checked'),
    ('Nine-pane segment', 'Nonzero and matches analytic product'),
    ('Independent reference', 'Double area quadrature; no GPU BVH'),
]
Y = 211
for Label, Detail in Details:
    Draw.text((798, Y), Label, font=Body, fill='#e6f0f4')
    Draw.text((798, Y + 26), Detail, font=Small, fill='#8fd9c2')
    Y += 86
Draw.text((36, 649), 'This is a bounded correctness fixture, not a physical-GPU speed or full-renderer claim.',
          font=Body, fill='#b8c8d5')
Draw.text((36, 676), 'Baseline: d9cbe17 | After: see ReplayTransmission-release.txt and the source hashes.',
          font=Small, fill='#91a6b7')
Destination.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Destination)
print('REPLAY_TRANSMISSION_IMAGE', Destination, Canvas.size)

"""Generate a measured chart from the retained material-sampling logs."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Output = Root / 'Exhibits/LightCompute/Images/ReplayMaterialSamplingComparison.png'


def DecodeRows(Pathname):
    Rows = {}
    for Line in Pathname.read_text().splitlines():
        if not Line.startswith('REPLAY_MATERIAL case='):
            continue
        Fields = dict(Part.split('=', 1) for Part in Line.split()[1:])
        Rows[Fields['case']] = {Name: float(Entry) for Name, Entry in Fields.items() if Name != 'case'}
    return Rows


def ResolveFont(Size, Bold=False):
    Name = 'DejaVuSans-Bold.ttf' if Bold else 'DejaVuSans.ttf'
    try:
        return ImageFont.truetype(Name, Size)
    except OSError:
        return ImageFont.load_default()


Before = DecodeRows(Results / 'ReplayMaterialSampling-baseline.txt')
After = DecodeRows(Results / 'ReplayMaterialSampling-release.txt')
for Label in ('faint', 'zero', 'matte', 'metal', 'coat', 'sheen'):
    if Label not in Before or Label not in After:
        raise ValueError('Missing measured row: ' + Label)

Canvas = Image.new('RGB', (1500, 1040), '#101923')
Pen = ImageDraw.Draw(Canvas)
Title = ResolveFont(29, True)
Heading = ResolveFont(20, True)
Body = ResolveFont(17)
Small = ResolveFont(15)
Bright = '#edf3fa'
Muted = '#a9bbcc'
Coral = '#e58a86'
Green = '#60d1b4'
Gold = '#f3ca76'
Border = '#395064'
Panel = '#192735'

Pen.text((42, 25), 'Authored replay material sampling: measured correction', font=Title, fill=Bright)
Pen.text((42, 70), 'SwiftShader CPU Vulkan | 32 batches x 4,096 samples | red-channel hemisphere tests',
         font=Body, fill=Muted)


def ConstructPanel(Box, Label, Caption):
    Pen.rounded_rectangle(Box, radius=12, fill=Panel, outline=Border, width=2)
    Pen.text((Box[0] + 22, Box[1] + 18), Label, font=Heading, fill=Bright)
    Pen.text((Box[0] + 22, Box[1] + 49), Caption, font=Small, fill=Muted)


def ConstructBars(Box, Maximum, Entries):
    Left = Box[0] + 145
    Right = Box[2] - 90
    Width = Right - Left
    Top = Box[1] + 106
    for Index, (Label, Amount, Colour) in enumerate(Entries):
        Y = Top + Index * 66
        Pen.text((Box[0] + 22, Y + 8), Label, font=Body, fill=Bright)
        Pen.rounded_rectangle((Left, Y, Left + Width * min(Amount / Maximum, 1), Y + 38),
                              radius=4, fill=Colour)
        Pen.text((Right + 8, Y + 8), f'{Amount:.4f}', font=Body, fill=Bright)


LeftBox = (40, 115, 735, 410)
RightBox = (765, 115, 1460, 410)
ConstructPanel(LeftBox, 'Faint layered material', 'Mean red reflectance; displayed values multiplied by 10^7')
ConstructBars(LeftBox, 1.08, [
    ('Before', Before['faint']['importance'], Coral),
    ('After', After['faint']['importance'], Green),
    ('Reference', After['faint']['reference'], Gold),
])
ConstructPanel(RightBox, 'Zero-coverage sampler', 'Unit cosine integrand divided by the reported proposal density')
ConstructBars(RightBox, 1.35, [
    ('Before', Before['zero']['integrand'], Coral),
    ('After', After['zero']['integrand'], Green),
    ('Reference', 1.0, Gold),
])

Box = (40, 440, 1460, 903)
ConstructPanel(Box, 'Single-sample RMSE relative to cosine sampling',
               'One material evaluation per valid draw; lower is better. Dashed line = cosine baseline.')
PlotLeft = 260
PlotRight = 1370
PlotWidth = PlotRight - PlotLeft
Maximum = 2.0
LineX = PlotLeft + PlotWidth / Maximum
for Y in range(525, 827, 12):
    Pen.line((LineX, Y, LineX, Y + 6), fill=Gold, width=2)
for Index, (Label, Name) in enumerate((('Metal', 'metal'), ('Coated', 'coat'),
                                        ('Matte', 'matte'), ('Sheen', 'sheen'))):
    Y = 528 + Index * 78
    Ratio = After[Name]['importance_rmse'] / After[Name]['cosine_rmse']
    Pen.text((68, Y + 9), Label, font=Heading, fill=Bright)
    Colour = Green if Ratio < 1 else Coral
    Pen.rounded_rectangle((PlotLeft, Y, PlotLeft + PlotWidth * Ratio / Maximum, Y + 40), radius=4, fill=Colour)
    Change = 100.0 * (Ratio - 1.0)
    TextX = min(PlotLeft + PlotWidth * Ratio / Maximum + 12, PlotRight - 110)
    Pen.text((TextX, Y + 10), f'{Ratio:.3f}x ({Change:+.1f}%)', font=Body, fill=Bright)
Pen.text((68, 853), 'The same sampler is better on glossy cases and worse on these matte/sheen cases.',
         font=Body, fill=Muted)
Pen.text((42, 935), 'Reference: separate double-precision integration of the same GGX/Charlie model; not physical ground truth.',
         font=Small, fill=Muted)
Pen.text((42, 961), 'No physical-GPU timing, game frame rate, full-scene material or general ReSTIR claim.', font=Small, fill=Muted)
Output.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Output)
print('SAVED', Output)

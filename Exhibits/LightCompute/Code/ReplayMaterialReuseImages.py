"""Plot retained, measured material-reuse outcomes and their unequal ray counts."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Log = Root / 'Exhibits/LightCompute/Results/ReplayMaterialReuse-release.txt'
Output = Root / 'Exhibits/LightCompute/Images/ReplayMaterialReuseComparison.png'
Modes = ('fresh1', 'fresh4', 'temporal', 'spatial_disk', 'temporal_disk')
Labels = ('Fresh 1', 'Fresh 4', 'Temporal', 'Spatial disk', 'Temporal + disk')


def Font(Size, Bold=False):
    Name = 'DejaVuSans-Bold.ttf' if Bold else 'DejaVuSans.ttf'
    try:
        return ImageFont.truetype(Name, Size)
    except OSError:
        return ImageFont.load_default()


def ParseRows():
    Content = Log.read_text()
    if 'PASS ReplayMaterialReuseValidation' not in Content:
        raise ValueError('Cannot draw a passing comparison from an incomplete or failed log')
    Rows = {}
    for Line in Content.splitlines():
        if not Line.startswith('REPLAY_MATERIAL_REUSE case='):
            continue
        Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
        Key = (Fields.pop('case'), Fields.pop('mode'))
        Rows[Key] = {Name: float(Value) for Name, Value in Fields.items()}
    for Case in ('metal', 'faint'):
        for Mode in Modes:
            if (Case, Mode) not in Rows:
                raise ValueError('Missing measured result: ' + str((Case, Mode)))
    return Rows


Rows = ParseRows()
Canvas = Image.new('RGB', (1580, 990), '#101923')
Draw = ImageDraw.Draw(Canvas)
Title, Heading, Body, Small = Font(29, True), Font(20, True), Font(16), Font(14)
White, Muted, Green, Gray, Gold = '#edf3fa', '#a9bbcc', '#60d1b4', '#8da7bd', '#f3ca76'
Panel, Border = '#192735', '#395064'
Draw.text((42, 25), 'Material replay reuse: quality and counted work', font=Title, fill=White)
Draw.text((42, 69), 'SwiftShader CPU Vulkan | 32 paired cohorts x 1,024 supplied receivers | one bounce',
          font=Body, fill=Muted)


def PanelFor(Case, Box, HeadingText, Caption):
    X0, Y0, X1, Y1 = Box
    Draw.rounded_rectangle(Box, radius=12, fill=Panel, outline=Border, width=2)
    Draw.text((X0 + 22, Y0 + 19), HeadingText, font=Heading, fill=White)
    Draw.text((X0 + 22, Y0 + 50), Caption, font=Small, fill=Muted)
    Draw.text((X0 + 22, Y0 + 89), 'Method', font=Small, fill=Muted)
    Draw.text((X0 + 258, Y0 + 89), 'RMSE vs Fresh 1', font=Small, fill=Muted)
    Draw.text((X1 - 180, Y0 + 89), 'Final rays', font=Small, fill=Muted)
    Reference = Rows[(Case, 'fresh1')]['rmse']
    Start, Width, Maximum = X0 + 258, 190, 1.25
    Line = Start + Width / Maximum
    Draw.line((Line, Y0 + 118, Line, Y0 + 498), fill=Gold, width=2)
    for Index, (Mode, Label) in enumerate(zip(Modes, Labels)):
        Values = Rows[(Case, Mode)]
        Y = Y0 + 125 + Index * 75
        Ratio = Values['rmse'] / Reference
        Colour = Green if Values['quality_fresh1'] == 1 else Gray
        Draw.text((X0 + 22, Y + 8), Label, font=Body, fill=White)
        Draw.rounded_rectangle((Start, Y, Start + Width * Ratio / Maximum, Y + 31), radius=4, fill=Colour)
        Draw.text((Start + Width + 10, Y + 8), f'{Ratio:.3f}x', font=Body, fill=White)
        Draw.text((X1 - 180, Y + 8), f"{Values['mean_rays']:,.0f}", font=Body, fill=White)
    Draw.text((X0 + 22, Y1 - 62),
              f"Fresh 1 RMSE {Reference:.4f}; temporal+disk warm rays {Rows[(Case, 'temporal_disk')]['warm_rays']:,.0f}",
              font=Small, fill=Muted)


PanelFor('metal', (40, 115, 765, 705), 'Metal', 'Separate double-precision hemisphere reference')
PanelFor('faint', (815, 115, 1540, 705), 'Faint layers', 'Displayed reflectance and RMSE scaled by 10^7')
Draw.text((42, 742), 'Green: paired MSE improvement over Fresh 1 passes the fixed 3-SE gate. Gray: no win established.',
          font=Body, fill=White)
Draw.text((42, 778), 'Temporal modes use 3 extra warm frames. Final-ray counts exclude warm-up and are not GPU time.',
          font=Body, fill=Muted)
Draw.text((42, 814), 'For faint layers, the 0.01 positive target floor is far larger than the actual radiance.',
          font=Body, fill=Muted)
Draw.text((42, 850), 'One fixed environment and supplied receivers; no motion, visibility, textured scenes or hardware GPU.',
          font=Body, fill=Muted)
Draw.text((42, 886), 'Same scattering model integrated independently; no general ReSTIR, PBR or equal-time claim.',
          font=Small, fill=Muted)
Output.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Output)
print('SAVED', Output)

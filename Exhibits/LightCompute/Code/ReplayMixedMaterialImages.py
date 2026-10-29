"""Visualize retained mixed-material floor outcomes; this is a chart, not a rendered image."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Log = Root / 'Exhibits/LightCompute/Results/ReplayMixedMaterial-release.txt'
Output = Root / 'Exhibits/LightCompute/Images/ReplayMixedMaterialComparison.png'


def Font(Size, Bold=False):
    try:
        return ImageFont.truetype('DejaVuSans-Bold.ttf' if Bold else 'DejaVuSans.ttf', Size)
    except OSError:
        return ImageFont.load_default()


def ParseRows():
    Content = Log.read_text()
    if 'PASS ReplayMixedMaterialValidation' not in Content:
        raise ValueError('Cannot draw a passing comparison from an incomplete or failed log')
    Rows = {}
    for Line in Content.splitlines():
        if not Line.startswith('REPLAY_MIXED group='):
            continue
        Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
        Key = (Fields.pop('group'), Fields.pop('mode'), Fields.pop('floor'))
        if Key in Rows:
            raise ValueError('Duplicate result: ' + str(Key))
        Rows[Key] = {Name: float(Value) for Name, Value in Fields.items()}
    for Case in ('dim', 'metal'):
        for Mode in ('fresh4', 'temporal_disk'):
            for Floor in ('default', 'lower'):
                if (Case, Mode, Floor) not in Rows or Rows[(Case, Mode, Floor)]['mean_gate'] != 1:
                    raise ValueError('Missing or failed measured result: ' + str((Case, Mode, Floor)))
    return Rows


Rows = ParseRows()
Canvas = Image.new('RGB', (1600, 1090), '#101923')
Draw = ImageDraw.Draw(Canvas)
Title, Heading, Body, Small = Font(31, True), Font(21, True), Font(17), Font(14)
White, Muted, Green, Gray, Red, Gold = '#edf3fa', '#a9bbcc', '#60d1b4', '#8da7bd', '#f08e8a', '#f3ca76'
Panel, Border = '#192735', '#395064'
Draw.text((42, 26), 'Mixed materials and unequal visibility: floor sensitivity', font=Title, fill=White)
Draw.text((42, 70), '32 paired cohorts x 1,024 supplied receivers | one absorbing square | SwiftShader CPU Vulkan',
          font=Body, fill=Muted)


def PanelFor(Group, Box, TitleText, Caption):
    X0, Y0, X1, Y1 = Box
    Draw.rounded_rectangle(Box, radius=12, fill=Panel, outline=Border, width=2)
    Draw.text((X0 + 22, Y0 + 18), TitleText, font=Heading, fill=White)
    Draw.text((X0 + 22, Y0 + 52), Caption, font=Small, fill=Muted)
    Start, Width = X0 + 200, 265
    for Index, (Mode, Label) in enumerate((('fresh4', 'Fresh 4'), ('temporal_disk', 'Temporal + disk'))):
        Y = Y0 + 105 + Index * 177
        Default = Rows[(Group, Mode, 'default')]
        Lower = Rows[(Group, Mode, 'lower')]
        Maximum = max(Default['rmse'], Lower['rmse']) * 1.12
        Draw.text((X0 + 22, Y + 7), Label, font=Body, fill=White)
        Colour = Green if Lower['quality_low'] else Red if Lower['regression_low'] else Gold
        for J, (Value, BarColour, FloorLabel) in enumerate(((Default, Gray, '0.01'), (Lower, Colour, '0.0001'))):
            BarY = Y + J * 40
            Draw.rounded_rectangle((Start, BarY, Start + Width * Value['rmse'] / Maximum, BarY + 26),
                                   radius=4, fill=BarColour)
            Draw.text((Start + Width + 9, BarY + 4), f"{Value['rmse']:.5f}", font=Body, fill=White)
            Draw.text((X1 - 125, BarY + 4), FloorLabel, font=Small, fill=Muted)
        if Lower['quality_low']:
            LabelText = 'Lower-floor improvement passes paired 3-SE gate'
        elif Lower['regression_low']:
            LabelText = 'Lower-floor regression passes paired 3-SE gate'
        else:
            LabelText = 'No 3-SE conclusion; high-variance observation'
        Draw.text((Start, Y + 87), LabelText, font=Small, fill=White)
        Draw.text((Start, Y + 109),
                  f"Final rays: {Default['mean_rays']:,.0f} -> {Lower['mean_rays']:,.0f}", font=Small, fill=Muted)
    Default = Rows[(Group, 'temporal_disk', 'default')]
    Lower = Rows[(Group, 'temporal_disk', 'lower')]
    Draw.text((X0 + 22, Y1 - 65),
              f"Max scaled sample: {Default['max_pixel']:.2f} -> {Lower['max_pixel']:.2f}", font=Small, fill=White)
    Draw.text((X0 + 22, Y1 - 42),
              f"Three warm frames: {Default['warm_rays']:,.0f} -> {Lower['warm_rays']:,.0f} rays", font=Small, fill=Muted)


PanelFor('dim', (40, 120, 780, 660), 'Dim, beneath the square', 'RMSE and samples scaled by 10^3')
PanelFor('metal', (820, 120, 1560, 660), 'Metal, displaced from the square', 'RMSE and samples unscaled')
Draw.text((42, 700), 'Gray = floor 0.01     Green = lower-floor win     Red = regression     Gold = no 3-SE conclusion',
          font=Body, fill=White)
Draw.text((42, 738), 'Bars scale separately per pair. All 12 mean gates passed; rare high-weight paths still matter.',
          font=Body, fill=White)
Draw.text((42, 776), 'Dim temporal+disk RMSE rose sharply, but its paired regression gate did not pass.', font=Body, fill=Muted)
Draw.text((42, 814), 'One audited frame used 1,097 dim-from-metal and 1,055 metal-from-dim spatial links.',
          font=Body, fill=Muted)
Draw.text((42, 852), 'Final ray counts exclude warm-up and are not GPU time; these are not equal-time results.', font=Body, fill=Muted)
Draw.text((42, 890), 'Artificial checkerboard; no textures, real camera, moving scene, physical GPU or production-default change.',
          font=Small, fill=Muted)
Draw.text((42, 928), 'Independent hemisphere and square-area integrals use the same material model; chart is not a render.',
          font=Small, fill=Muted)
Output.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Output)
print('SAVED', Output)

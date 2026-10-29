"""Plot the retained homogeneous target-floor audit; this is a chart, not a camera render."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Log = Root / 'Exhibits/LightCompute/Results/ReplayMaterialFloor-release.txt'
Output = Root / 'Exhibits/LightCompute/Images/ReplayMaterialFloorComparison.png'


def Font(Size, Bold=False):
    try:
        return ImageFont.truetype('DejaVuSans-Bold.ttf' if Bold else 'DejaVuSans.ttf', Size)
    except OSError:
        return ImageFont.load_default()


def ParseRows():
    Content = Log.read_text()
    if 'PASS ReplayMaterialFloorValidation' not in Content:
        raise ValueError('Cannot draw a passing comparison from an incomplete or failed log')
    Rows = {}
    for Line in Content.splitlines():
        if not Line.startswith('REPLAY_MATERIAL_FLOOR case='):
            continue
        Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
        Key = (Fields.pop('case'), Fields.pop('mode'), Fields.pop('floor'))
        if Key in Rows:
            raise ValueError('Duplicate result: ' + str(Key))
        Rows[Key] = {Name: float(Value) for Name, Value in Fields.items()}
    for Case in ('dim', 'faint'):
        for Mode in ('fresh4', 'temporal_disk'):
            for Floor in ('default', 'lower'):
                if (Case, Mode, Floor) not in Rows or Rows[(Case, Mode, Floor)]['mean_gate'] != 1:
                    raise ValueError('Missing or failed measured result: ' + str((Case, Mode, Floor)))
    return Rows


Rows = ParseRows()
Canvas = Image.new('RGB', (1600, 1040), '#101923')
Draw = ImageDraw.Draw(Canvas)
Title, Heading, Body, Small = Font(31, True), Font(21, True), Font(17), Font(14)
White, Muted, Green, Gray = '#edf3fa', '#a9bbcc', '#60d1b4', '#8da7bd'
Panel, Border = '#192735', '#395064'
Draw.text((42, 26), 'Replay target floor: paired quality and counted work', font=Title, fill=White)
Draw.text((42, 70), '32 paired cohorts x 1,024 supplied receivers | one bounce | SwiftShader CPU Vulkan',
          font=Body, fill=Muted)


def PanelFor(Case, Box, TitleText, Caption):
    X0, Y0, X1, Y1 = Box
    Draw.rounded_rectangle(Box, radius=12, fill=Panel, outline=Border, width=2)
    Draw.text((X0 + 22, Y0 + 18), TitleText, font=Heading, fill=White)
    Draw.text((X0 + 22, Y0 + 52), Caption, font=Small, fill=Muted)
    Start, Width = X0 + 200, 265
    for Index, (Mode, Label) in enumerate((('fresh4', 'Fresh 4'), ('temporal_disk', 'Temporal + disk'))):
        Y = Y0 + 106 + Index * 173
        Default = Rows[(Case, Mode, 'default')]
        Lower = Rows[(Case, Mode, 'lower')]
        Maximum = max(Default['rmse'], Lower['rmse']) * 1.12
        Draw.text((X0 + 22, Y + 7), Label, font=Body, fill=White)
        for J, (Value, Colour, FloorLabel) in enumerate(((Default, Gray, '0.01'), (Lower, Green, '0.0001'))):
            BarY = Y + J * 40
            Draw.rounded_rectangle((Start, BarY, Start + Width * Value['rmse'] / Maximum, BarY + 26),
                                   radius=4, fill=Colour)
            Draw.text((Start + Width + 9, BarY + 4), f"{Value['rmse']:.5f}", font=Body, fill=White)
            Draw.text((X1 - 125, BarY + 4), FloorLabel, font=Small, fill=Muted)
        Improvement = 100 * (1 - Lower['rmse'] / Default['rmse'])
        Draw.text((Start, Y + 87), f'Lower-floor RMSE change: -{Improvement:.2f}%', font=Small, fill=White)
        Draw.text((Start, Y + 109),
                  f"Final rays: {Default['mean_rays']:,.0f} -> {Lower['mean_rays']:,.0f}", font=Small, fill=Muted)
    Default = Rows[(Case, 'temporal_disk', 'default')]
    Lower = Rows[(Case, 'temporal_disk', 'lower')]
    Draw.text((X0 + 22, Y1 - 53),
              f"Three warm frames: {Default['warm_rays']:,.0f} -> {Lower['warm_rays']:,.0f} rays",
              font=Small, fill=Muted)


PanelFor('dim', (40, 120, 780, 650), 'Dim layered material', 'RMSE scaled by 10^3; separate hemisphere reference')
PanelFor('faint', (820, 120, 1560, 650), 'Much fainter layered material', 'RMSE scaled by 10^7; separate hemisphere reference')
Draw.text((42, 690), 'Gray = default floor 0.01     Green = lower accepted floor 0.0001', font=Body, fill=White)
Draw.text((42, 727), 'All displayed lower-floor comparisons passed the predeclared paired 3-SE MSE gate.',
          font=Body, fill=White)
Draw.text((42, 764), 'The faint effect is statistically detected but practically tiny in this fixture.', font=Body, fill=Muted)
Draw.text((42, 801), 'Final-ray counts exclude warm-up. Counts are not GPU time; the two methods are not equal-time tests.',
          font=Body, fill=Muted)
Draw.text((42, 838), 'No visibility, mixed-material donors, motion, textured scenes, physical GPU or production-default change.',
          font=Body, fill=Muted)
Draw.text((42, 875), 'Independent integral of the same scattering model; plotted from retained results, not a rendered image.',
          font=Small, fill=Muted)
Output.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Output)
print('SAVED', Output)

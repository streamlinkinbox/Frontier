"""Plot a failed guard holdout, including both its partial gains and unchanged failure."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Folder = Root / 'Exhibits/LightCompute/Results'
Output = Root / 'Exhibits/LightCompute/Images/ReplayMaterialGuardHoldout.png'


def Font(Size, Bold=False):
    try:
        return ImageFont.truetype('DejaVuSans-Bold.ttf' if Bold else 'DejaVuSans.ttf', Size)
    except OSError:
        return ImageFont.load_default()


def Results():
    Rows = {}
    for Name in ('ReplayMaterialGuard-baseline-failed.txt', 'ReplayMaterialGuard-candidate-failed.txt'):
        Text = (Folder / Name).read_text()
        if 'Material-guard holdout failed the independent mean gates' not in Text:
            raise ValueError('Expected the retained failed arm: ' + Name)
        for Line in Text.splitlines():
            if not Line.startswith('REPLAY_GUARD_RESULT '):
                continue
            Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
            Key = tuple(Fields.pop(Name) for Name in ('arm', 'scene', 'group', 'mode', 'floor'))
            if Key in Rows:
                raise ValueError('Duplicate result ' + str(Key))
            Rows[Key] = {Name: float(Value) for Name, Value in Fields.items()}
    Summary = (Folder / 'ReplayMaterialGuard-paired-failed.txt').read_text()
    if 'failed_mean_gates=5' not in Summary or 'FAIL ReplayMaterialGuardPairedSummary' not in Summary:
        raise ValueError('Failed summary is missing')
    return Rows


Rows = Results()
Canvas = Image.new('RGB', (1600, 1010), '#101923')
Draw = ImageDraw.Draw(Canvas)
Title, Heading, Body, Small = Font(31, True), Font(20, True), Font(17), Font(14)
White, Muted, Green, Gray, Red = '#edf3fa', '#a9bbcc', '#60d1b4', '#8da7bd', '#f08e8a'
Panel, Border = '#192735', '#395064'
Draw.text((42, 25), 'Material-boundary candidate: partial win, failed holdout', font=Title, fill=White)
Draw.text((42, 70), '48 paired cohorts x 1,024 supplied receivers | same shader except test-only donor guard',
          font=Body, fill=Muted)

Left = (40, 120, 780, 630)
Right = (820, 120, 1560, 630)
for Box in (Left, Right):
    Draw.rounded_rectangle(Box, radius=12, fill=Panel, outline=Border, width=2)
Draw.text((62, 139), 'Mixed materials: temporal + disk RMSE', font=Heading, fill=White)
Draw.text((62, 172), 'Both shown comparisons pass their mean gates and paired 3-SE MSE test.', font=Small, fill=Muted)
for Index, (Group, Floor, Label) in enumerate((('dim', 'default', 'Dim / 0.01'), ('metal', 'lower', 'Metal / 0.0001'))):
    Y = 230 + Index * 150
    Old = Rows[('baseline', 'mixed', Group, 'temporal_disk', Floor)]
    New = Rows[('guard', 'mixed', Group, 'temporal_disk', Floor)]
    assert Old['mean_gate'] == New['mean_gate'] == 1
    Maximum = max(Old['rmse'], New['rmse']) * 1.12
    Draw.text((62, Y + 5), Label, font=Body, fill=White)
    for J, (Row, Colour) in enumerate(((Old, Gray), (New, Green))):
        BarY = Y + J * 40
        Draw.rounded_rectangle((255, BarY, 255 + 235 * Row['rmse'] / Maximum, BarY + 25), radius=4, fill=Colour)
        Draw.text((503, BarY + 4), f"{Row['rmse']:.5f}", font=Body, fill=White)
    Improvement = 100 * (1 - New['rmse'] / Old['rmse'])
    Draw.text((255, Y + 86), f'RMSE -{Improvement:.2f}% | final rays {Old["mean_rays"]:,.0f} -> {New["mean_rays"]:,.0f}',
              font=Small, fill=Muted)
Draw.text((62, 562), 'Fewer links also reduce warm rays; ray counts are not GPU time.', font=Small, fill=Muted)

Draw.text((842, 139), 'Same material, unequal visibility: lower floor', font=Heading, fill=White)
Draw.text((842, 172), 'Guard does nothing to same-address donors; both mean gates fail.', font=Small, fill=Muted)
for Index, (Group, Label) in enumerate((('near', 'Near square'), ('far', 'Far from square'))):
    Y = 230 + Index * 150
    Row = Rows[('baseline', 'visibility', Group, 'temporal_disk', 'lower')]
    Guard = Rows[('guard', 'visibility', Group, 'temporal_disk', 'lower')]
    assert Row['mean_gate'] == Guard['mean_gate'] == 0 and Row['mean'] == Guard['mean']
    Max = max(Row['reference'], Row['mean']) * 1.12
    Draw.text((842, Y + 5), Label, font=Body, fill=White)
    for J, (Value, Colour) in enumerate(((Row['reference'], Gray), (Row['mean'], Red))):
        BarY = Y + J * 40
        Draw.rounded_rectangle((1038, BarY, 1038 + 235 * Value / Max, BarY + 25), radius=4, fill=Colour)
        Draw.text((1287, BarY + 4), f'{Value:.5f}', font=Body, fill=White)
    Draw.text((1038, Y + 86), f"Difference {Row['mean'] - Row['reference']:+.5f}; 5 SE + 0.001 = {5*Row['batch_se']+.001:.5f}",
              font=Small, fill=Muted)
Draw.text((842, 562), 'Gray = independent reference   Red = both measured arms', font=Small, fill=Muted)

Draw.text((42, 671), 'Gray = original replay   Green = test-only address guard (left panel)', font=Body, fill=White)
Draw.text((42, 708), '5 of 32 independent mean gates failed across the two arms. Negative-control image hashes matched.',
          font=Body, fill=White)
Draw.text((42, 745), 'Same-material cross-position reuse remains active: address matching cannot solve visibility differences.',
          font=Body, fill=Muted)
Draw.text((42, 782), 'The test-only guard is not ported; no Engine default or range changed. Rare high-weight tails remain uncertain.',
          font=Body, fill=Muted)
Draw.text((42, 819), 'Artificial static fixtures on SwiftShader CPU Vulkan; not equal-time, physical-GPU or production-engine results.',
          font=Small, fill=Muted)
Output.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Output)
print('SAVED', Output)
